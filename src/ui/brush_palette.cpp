#include "ui/brush_palette.h"

#include <algorithm>
#include <cwchar>

#include <windowsx.h>

#include "common/log.h"
#include "terrain/tileset.h"

namespace scmd {

namespace {
constexpr const wchar_t* kClassName = L"SCMDPlusBrushPalette";
constexpr const wchar_t* kWindowTitle = L"地形刷";
constexpr int kColumns = 10;
constexpr int kCell = 40;          // 32 像素瓦片 + 8 像素间距
constexpr int kMargin = 8;
constexpr int kTileSize = 32;
constexpr int kStatusHeight = 26;
constexpr int kFilterHeight = 32;
constexpr int kGridTop = 40;
constexpr int kClientWidth = kMargin * 2 + kColumns * kCell;
constexpr int kClientHeight = 460;

constexpr int kFilterWalkId = 1001;
constexpr int kFilterTypeId = 1002;
constexpr int kFilterGroupId = 1003;

struct GroupRange {
    const wchar_t* label;
    int min;
    int max;
};
const GroupRange kGroupRanges[] = {
    {L"全部组", 0, 0x7FF},
    {L"组 0-511", 0, 511},
    {L"组 512-1023", 512, 1023},
    {L"组 1024-1535", 1024, 1535},
    {L"组 1536-2047", 1536, 2047},
};
constexpr int kGroupRangeCount = static_cast<int>(sizeof(kGroupRanges) / sizeof(kGroupRanges[0]));
}  // namespace

std::vector<BrushPalette::Entry> BrushPalette::BuildEntries(const Tileset& ts,
                                                            const Filter& filter) {
    std::vector<Entry> out;
    if (!ts.IsLoaded() || ts.MegaCount() <= 0) return out;

    u8 block[1024];
    std::vector<u16> seen;   // 当前组已收录的大瓦片
    const int groupStart = std::max(0, filter.groupMin);
    const int groupEnd = std::min(std::min(ts.GroupCount(), 0x800),
                                  std::min(filter.groupMax, 0x7FF) + 1);
    for (int group = groupStart; group < groupEnd; group++) {
        if (filter.walkable >= 0 && ts.IsWalkable(group) != (filter.walkable == 1)) continue;
        if (filter.type >= 0 && ts.Type(group) != filter.type) continue;
        seen.clear();
        for (int index = 0; index < 16; index++) {
            const u16 value = static_cast<u16>((group << 4) | index);
            const u16 mega = ts.MegatileOf(value);
            if (mega >= ts.MegaCount()) continue;
            if (std::find(seen.begin(), seen.end(), mega) != seen.end()) continue;
            if (!ts.TileBlock(value, block)) continue;

            bool painted = false;
            for (int i = 0; i < 1024; i++) {
                if (block[i] != 0) {
                    painted = true;
                    break;
                }
            }
            if (!painted) continue;

            seen.push_back(mega);
            out.push_back({static_cast<u16>(group), static_cast<u16>(index), value, mega});
        }
    }
    return out;
}

bool BrushPalette::Create(HINSTANCE instance, HWND owner) {
    if (hwnd_) return true;
    instance_ = instance;
    owner_ = owner;

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = &BrushPalette::WndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Log(L"[brush] RegisterClass 失败 %u", ::GetLastError());
        return false;
    }

