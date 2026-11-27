#include "ui/placement_palette.h"

#include <algorithm>
#include <commctrl.h>
#include <cstring>
#include <cwchar>
#include <map>

#include "map/map_document.h"
#include "terrain/terrain_types.h"
#include "terrain/tileset.h"
#include "ui/dlg_common.h"
#include "ui/main_window.h"

namespace scmd {

namespace {

constexpr const wchar_t* kPaletteClass = L"SCMDPlusPlacementPalette";
constexpr int kTabId = 100;
constexpr int kTypeComboId = 201;
constexpr int kModeComboId = 202;
constexpr int kSizeComboId = 203;
constexpr int kModeButtonId = 210;
constexpr int kIconSize = 32;
constexpr int kMaxBrushSize = 14;   // 与工具栏 01..14 一致

// 把任意尺寸的 BGRA 图像按最近邻缩放并居中到 32×32 透明画布
std::vector<u8> FitIcon32(const std::vector<u8>& src, int sw, int sh) {
    std::vector<u8> out(static_cast<std::size_t>(kIconSize) * kIconSize * 4, 0);
    if (sw <= 0 || sh <= 0 || src.size() < static_cast<std::size_t>(sw) * sh * 4) return out;
    int dw = kIconSize;
    int dh = kIconSize;
    if (sw >= sh) {
        dh = std::max(1, kIconSize * sh / sw);
    } else {
        dw = std::max(1, kIconSize * sw / sh);
    }
    const int ox = (kIconSize - dw) / 2;
    const int oy = (kIconSize - dh) / 2;
    for (int y = 0; y < dh; y++) {
        const int sy = std::min(sh - 1, y * sh / dh);
        for (int x = 0; x < dw; x++) {
            const int sx = std::min(sw - 1, x * sw / dw);
            const u8* p = src.data() + (static_cast<std::size_t>(sy) * sw + sx) * 4;
            u8* q = out.data() + (static_cast<std::size_t>(oy + y) * kIconSize + ox + x) * 4;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[3] = p[3];
        }
    }
    return out;
}

void SetControlFont(HWND h) {
    if (h) ::SendMessageW(h, WM_SETFONT,
                          reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

}  // namespace

void PlacementPalette::Show(HINSTANCE instance, HWND owner, MainWindow* win, int tab) {
    instance_ = instance;
    owner_ = owner;
    win_ = win;
    if (tab >= 0) tab_ = std::clamp(tab, 0, 3);

    if (!hwnd_) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &PlacementPalette::WndProcThunk;
        wc.hInstance = instance;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kPaletteClass;
        ::RegisterClassExW(&wc);

        RECT o = {0, 0, 640, 480};
        if (owner_) ::GetWindowRect(owner_, &o);
        int x = static_cast<int>(o.right) + 8;
        int y = static_cast<int>(o.top) + 80;
        ClampWindowToWorkArea(x, y, 420, 460);
        hwnd_ = ::CreateWindowExW(
            WS_EX_TOOLWINDOW, kPaletteClass, L"放置面板",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
            x, y, 420, 460, owner_, nullptr, instance, this);
        if (!hwnd_) return;
        CreateChildren();
        Layout();
    }
    if (tabs_) TabCtrl_SetCurSel(tabs_, tab_);
    Refresh();
    ::ShowWindow(hwnd_, SW_SHOW);
    ::SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void PlacementPalette::Hide() {
    if (hwnd_) ::ShowWindow(hwnd_, SW_HIDE);
}

LRESULT CALLBACK PlacementPalette::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam,
                                               LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* self = reinterpret_cast<PlacementPalette*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<PlacementPalette*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->WndProc(hwnd, msg, wParam, lParam)
                : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

void PlacementPalette::CreateChildren() {
    tabs_ = ::CreateWindowExW(0, WC_TABCONTROLW, L"",
                              WS_CHILD | WS_VISIBLE | TCS_TOOLTIPS, 0, 0, 0, 0, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabId)),
                              instance_, nullptr);
    if (tabs_) {
        static const wchar_t* kTabs[] = {L"地形", L"单位", L"装饰物", L"地区"};
        for (int i = 0; i < 4; i++) {
            TCITEMW item = {};
            item.mask = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(kTabs[i]);
            TabCtrl_InsertItem(tabs_, i, &item);
        }
        SetControlFont(tabs_);
    }

    typeCombo_ = ::CreateWindowExW(
        0, L"ComboBox", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
        0, 200, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTypeComboId)), instance_, nullptr);
    SetControlFont(typeCombo_);

    // 原版地形调色板的两个下拉：放置方式 + 笔刷尺寸
    modeCombo_ = ::CreateWindowExW(
        0, L"ComboBox", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
        0, 200, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kModeComboId)), instance_, nullptr);
    if (modeCombo_) {
        static const wchar_t* kModes[] = {
            L"标准地形（Isometrical）", L"方块地形（Rectangular）",
            L"子瓦片（Subtile）", L"瓦片索引（Tileset Indexed）",
            L"复制/剪切/粘贴（Copy/Cut/Paste）",
        };
        for (int i = 0; i < 5; i++) {
            ::SendMessageW(modeCombo_, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(kModes[i]));
        }
        SetControlFont(modeCombo_);
    }
    sizeCombo_ = ::CreateWindowExW(
        0, L"ComboBox", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
        0, 200, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSizeComboId)), instance_, nullptr);
    if (sizeCombo_) {
        wchar_t label[8];
        for (int i = 1; i <= kMaxBrushSize; i++) {
            ::wsprintfW(label, L"%02d", i);
            ::SendMessageW(sizeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        }
        SetControlFont(sizeCombo_);
    }

    list_ = ::CreateWindowExW(
        0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_VSCROLL,
        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
    if (list_) {
        ListView_SetExtendedListViewStyle(list_, LVS_EX_DOUBLEBUFFER);
        ListView_SetIconSpacing(list_, 52, 46);
    }

    static const wchar_t* kModes[] = {L"整齐描绘", L"方格描绘", L"任意描绘"};
    for (int i = 0; i < 3; i++) {
        modeButtons_[i] = ::CreateWindowExW(
            0, L"Button", kModes[i], WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kModeButtonId + i)), instance_,
            nullptr);
        SetControlFont(modeButtons_[i]);
    }
}

void PlacementPalette::Layout() {
    if (!hwnd_) return;
    RECT rc = {};
    ::GetClientRect(hwnd_, &rc);
    const int w = static_cast<int>(rc.right);
    const int h = static_cast<int>(rc.bottom);
    const int pad = 8;
    if (tabs_) {
        ::SetWindowPos(tabs_, nullptr, pad, pad, w - pad * 2, 26, SWP_NOZORDER);
    }
    const bool terrain = tab_ == 0;
    const bool locations = tab_ == 3;
    const TerrainPlacementMode mode =
        win_ ? win_->terrainPlacementMode() : TerrainPlacementMode::kIsometric;
    const bool typeRow =
        terrain && (mode == TerrainPlacementMode::kSubtile ||
                    mode == TerrainPlacementMode::kIndexed);
    const int top = pad + 26 + 6;
    const int sizeW = 52;
    if (modeCombo_) {
        ::SetWindowPos(modeCombo_, nullptr, pad + 2, top, w - pad * 2 - sizeW - 8, 200,
                       SWP_NOZORDER);
    }
    if (sizeCombo_) {
        ::SetWindowPos(sizeCombo_, nullptr, w - pad - sizeW + 2, top, sizeW, 200,
                       SWP_NOZORDER);
    }
    if (typeCombo_) {
        ::SetWindowPos(typeCombo_, nullptr, pad + 4, top + 30, w - pad * 2 - 8, 200,
                       SWP_NOZORDER);
    }
    const int listTop = terrain ? top + 30 + (typeRow ? 32 : 0) : top;
    const int listBottom = locations ? h - pad - 30 : h - pad;
    if (list_) {
        ::SetWindowPos(list_, nullptr, pad, listTop, w - pad * 2,
                       std::max(32, listBottom - listTop), SWP_NOZORDER);
    }
    if (locations) {
        const int bw = std::max(60, (w - pad * 2 - 8) / 3);
        for (int i = 0; i < 3; i++) {
            if (!modeButtons_[i]) continue;
            ::SetWindowPos(modeButtons_[i], nullptr, pad + i * (bw + 4), h - pad - 26, bw, 24,
                           SWP_NOZORDER);
        }
    }
}