    RECT rc = {0, 0, kClientWidth, kClientHeight};
    ::AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW | WS_VSCROLL, FALSE, WS_EX_TOOLWINDOW);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    RECT ownerRect;
    if (owner && ::GetWindowRect(owner, &ownerRect)) {
        x = ownerRect.left - width - 12;
        if (x < 0) x = ownerRect.right + 12;
        y = ownerRect.top + 40;
    }

    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, kWindowTitle,
                              WS_OVERLAPPEDWINDOW | WS_VSCROLL,
                              x, y, width, height,
                              owner, nullptr, instance, this);
    if (!hwnd_) {
        Log(L"[brush] CreateWindow 失败 %u", ::GetLastError());
        return false;
    }

    tileInfo_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    tileInfo_.bmiHeader.biWidth = kTileSize;
    tileInfo_.bmiHeader.biHeight = -kTileSize;   // 自上而下
    tileInfo_.bmiHeader.biPlanes = 1;
    tileInfo_.bmiHeader.biBitCount = 32;
    tileInfo_.bmiHeader.biCompression = BI_RGB;
    tileBitmap_ = ::CreateDIBSection(nullptr, &tileInfo_, DIB_RGB_COLORS, &tileBits_, nullptr, 0);
    if (!tileBitmap_) {
        Log(L"[brush] CreateDIBSection 失败 %u", ::GetLastError());
        ::DestroyWindow(hwnd_);
        return false;
    }
    CreateFilterControls();
    return true;
}

void BrushPalette::Show(HINSTANCE instance, HWND owner, const Tileset* ts, u16 selected) {
    if (!Create(instance, owner)) return;
    Refresh(ts, selected);
    ::ShowWindow(hwnd_, SW_SHOWNORMAL);
    ::SetForegroundWindow(hwnd_);
}