void PlacementPalette::ShowTabControls() {
    const bool terrain = tab_ == 0;
    const bool locations = tab_ == 3;
    const TerrainPlacementMode mode =
        win_ ? win_->terrainPlacementMode() : TerrainPlacementMode::kIsometric;
    const bool typeRow =
        terrain && (mode == TerrainPlacementMode::kSubtile ||
                    mode == TerrainPlacementMode::kIndexed);
    if (modeCombo_) ::ShowWindow(modeCombo_, terrain ? SW_SHOW : SW_HIDE);
    if (sizeCombo_) ::ShowWindow(sizeCombo_, terrain ? SW_SHOW : SW_HIDE);
    if (typeCombo_) ::ShowWindow(typeCombo_, typeRow ? SW_SHOW : SW_HIDE);
    for (HWND btn : modeButtons_) {
        if (btn) ::ShowWindow(btn, locations ? SW_SHOW : SW_HIDE);
    }
    if (list_) {
        // 地区用列表视图（一行一条，坐标完整可见）；其余用大图标网格
        const LONG_PTR want = locations ? LVS_LIST : LVS_ICON;
        const LONG_PTR style = ::GetWindowLongPtrW(list_, GWL_STYLE);
        if ((style & LVS_TYPEMASK) != want) {
            ::SetWindowLongPtrW(list_, GWL_STYLE, (style & ~LVS_TYPEMASK) | want);
            ::SetWindowPos(list_, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        ::ShowWindow(list_, SW_SHOW);
    }
}

bool PlacementPalette::TerrainTypeListMode() const {
    if (!win_) return true;
    const TerrainPlacementMode mode = win_->terrainPlacementMode();
    return mode == TerrainPlacementMode::kIsometric ||
           mode == TerrainPlacementMode::kRectangular;
}

void PlacementPalette::Refresh() {
    if (!hwnd_) return;
    RebuildCurrentPage();
}

void PlacementPalette::RefreshTerrain() {
    if (!hwnd_ || tab_ != 0) return;
    RebuildCurrentPage();
}

void PlacementPalette::BeginUpdate() {
    updating_++;
    if (list_) ::SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
}

void PlacementPalette::EndUpdate() {
    if (list_) {
        ::SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        ::InvalidateRect(list_, nullptr, TRUE);
    }
    if (updating_ > 0) updating_--;
}

void PlacementPalette::DestroyImageList() {
    if (images_) {
        if (list_) ListView_SetImageList(list_, nullptr, LVSIL_NORMAL);
        ImageList_Destroy(images_);
        images_ = nullptr;
    }
}

void PlacementPalette::ClearItems() {
    BeginUpdate();
    if (list_) ListView_DeleteAllItems(list_);
    DestroyImageList();
    itemValues_.clear();
    EndUpdate();
}

int PlacementPalette::AddPlaceholderIcon() {
    std::vector<u8> dark(static_cast<std::size_t>(kIconSize) * kIconSize * 4, 0);
    for (std::size_t i = 0; i < dark.size(); i += 4) {
        dark[i + 0] = 64;
        dark[i + 1] = 64;
        dark[i + 2] = 64;
        dark[i + 3] = 255;
    }
    return AddIcon(dark);
}

// CV5 空槽（megatile 引用为 0）：原版 Tileset Indexed 表照常占位，画成纯黑格
int PlacementPalette::AddBlackIcon() {
    std::vector<u8> black(static_cast<std::size_t>(kIconSize) * kIconSize * 4, 0);
    for (std::size_t i = 0; i < black.size(); i += 4) black[i + 3] = 255;
    return AddIcon(black);
}

int PlacementPalette::AddIcon(const std::vector<u8>& bgra32) {
    if (!images_ || bgra32.size() < static_cast<std::size_t>(kIconSize) * kIconSize * 4) {
        return -1;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kIconSize;
    bi.bmiHeader.biHeight = -kIconSize;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HDC screen = hwnd_ ? ::GetDC(hwnd_) : nullptr;
    HBITMAP bmp = screen ? ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)
                         : nullptr;
    if (screen) ::ReleaseDC(hwnd_, screen);
    if (!bmp || !bits) {
        if (bmp) ::DeleteObject(bmp);
        return -1;
    }
    std::memcpy(bits, bgra32.data(), static_cast<std::size_t>(kIconSize) * kIconSize * 4);
    const int index = ImageList_Add(images_, bmp, nullptr);
    ::DeleteObject(bmp);
    return index;
}

bool PlacementPalette::RenderTile(u16 value, std::vector<u8>& bgra32) const {
    if (!win_) return false;
    const MapDocument& doc = win_->map();
    if (!doc.hasTileset()) return false;
    const Tileset& ts = doc.tileset();
    u8 block[1024] = {};
    if (!ts.TileBlock(value, block)) return false;
    const Buffer& wpe = ts.palette();
    if (wpe.size() < 1024) return false;
    bgra32.assign(static_cast<std::size_t>(kIconSize) * kIconSize * 4, 0);
    for (int i = 0; i < kIconSize * kIconSize; i++) {
        const u8 idx = block[i];
        bgra32[static_cast<std::size_t>(i) * 4 + 0] = wpe[static_cast<std::size_t>(idx) * 4 + 2];
        bgra32[static_cast<std::size_t>(i) * 4 + 1] = wpe[static_cast<std::size_t>(idx) * 4 + 1];
        bgra32[static_cast<std::size_t>(i) * 4 + 2] = wpe[static_cast<std::size_t>(idx) * 4 + 0];
        bgra32[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    return true;
}

bool PlacementPalette::RenderUnitIcon(u16 type, std::vector<u8>& bgra32) const {
    if (!win_) return false;
    const std::wstring grp = win_->PaletteUnitGrpPath(type);
    if (grp.empty()) return false;
    std::vector<u8> src;
    int w = 0;
    int h = 0;
    if (!win_->PaletteDecodeObject(grp, win_->unitOwner(), 0, src, w, h)) return false;
    bgra32 = FitIcon32(src, w, h);
    return true;
}

bool PlacementPalette::RenderDoodadIcon(u16 type, std::vector<u8>& bgra32) const {
    if (!win_) return false;
    const MapDocument& doc = win_->map();
    if (!doc.IsOpen()) return false;
    for (const Doodad& d : doc.doodads().Items()) {
        if (d.type != type) continue;
        const int tx = static_cast<int>(d.x) / kTilePixels;
        const int ty = static_cast<int>(d.y) / kTilePixels;
        if (tx < 0 || ty < 0 || tx >= doc.width() || ty >= doc.height()) continue;
        return RenderTile(doc.TileAt(tx, ty), bgra32);
    }
    return false;
}

void PlacementPalette::RebuildCurrentPage() {
    if (!hwnd_) return;
    ShowTabControls();
    switch (tab_) {
        case 0: PopulateTerrain(); break;
        case 1: PopulateUnits(); break;
        case 2: PopulateDoodads(); break;
        case 3: PopulateLocations(); break;
        default: break;
    }
    Layout();
}

void PlacementPalette::PopulateTerrain() {
    ClearItems();
    const bool ready = win_ && win_->map().IsOpen() && win_->map().hasTileset();

    updating_++;
    if (modeCombo_) {
        if (ready) {
            const int mode = std::clamp(static_cast<int>(win_->terrainPlacementMode()), 0, 4);
            ::SendMessageW(modeCombo_, CB_SETCURSEL, static_cast<WPARAM>(mode), 0);
        } else {
            ::SendMessageW(modeCombo_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        }
        ::EnableWindow(modeCombo_, ready);
    }
    if (sizeCombo_) {
        if (ready) {
            const int size = std::clamp(win_->terrainBrushWidth(), 1, kMaxBrushSize);
            ::SendMessageW(sizeCombo_, CB_SETCURSEL, static_cast<WPARAM>(size - 1), 0);
        } else {
            ::SendMessageW(sizeCombo_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        }
        ::EnableWindow(sizeCombo_, ready);
    }
    updating_--;

    ShowTabControls();
    if (!ready) {
        BeginUpdate();
        if (typeCombo_) {
            ::SendMessageW(typeCombo_, CB_RESETCONTENT, 0, 0);
            ::EnableWindow(typeCombo_, FALSE);
        }
        if (list_) {
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.pszText = const_cast<wchar_t*>(L"打开地图后可用");
            ListView_InsertItem(list_, &item);
        }
        EndUpdate();
        return;
    }

    terrainSlot_ = std::clamp(win_->terrainTypeSlot(), 0, kTerrainTypeSlots - 1);
    const TerrainPlacementMode mode = win_->terrainPlacementMode();
    if (mode == TerrainPlacementMode::kClipboard) {
        if (typeCombo_) {
            ::SendMessageW(typeCombo_, CB_RESETCONTENT, 0, 0);
            ::EnableWindow(typeCombo_, FALSE);
        }
        BeginUpdate();
        if (list_) {
            LVITEMW item = {};
            item.mask = LVIF_TEXT;
            item.pszText = const_cast<wchar_t*>(L"剪贴板地形：在地图上左键铺设");
            ListView_InsertItem(list_, &item);
        }
        EndUpdate();
        return;
    }
    if (TerrainTypeListMode()) {
        PopulateTerrainTypes();
    } else {
        RebuildTerrainGrid();
    }
}

// 标准/方块地形：网格列出地形类型（每类用组内第一个有效子瓦片作缩略图）
void PlacementPalette::PopulateTerrainTypes() {
    if (!win_ || !win_->map().IsOpen() || !win_->map().hasTileset()) return;
    const int era = win_->map().era();
    const TerrainTypeInfo* types = TerrainTypes(era < kTerrainSetCount ? era : 0);
    if (!types) return;
    const Tileset& ts = win_->map().tileset();

    if (typeCombo_) {
        ::SendMessageW(typeCombo_, CB_RESETCONTENT, 0, 0);
        ::EnableWindow(typeCombo_, FALSE);
    }
    if (list_) ListView_SetIconSpacing(list_, 74, 56);
    images_ = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32, 1, 16);
    BeginUpdate();
    for (int slot = 0; slot < kTerrainTypeSlots; slot++) {
        const u16 group = types[slot].group;
        if (group == 0) continue;
        const int sub = ts.FirstValidSubtile(group);
        if (sub < 0) continue;
        const u16 value = static_cast<u16>((group << 4) | static_cast<u16>(sub));
        std::vector<u8> icon;
        int image = -1;
        if (RenderTile(value, icon)) image = AddIcon(icon);
        if (image < 0) image = AddPlaceholderIcon();
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = static_cast<int>(itemValues_.size());
        item.iImage = image;
        item.pszText = const_cast<wchar_t*>(types[slot].name);
        item.lParam = slot;
        ListView_InsertItem(list_, &item);
        itemValues_.push_back(static_cast<u16>(slot));
    }
    if (images_ && list_) ListView_SetImageList(list_, images_, LVSIL_NORMAL);
    EndUpdate();
}

void PlacementPalette::RebuildTerrainGrid() {
    if (!win_ || !win_->map().IsOpen() || !win_->map().hasTileset()) return;
    const int era = win_->map().era();
    const TerrainTypeInfo* types = TerrainTypes(era < kTerrainSetCount ? era : 0);
    if (!types || terrainSlot_ < 0 || terrainSlot_ >= kTerrainTypeSlots) return;
    const u16 group = types[terrainSlot_].group;
    if (group == 0) return;

    ClearItems();
    if (typeCombo_) ::EnableWindow(typeCombo_, TRUE);
    if (list_) ListView_SetIconSpacing(list_, 52, 46);

    // 类型下拉：只列出有组号的槽位（子瓦片/瓦片索引模式使用）
    BeginUpdate();
    if (typeCombo_) {
        ::SendMessageW(typeCombo_, CB_RESETCONTENT, 0, 0);
        bool valid = false;
        LRESULT select = -1;
        for (int slot = 0; slot < kTerrainTypeSlots; slot++) {
            if (types[slot].group == 0) continue;
            const LRESULT at = ::SendMessageW(
                typeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(types[slot].name));
            if (at == CB_ERR) continue;
            ::SendMessageW(typeCombo_, CB_SETITEMDATA, static_cast<WPARAM>(at),
                           static_cast<LPARAM>(slot));
            if (slot == terrainSlot_) {
                select = at;
                valid = true;
            }
        }
        if (!valid) {
            for (int slot = 0; slot < kTerrainTypeSlots; slot++) {
                if (types[slot].group == 0) continue;
                terrainSlot_ = slot;
                select = 0;
                break;
            }
        }
        if (select >= 0) {
            ::SendMessageW(typeCombo_, CB_SETCURSEL, static_cast<WPARAM>(select), 0);
        }
    }
    EndUpdate();

    images_ = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32, 1, 16);
    BeginUpdate();
    const Tileset& ts = win_->map().tileset();
    // 原版 Tileset Indexed 表按 group<<4|sub 连续枚举 16 格，空槽照样占位（黑色），
    // 便于与原版逐格对照，也允许直接选中空槽做原始索引放置。
    for (int sub = 0; sub < 16; sub++) {
        const u16 value = static_cast<u16>((group << 4) | static_cast<u16>(sub));
        const bool has = ts.HasSubtile(group, static_cast<u16>(sub));
        std::vector<u8> icon;
        int image = -1;
        if (has && RenderTile(value, icon)) image = AddIcon(icon);
        if (image < 0) image = AddBlackIcon();
        wchar_t label[16];
        ::wsprintfW(label, L"%02d", sub);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = static_cast<int>(itemValues_.size());
        item.iImage = image;
        item.pszText = label;
        item.lParam = static_cast<LPARAM>(value);
        ListView_InsertItem(list_, &item);
        itemValues_.push_back(value);
    }
    if (images_ && list_) ListView_SetImageList(list_, images_, LVSIL_NORMAL);
    EndUpdate();
}

void PlacementPalette::PopulateUnits() {
    ClearItems();
    if (!win_ || !win_->map().IsOpen() || !win_->map().hasTileset()) {
        BeginUpdate();
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"打开地图后可用");
        ListView_InsertItem(list_, &item);
        EndUpdate();
        return;
    }
    images_ = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32, 1, 256);
    BeginUpdate();
    for (u16 type = 0; type < 228; type++) {
        if (win_->PaletteUnitGrpPath(type).empty()) continue;
        std::vector<u8> icon;
        int image = RenderUnitIcon(type, icon) ? AddIcon(icon) : -1;
        if (image < 0) image = AddPlaceholderIcon();
        std::wstring name = win_->PaletteUnitName(type);
        wchar_t label[128];
        if (name.empty()) {
            ::wsprintfW(label, L"类型 %u", static_cast<unsigned>(type));
        } else {
            ::wsprintfW(label, L"%s", name.c_str());
        }
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = static_cast<int>(itemValues_.size());
        item.iImage = image;
        item.pszText = label;
        item.lParam = static_cast<LPARAM>(type);
        ListView_InsertItem(list_, &item);
        itemValues_.push_back(type);
    }
    if (images_ && list_) ListView_SetImageList(list_, images_, LVSIL_NORMAL);
    EndUpdate();
}

void PlacementPalette::PopulateDoodads() {
    ClearItems();
    if (!win_ || !win_->map().IsOpen()) {
        BeginUpdate();
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"打开地图后可用");
        ListView_InsertItem(list_, &item);
        EndUpdate();
        return;
    }
    std::map<u16, int> counts;
    for (const Doodad& d : win_->map().doodads().Items()) counts[d.type]++;
    images_ = ImageList_Create(kIconSize, kIconSize, ILC_COLOR32, 1, 64);
    BeginUpdate();
    if (counts.empty()) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"地图中没有装饰物");
        ListView_InsertItem(list_, &item);
    } else {
        for (const auto& kv : counts) {
            std::vector<u8> icon;
            int image = RenderDoodadIcon(kv.first, icon) ? AddIcon(icon) : -1;
            if (image < 0) image = AddPlaceholderIcon();
            wchar_t label[64];
            ::wsprintfW(label, L"类型 %u  ×%d", static_cast<unsigned>(kv.first), kv.second);
            LVITEMW item = {};
            item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            item.iItem = static_cast<int>(itemValues_.size());
            item.iImage = image;
            item.pszText = label;
            item.lParam = static_cast<LPARAM>(kv.first);
            ListView_InsertItem(list_, &item);
            itemValues_.push_back(kv.first);
        }
    }
    if (images_ && list_) ListView_SetImageList(list_, images_, LVSIL_NORMAL);
    EndUpdate();
}

void PlacementPalette::PopulateLocations() {
    ClearItems();
    if (!win_ || !win_->map().IsOpen()) {
        BeginUpdate();
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"打开地图后可用");
        ListView_InsertItem(list_, &item);
        EndUpdate();
        return;
    }
    BeginUpdate();
    int index = 0;
    for (const Location& l : win_->map().locations().Items()) {
        wchar_t label[96];
        ::wsprintfW(label, L"%02d  (%d,%d)-(%d,%d)", index + 1, l.left / kTilePixels,
                    l.top / kTilePixels, l.right / kTilePixels, l.bottom / kTilePixels);
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = index;
        item.pszText = label;
        item.lParam = index;
        ListView_InsertItem(list_, &item);
        itemValues_.push_back(static_cast<u16>(index));
        index++;
    }
    if (index == 0) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"地图中没有地点，拖拽即可创建");
        ListView_InsertItem(list_, &item);
    }
    EndUpdate();
}

void PlacementPalette::OnTabChanged() {
    if (!tabs_ || updating_) return;
    const int sel = TabCtrl_GetCurSel(tabs_);
    if (sel < 0) return;
    tab_ = sel;
    if (win_) {
        switch (tab_) {
            case 0: win_->PaletteSetLayer(EditLayer::kTerrain); break;
            case 1: win_->PaletteSetLayer(EditLayer::kUnits); break;
            case 2: win_->PaletteSetLayer(EditLayer::kDoodads); break;
            case 3: win_->PaletteSetLayer(EditLayer::kLocations); break;
            default: break;
        }
    }
    RebuildCurrentPage();
}