void BrushPalette::Refresh(const Tileset* ts, u16 selected) {
    ts_ = ts;
    selected_ = selected;
    selectedIndex_ = -1;
    scrollY_ = 0;
    entries_.clear();
    if (!hwnd_) return;

    if (ts_) {
        RebuildFilterControls();
        entries_ = BuildEntries(*ts_, filter_);
    }
    ResolveSelection();
    UpdateScroll();
    UpdateStatus();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void BrushPalette::CreateFilterControls() {
    if (!hwnd_) return;

    auto create = [this](int id, int x, int width) -> HWND {
        HWND combo = ::CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
            x, 7, width, 200, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (combo) {
            ::SendMessageW(combo, WM_SETFONT,
                           reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        }
        return combo;
    };

    filterWalk_ = create(kFilterWalkId, 8, 118);
    filterType_ = create(kFilterTypeId, 132, 130);
    filterGroup_ = create(kFilterGroupId, 268, 140);
}

void BrushPalette::RebuildFilterControls() {
    if (!hwnd_) return;

    ::SendMessageW(filterWalk_, CB_RESETCONTENT, 0, 0);
    ::SendMessageW(filterWalk_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部可走性"));
    ::SendMessageW(filterWalk_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"仅可走"));
    ::SendMessageW(filterWalk_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"仅阻挡"));
    int walkSel = 0;
    if (filter_.walkable == 1) walkSel = 1;
    else if (filter_.walkable == 0) walkSel = 2;
    ::SendMessageW(filterWalk_, CB_SETCURSEL, static_cast<WPARAM>(walkSel), 0);

    ::SendMessageW(filterType_, CB_RESETCONTENT, 0, 0);
    ::SendMessageW(filterType_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部类型"));
    ::SendMessageW(filterType_, CB_SETITEMDATA, 0, static_cast<LPARAM>(-1));

    std::vector<int> types;
    if (ts_) {
        for (int group = 0; group < ts_->GroupCount(); group++) types.push_back(ts_->Type(group));
        std::sort(types.begin(), types.end());
        types.erase(std::unique(types.begin(), types.end()), types.end());
    }
    int typeSel = 0;
    bool typeFound = filter_.type < 0;
    for (std::size_t i = 0; i < types.size(); i++) {
        wchar_t label[32];
        ::wsprintfW(label, L"类型 %d", types[i]);
        const LRESULT at = ::SendMessageW(filterType_, CB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(label));
        if (at != CB_ERR) {
            ::SendMessageW(filterType_, CB_SETITEMDATA, static_cast<WPARAM>(at),
                           static_cast<LPARAM>(types[i]));
            if (types[i] == filter_.type) {
                typeSel = static_cast<int>(at);
                typeFound = true;
            }
        }
    }
    if (!typeFound) filter_.type = -1;
    ::SendMessageW(filterType_, CB_SETCURSEL, static_cast<WPARAM>(typeSel), 0);

    ::SendMessageW(filterGroup_, CB_RESETCONTENT, 0, 0);
    int groupSel = 0;
    bool groupFound = false;
    for (int i = 0; i < kGroupRangeCount; i++) {
        ::SendMessageW(filterGroup_, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(kGroupRanges[i].label));
        if (filter_.groupMin == kGroupRanges[i].min && filter_.groupMax == kGroupRanges[i].max) {
            groupSel = i;
            groupFound = true;
        }
    }
    if (!groupFound) {
        filter_.groupMin = kGroupRanges[0].min;
        filter_.groupMax = kGroupRanges[0].max;
        groupSel = 0;
    }
    ::SendMessageW(filterGroup_, CB_SETCURSEL, static_cast<WPARAM>(groupSel), 0);
}

void BrushPalette::ApplyFilter() {
    const int walkSel = static_cast<int>(::SendMessageW(filterWalk_, CB_GETCURSEL, 0, 0));
    filter_.walkable = walkSel == 1 ? 1 : (walkSel == 2 ? 0 : -1);

    const int typeSel = static_cast<int>(::SendMessageW(filterType_, CB_GETCURSEL, 0, 0));
    if (typeSel <= 0) {
        filter_.type = -1;
    } else {
        filter_.type = static_cast<int>(::SendMessageW(filterType_, CB_GETITEMDATA,
                                                       static_cast<WPARAM>(typeSel), 0));
    }

    const int groupSel = static_cast<int>(::SendMessageW(filterGroup_, CB_GETCURSEL, 0, 0));
    if (groupSel >= 0 && groupSel < kGroupRangeCount) {
        filter_.groupMin = kGroupRanges[groupSel].min;
        filter_.groupMax = kGroupRanges[groupSel].max;
    }

    entries_ = ts_ ? BuildEntries(*ts_, filter_) : std::vector<Entry>();
    selectedIndex_ = -1;
    ResolveSelection();
    scrollY_ = 0;
    UpdateScroll();
    UpdateStatus();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void BrushPalette::ResolveSelection() {
    selectedIndex_ = -1;
    if (!ts_) return;

    for (std::size_t i = 0; i < entries_.size(); i++) {
        if (entries_[i].value == selected_) {
            selectedIndex_ = static_cast<int>(i);
            return;
        }
    }
    // 吸管可能吸到同组的其它索引；按大瓦片视为同一块
    const u16 group = static_cast<u16>((selected_ & 0x7FF0) >> 4);
    const u16 mega = ts_->MegatileOf(selected_);
    for (std::size_t i = 0; i < entries_.size(); i++) {
        if (entries_[i].group == group && entries_[i].mega == mega) {
            selectedIndex_ = static_cast<int>(i);
            return;
        }
    }
}

void BrushPalette::SetSelected(u16 value) {
    selected_ = value;
    ResolveSelection();
    if (!hwnd_) return;
    if (selectedIndex_ >= 0) EnsureVisible(selectedIndex_);
    UpdateStatus();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void BrushPalette::Close() {
    if (hwnd_) ::DestroyWindow(hwnd_);
}

int BrushPalette::HitTest(int x, int y) const {
    const int gx = x - kMargin;
    const int gy = y - kGridTop + scrollY_;
    if (gx < 0 || gy < 0) return -1;
    const int col = gx / kCell;
    const int row = gy / kCell;
    if (col >= kColumns) return -1;
    const int index = row * kColumns + col;
    return index < static_cast<int>(entries_.size()) ? index : -1;
}

void BrushPalette::SelectIndex(int index, bool notify) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    selected_ = entries_[index].value;
    selectedIndex_ = index;
    EnsureVisible(index);
    UpdateStatus();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    if (notify && ::IsWindow(owner_)) {
        ::SendMessageW(owner_, kMsgSelected, static_cast<WPARAM>(selected_), 0);
    }
}

void BrushPalette::EnsureVisible(int index) {
    if (!hwnd_ || index < 0) return;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const int gridHeight = (rc.bottom - rc.top) - kStatusHeight - kGridTop;
    const int top = (index / kColumns) * kCell;
    const int bottom = top + kCell;
    if (top < scrollY_ + kMargin) {
        scrollY_ = std::max(0, top - kMargin);
    } else if (bottom > scrollY_ + gridHeight) {
        scrollY_ = bottom - gridHeight;
    }
    UpdateScroll();
}

void BrushPalette::UpdateScroll() {
    if (!hwnd_) return;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const int gridHeight = std::max(
        1, static_cast<int>(rc.bottom - rc.top) - kStatusHeight - kGridTop);
    const int rows = static_cast<int>((entries_.size() + kColumns - 1) / kColumns);
    const int content = kMargin * 2 + rows * kCell;
    const int maxScroll = std::max(0, content - gridHeight);
    scrollY_ = std::max(0, std::min(scrollY_, maxScroll));

    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, content - 1);
    si.nPage = static_cast<UINT>(gridHeight);
    si.nPos = scrollY_;
    ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void BrushPalette::UpdateStatus() {
    if (selectedIndex_ >= 0 && ts_) {
        const Entry& e = entries_[selectedIndex_];
        ::wsprintfW(status_, L"组 %u  索引 %u  值 0x%04X  类型 %u  %s  (%d/%d)",
                    static_cast<unsigned>(e.group), static_cast<unsigned>(e.index),
                    static_cast<unsigned>(e.value), static_cast<unsigned>(ts_->Type(e.group)),
                    ts_->IsWalkable(e.group) ? L"可走" : L"阻挡",
                    selectedIndex_ + 1, static_cast<int>(entries_.size()));
    } else {
        ::wsprintfW(status_, L"刷子 0x%04X  （共 %d 个瓦片）",
                    static_cast<unsigned>(selected_), static_cast<int>(entries_.size()));
    }
}

void BrushPalette::DrawTile(HDC dc, int x, int y, u16 value) {
    if (!ts_ || !tileBits_) return;

    u8 block[1024];
    if (!ts_->TileBlock(value, block)) return;
    const Buffer& wpe = ts_->palette();
    if (wpe.size() < 256 * 4) return;

    u8* dst = static_cast<u8*>(tileBits_);
    for (int i = 0; i < 1024; i++) {
        const u8* rgb = wpe.data() + static_cast<std::size_t>(block[i]) * 4;
        dst[i * 4 + 0] = rgb[2];
        dst[i * 4 + 1] = rgb[1];
        dst[i * 4 + 2] = rgb[0];
        dst[i * 4 + 3] = 0xFF;
    }
    ::SetDIBitsToDevice(dc, x, y, kTileSize, kTileSize, 0, 0, 0, kTileSize,
                        tileBits_, &tileInfo_, DIB_RGB_COLORS);
}

void BrushPalette::Paint(HDC dc, const RECT& client) {
    RECT grid = client;
    grid.bottom -= kStatusHeight;

    HBRUSH background = ::CreateSolidBrush(RGB(48, 48, 52));
    ::FillRect(dc, &grid, background);
    ::DeleteObject(background);

    if (entries_.empty()) {
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(210, 210, 210));
        grid.top = kGridTop;
        const wchar_t* text = ts_ ? L"没有匹配的瓦片" : L"请先打开地图并载入地形集";
        ::DrawTextW(dc, text, -1, &grid, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        const int gridHeight = static_cast<int>(grid.bottom - grid.top) - kGridTop;
        const int firstRow = std::max(0, scrollY_ / kCell);
        const int rows = static_cast<int>((entries_.size() + kColumns - 1) / kColumns);
        const int lastRow = std::min(rows - 1, (scrollY_ + gridHeight) / kCell + 1);

        for (int row = firstRow; row <= lastRow; row++) {
            for (int col = 0; col < kColumns; col++) {
                const int index = row * kColumns + col;
                if (index >= static_cast<int>(entries_.size())) break;
                const int x = kMargin + col * kCell;
                const int y = kGridTop + row * kCell - scrollY_;
                if (y + kTileSize < kGridTop || y > grid.bottom) continue;

                DrawTile(dc, x, y, entries_[index].value);
                if (index == selectedIndex_) {
                    HPEN pen = ::CreatePen(PS_SOLID, 2, RGB(255, 200, 0));
                    HGDIOBJ oldPen = ::SelectObject(dc, pen);
                    HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
                    ::Rectangle(dc, x - 2, y - 2, x + kTileSize + 2, y + kTileSize + 2);
                    ::SelectObject(dc, oldBrush);
                    ::SelectObject(dc, oldPen);
                    ::DeleteObject(pen);
                }
            }
        }
    }

    RECT status = {0, client.bottom - kStatusHeight, client.right, client.bottom};
    ::FillRect(dc, &status, ::GetSysColorBrush(COLOR_BTNFACE));
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, ::GetSysColor(COLOR_BTNTEXT));
    ::DrawTextW(dc, status_, -1, &status, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

LRESULT CALLBACK BrushPalette::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* self = reinterpret_cast<BrushPalette*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<BrushPalette*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->WndProc(msg, wParam, lParam)
                : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT BrushPalette::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (HIWORD(wParam) == CBN_SELCHANGE &&
                (id == kFilterWalkId || id == kFilterTypeId || id == kFilterGroupId)) {
                ApplyFilter();
                return 0;
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            const HDC dc = ::BeginPaint(hwnd_, &ps);
            RECT rc;
            ::GetClientRect(hwnd_, &rc);
            Paint(dc, rc);
            ::EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            UpdateScroll();
            ::InvalidateRect(hwnd_, nullptr, TRUE);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            ::GetScrollInfo(hwnd_, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= kCell; break;
                case SB_LINEDOWN: pos += kCell; break;
                case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = si.nMax; break;
                default: break;
            }
            scrollY_ = pos;
            UpdateScroll();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            scrollY_ -= (delta / WHEEL_DELTA) * kCell * 3;
            UpdateScroll();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            ::SetFocus(hwnd_);
            const int index = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (index >= 0) SelectIndex(index, true);
            return 0;
        }
        case WM_KEYDOWN: {
            if (entries_.empty()) return 0;
            int index = selectedIndex_ >= 0 ? selectedIndex_ : 0;
            switch (wParam) {
                case VK_LEFT: index -= 1; break;
                case VK_RIGHT: index += 1; break;
                case VK_UP: index -= kColumns; break;
                case VK_DOWN: index += kColumns; break;
                case VK_PRIOR: index -= kColumns * 5; break;
                case VK_NEXT: index += kColumns * 5; break;
                case VK_HOME: index = 0; break;
                case VK_END: index = static_cast<int>(entries_.size()) - 1; break;
                default:
                    return ::DefWindowProcW(hwnd_, msg, wParam, lParam);
            }
            index = std::max(0, std::min(index, static_cast<int>(entries_.size()) - 1));
            SelectIndex(index, true);
            return 0;
        }
        case WM_CLOSE:
            ::DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            if (tileBitmap_) {
                ::DeleteObject(tileBitmap_);
                tileBitmap_ = nullptr;
                tileBits_ = nullptr;
            }
            hwnd_ = nullptr;
            owner_ = nullptr;
            instance_ = nullptr;
            ts_ = nullptr;
            filterWalk_ = nullptr;
            filterType_ = nullptr;
            filterGroup_ = nullptr;
            entries_.clear();
            selectedIndex_ = -1;
            scrollY_ = 0;
            return 0;
        default:
            return ::DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

}  // namespace scmd