void PlacementPalette::ApplySelection(int index) {
    if (updating_ || !win_ || index < 0 ||
        index >= static_cast<int>(itemValues_.size())) {
        return;
    }
    const u16 value = itemValues_[static_cast<std::size_t>(index)];
    switch (tab_) {
        case 0:
            if (TerrainTypeListMode()) {
                // 网格项是地形类型槽位：用组内第一个有效子瓦片作为放置值
                const int era = win_->map().era();
                const TerrainTypeInfo* types = TerrainTypes(era < kTerrainSetCount ? era : 0);
                const int slot = value;
                if (types && slot >= 0 && slot < kTerrainTypeSlots &&
                    types[slot].group != 0) {
                    const int sub = win_->map().tileset().FirstValidSubtile(types[slot].group);
                    if (sub >= 0) {
                        win_->PaletteSetBrushTile(
                            static_cast<u16>((types[slot].group << 4) | sub));
                    }
                }
            } else {
                win_->PaletteSetBrushTile(value);
            }
            win_->PaletteSetLayer(EditLayer::kTerrain);
            break;
        case 1:
            win_->SetUnitPlacement(value, win_->unitOwner(), win_->unitResource());
            win_->PaletteSetLayer(EditLayer::kUnits);
            break;
        case 2:
            win_->SetDoodadPlacement(value, win_->unitOwner(), false);
            win_->PaletteSetLayer(EditLayer::kDoodads);
            break;
        case 3:
            win_->PaletteSetLayer(EditLayer::kLocations);
            break;
        default:
            break;
    }
}

LRESULT PlacementPalette::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 340;
            info->ptMinTrackSize.y = 320;
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == kTypeComboId && code == CBN_SELCHANGE && typeCombo_) {
                const LRESULT sel = ::SendMessageW(typeCombo_, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    const LRESULT slot = ::SendMessageW(typeCombo_, CB_GETITEMDATA,
                                                        static_cast<WPARAM>(sel), 0);
                    if (slot >= 0) {
                        terrainSlot_ = static_cast<int>(slot);
                        if (win_) win_->SetTerrainTypeSlot(terrainSlot_);
                    }
                }
                return 0;
            }
            if (id == kModeComboId && code == CBN_SELCHANGE && modeCombo_) {
                const LRESULT sel = ::SendMessageW(modeCombo_, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR && win_) {
                    win_->SetTerrainPlacementMode(
                        static_cast<TerrainPlacementMode>(static_cast<int>(sel)));
                    win_->PaletteSetLayer(EditLayer::kTerrain);
                }
                return 0;
            }
            if (id == kSizeComboId && code == CBN_SELCHANGE && sizeCombo_) {
                const LRESULT sel = ::SendMessageW(sizeCombo_, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR && win_) {
                    win_->SetTerrainBrushSize(static_cast<int>(sel) + 1);
                }
                return 0;
            }
            if (id >= kModeButtonId && id < kModeButtonId + 3 && code == BN_CLICKED) {
                if (win_) {
                    win_->SetLocationDrawMode(id - kModeButtonId);
                    win_->PaletteSetLayer(EditLayer::kLocations);
                }
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr && hdr->hwndFrom == tabs_ && hdr->code == TCN_SELCHANGE) {
                OnTabChanged();
                return 0;
            }
            if (hdr && hdr->hwndFrom == list_ && hdr->code == LVN_ITEMCHANGED) {
                auto* nm = reinterpret_cast<NMLISTVIEW*>(lParam);
                if ((nm->uNewState & LVIS_SELECTED) && !(nm->uOldState & LVIS_SELECTED)) {
                    ApplySelection(nm->iItem);
                }
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            ::ShowWindow(hwnd_, SW_HIDE);
            return 0;
        case WM_NCDESTROY:
            DestroyImageList();
            hwnd_ = nullptr;
            return ::DefWindowProcW(hwnd, msg, wParam, lParam);
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace scmd
