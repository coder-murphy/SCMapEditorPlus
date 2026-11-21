#include "ui/main_window.h"

#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <cwchar>
#include <windows.h>
#include <windowsx.h>

#include "common/log.h"
#include "platform/file_dialog.h"
#include "res/resource.h"
#include "ui/command_ids.h"
#include "ui/dlg_procs.h"
#include "ui/dialogs.h"

namespace scmd {

namespace {
constexpr const wchar_t* kWindowClass = L"SCMDPlusMainWindow";
constexpr const wchar_t* kWindowTitle = L"SCMD Plus 3.0";

constexpr const wchar_t* kMapFilter =
    L"StarCraft 地图 (*.scx;*.scm)\0*.scx;*.scm\0所有文件 (*.*)\0*.*\0\0";

std::wstring FileNameOf(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

int NextZoom(int current, int direction) {
    static const int kSteps[] = {2, 4, 8, 16, 32};
    int idx = 0;
    for (int i = 0; i < 5; i++) {
        if (kSteps[i] == current) { idx = i; break; }
    }
    idx += direction;
    if (idx < 0) idx = 0;
    if (idx > 4) idx = 4;
    return kSteps[idx];
}

COLORREF UnitColor(u16 type) {
    if (type >= 176 && type <= 178) return RGB(0, 200, 255);    // 矿物
    if (type == 188) return RGB(0, 255, 130);                   // 气矿
    if (type == 214) return RGB(255, 255, 255);                 // 出生点
    return RGB(255, 150, 0);
}

// 递归找到直接包含命令 ID 的子菜单（用于按命令 ID 勾选/启用）
HMENU FindMenuContaining(HMENU menu, int id) {
    if (!menu) return nullptr;
    const int count = ::GetMenuItemCount(menu);
    for (int i = 0; i < count; i++) {
        if (::GetMenuItemID(menu, i) == static_cast<UINT>(id)) return menu;
    }
    for (int i = 0; i < count; i++) {
        if (HMENU sub = ::GetSubMenu(menu, i)) {
            if (HMENU found = FindMenuContaining(sub, id)) return found;
        }
    }
    return nullptr;
}

// 在一组互斥命令中只勾选 checkedId
void CheckOnly(HMENU bar, const int* ids, int count, int checkedId) {
    for (int i = 0; i < count; i++) {
        if (HMENU menu = FindMenuContaining(bar, ids[i])) {
            ::CheckMenuItem(menu, ids[i], MF_BYCOMMAND |
                            (ids[i] == checkedId ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}

const int kLayerIds[] = {0x803A, 0x803B, 0x803E, 0x803C, 0x803D, 0x803F};
const int kGridIds[] = {0x80AF, 0x800E, 0x800D, 0x80A5, 0x80AA, 0x80CF, 0x80D6};
const int kGridColorIds[] = {0x8036, 0x80B1, 0x8037, 0x8038, 0x8039, 0x80B2, 0x80D0};

int GridPresetOf(bool enabled, int cellX, int cellY) {
    if (!enabled) return 0x80D6;      // 禁止
    if (cellX != cellY) return 0x80CF;  // 自定义
    switch (cellX) {
        case 8: return 0x80AF;        // 微小
        case 16: return 0x800E;       // 较小
        case 32: return 0x800D;       // 普通
        case 64: return 0x80A5;       // 较大
        case 128: return 0x80AA;      // 极大
        default: return 0x80CF;       // 自定义
    }
}
}  // namespace

bool MainWindow::Create(HINSTANCE hInstance) {
    hInstance_ = hInstance;
    renderer_.SetTileset(&map_.tileset());

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindow::WndProcThunk;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hIcon = ::LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    if (!::RegisterClassExW(&wc)) return false;

    hwnd_ = ::CreateWindowExW(0, kWindowClass, kWindowTitle,
                              WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
                              nullptr, ::LoadMenuW(hInstance, MAKEINTRESOURCEW(IDR_MAINMENU)),
                              hInstance, this);
    if (!hwnd_) return false;

    // 主菜单加速键：原始 RT_ACCELERATOR 109（Ctrl+N/O/S/Z/Y、Alt+网格尺寸…）
    hAccel_ = ::LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL_MAIN));

    // 原菜单没有“恢复的界面”入口，追加到「帮助」子菜单，方便逐个检查 25 个对话框
    if (HMENU bar = ::GetMenu(hwnd_)) {
        if (HMENU help = ::GetSubMenu(bar, 8)) {
            ::AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(help, MF_STRING, kDebugDialogs, L"恢复的界面...");
        }
        // 原程序把显示选项里的 4 项灰掉了；本工程可用，解除灰显
        const int kEnableIds[] = {kDispAllColors, kDispUnits, kDispDoodads, kDispSpUnits};
        for (int id : kEnableIds) {
            if (HMENU menu = FindMenuContaining(bar, id)) {
                ::EnableMenuItem(menu, id, MF_BYCOMMAND | MF_ENABLED);
            }
        }
    }
    SyncMenuChecks();

    UpdateTitle();
    ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);
    return true;
}

bool MainWindow::TranslateAccel(MSG& msg) const {
    return hAccel_ && ::TranslateAcceleratorW(hwnd_, hAccel_, &msg) != 0;
}

void MainWindow::UpdateTitle() {
    std::wstring title = kWindowTitle;
    if (map_.IsOpen()) {
        title += L" - ";
        title += fileName_.empty() ? L"未命名" : fileName_;
        wchar_t info[192];
        ::wsprintfW(info, L"  [%dx%d  ERA %u %s  %u 个单位%s%s]",
                    map_.width(), map_.height(), static_cast<unsigned>(map_.era()),
                    map_.eraName(), static_cast<unsigned>(map_.unitCount()),
                    map_.hasTileset() ? L"" : L"，无地形集",
                    map_.IsDirty() ? L"，已修改" : L"");
        title += info;
    }
    ::SetWindowTextW(hwnd_, title.c_str());
}

void MainWindow::ReleaseBackBuffer() {
    if (backBuffer_) {
        ::DeleteObject(backBuffer_);
        backBuffer_ = nullptr;
    }
    dibBits_ = nullptr;
    image_.pixels.clear();
    image_.width = image_.height = 0;
}

bool MainWindow::RebuildView(int pxPerTile) {
    ReleaseBackBuffer();
    pxPerTile_ = pxPerTile;
    if (!map_.IsOpen() || !map_.hasTileset()) return false;
    if (!renderer_.Render(map_.tiles(), map_.width(), map_.height(), pxPerTile, image_)) return false;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = image_.width;
    bi.bmiHeader.biHeight = -image_.height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    const HDC screen = ::GetDC(hwnd_);
    if (!screen) return false;
    backBuffer_ = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(hwnd_, screen);
    if (!backBuffer_ || !bits) return false;

    dibBits_ = bits;
    std::memcpy(dibBits_, image_.pixels.data(), image_.pixels.size());

    scrollX_ = scrollY_ = 0;
    UpdateScrollBars();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void MainWindow::UpdateScrollBars() {
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const int viewW = rc.right - rc.left;
    const int viewH = rc.bottom - rc.top;
    const int maxX = std::max(0, image_.width - viewW);
    const int maxY = std::max(0, image_.height - viewH);
    scrollX_ = std::min(scrollX_, maxX);
    scrollY_ = std::min(scrollY_, maxY);
    ::SetScrollRange(hwnd_, SB_HORZ, 0, maxX, TRUE);
    ::SetScrollPos(hwnd_, SB_HORZ, scrollX_, TRUE);
    ::SetScrollRange(hwnd_, SB_VERT, 0, maxY, TRUE);
    ::SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
}

bool MainWindow::OpenMap(const std::wstring& path) {
    if (!map_.Open(path, tilesetDir_)) {
        ::MessageBoxW(hwnd_, L"无法打开该地图文件。", kWindowTitle, MB_ICONWARNING);
        return false;
    }
    mapPath_ = path;
    fileName_ = FileNameOf(path);
    history_.Clear();
    editOpen_ = false;
    UpdateTitle();
    RebuildView(pxPerTile_);
    return true;
}

void MainWindow::NewMap() {
    if (!MaybeSave()) return;
    if (!map_.CreateNew(64, 64, 0, tilesetDir_)) return;
    mapPath_.clear();
    fileName_.clear();
    history_.Clear();
    editOpen_ = false;
    UpdateTitle();
    RebuildView(pxPerTile_);
}

void MainWindow::CloseMap() {
    if (!MaybeSave()) return;
    map_.Close();
    mapPath_.clear();
    fileName_.clear();
    history_.Clear();
    editOpen_ = false;
    ReleaseBackBuffer();
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

bool MainWindow::SaveMap(bool saveAs) {
    if (!map_.IsOpen()) return false;
    std::wstring path = mapPath_;
    if (saveAs || path.empty()) {
        if (!SaveFileDialog(hwnd_, kMapFilter, L"scx", path)) return false;
    }
    if (!map_.Save(path)) {
        ::MessageBoxW(hwnd_, L"保存地图失败。", kWindowTitle, MB_ICONWARNING);
        return false;
    }
    mapPath_ = path;
    fileName_ = FileNameOf(path);
    UpdateTitle();
    return true;
}

bool MainWindow::MaybeSave() {
    if (!map_.IsOpen() || !map_.IsDirty()) return true;
    const int answer = ::MessageBoxW(hwnd_, L"当前地图已修改，是否保存？", kWindowTitle,
                                     MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) return false;
    if (answer == IDYES) return SaveMap(false);
    return true;
}

void MainWindow::SetLayer(EditLayer layer) {
    layer_ = layer;
    switch (layer) {
        case EditLayer::kTerrain:
            tool_ = Tool::kTerrain;
            break;
        case EditLayer::kUnits:
        case EditLayer::kSpUnits:
            tool_ = Tool::kUnit;
            break;
        default:
            tool_ = Tool::kNone;   // 装饰物/迷雾/地点：只显示，不放置
            break;
    }
    if (layer == EditLayer::kLocations) showLocations_ = true;
    SyncMenuChecks();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::ApplyGridPreset(int cell) {
    gridEnabled_ = cell > 0;
    if (cell > 0) {
        gridCellX_ = gridCellY_ = cell;
        gridOffsetX_ = gridOffsetY_ = 0;
    }
    SyncMenuChecks();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::OpenGridSettings() {
    GridSettings seed;
    seed.enabled = gridEnabled_;
    seed.cellX = gridCellX_;
    seed.cellY = gridCellY_;
    seed.offsetX = gridOffsetX_;
    seed.offsetY = gridOffsetY_;
    SeedGridSettings(seed);

    ShowRecoveredDialog(IDD_DLG_166, hwnd_);
    if (LastGridSettingsAccepted()) {
        const GridSettings& s = LastGridSettings();
        gridEnabled_ = s.enabled;
        gridCellX_ = s.cellX;
        gridCellY_ = s.cellY;
        gridOffsetX_ = s.offsetX;
        gridOffsetY_ = s.offsetY;
        SyncMenuChecks();
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void MainWindow::SetGridColor(COLORREF color, int commandId) {
    if (commandId == kGridColorOther) {
        static COLORREF custom[16] = {0};
        CHOOSECOLORW cc = {sizeof(cc)};
        cc.hwndOwner = hwnd_;
        cc.rgbResult = gridColor_;
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (!::ChooseColorW(&cc)) return;
        color = cc.rgbResult;
    }
    gridColor_ = color;
    gridColorCommand_ = commandId;
    SyncMenuChecks();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::ToggleDisplay(int commandId) {
    switch (commandId) {
        case kDispAllColors: showAllColors_ = !showAllColors_; break;
        case kDispUnits: showUnits_ = !showUnits_; break;
        case kDispDoodads: showDoodads_ = !showDoodads_; break;
        case kDispSpUnits: showSpUnits_ = !showSpUnits_; break;
        case kDispZergGround: showZergGround_ = !showZergGround_; break;
        case kDispSpecialStates: showSpecialStates_ = !showSpecialStates_; break;
        case kDispHideMinimap: hideMinimap_ = !hideMinimap_; break;
        case kDispColorCycle: allowColorCycle_ = !allowColorCycle_; break;
        case kDispDeathFx: showDeathFx_ = !showDeathFx_; break;
        case kDispFog: showFog_ = !showFog_; break;
        case kDispLocations: showLocations_ = !showLocations_; break;
        default: return;
    }
    SyncMenuChecks();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::SyncMenuChecks() {
    HMENU bar = ::GetMenu(hwnd_);
    if (!bar) return;

    CheckOnly(bar, kLayerIds, 6, kLayerIds[static_cast<int>(layer_)]);
    CheckOnly(bar, kGridIds, 7, GridPresetOf(gridEnabled_, gridCellX_, gridCellY_));
    CheckOnly(bar, kGridColorIds, 7, gridColorCommand_);

    struct Toggle { int id; bool on; };
    const Toggle toggles[] = {
        {kDispAllColors, showAllColors_}, {kDispUnits, showUnits_},
        {kDispDoodads, showDoodads_}, {kDispSpUnits, showSpUnits_},
        {kDispZergGround, showZergGround_}, {kDispSpecialStates, showSpecialStates_},
        {kDispHideMinimap, hideMinimap_}, {kDispColorCycle, allowColorCycle_},
        {kDispDeathFx, showDeathFx_}, {kDispFog, showFog_},
        {kDispLocations, showLocations_},
    };
    for (const Toggle& t : toggles) {
        if (HMENU menu = FindMenuContaining(bar, t.id)) {
            ::CheckMenuItem(menu, t.id, MF_BYCOMMAND | (t.on ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    ::DrawMenuBar(hwnd_);
}

void MainWindow::DrawGrid(HDC dc, const RECT& client) const {
    if (!gridEnabled_ || !map_.IsOpen() || pxPerTile_ <= 0) return;

    // 方格尺寸按源像素定义（32 = 一格），随缩放换算到屏幕
    const int cellX = gridCellX_ * pxPerTile_ / 32;
    const int cellY = gridCellY_ * pxPerTile_ / 32;
    if (cellX < 2 && cellY < 2) return;

    const HPEN pen = ::CreatePen(PS_DOT, 1, gridColor_);
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const int oldBk = ::SetBkMode(dc, TRANSPARENT);

    if (cellX >= 2) {
        int x = gridOffsetX_ * pxPerTile_ / 32 - scrollX_;
        while (x < client.left) x += cellX;
        for (; x < client.right; x += cellX) {
            ::MoveToEx(dc, x, client.top, nullptr);
            ::LineTo(dc, x, client.bottom);
        }
    }
    if (cellY >= 2) {
        int y = gridOffsetY_ * pxPerTile_ / 32 - scrollY_;
        while (y < client.top) y += cellY;
        for (; y < client.bottom; y += cellY) {
            ::MoveToEx(dc, client.left, y, nullptr);
            ::LineTo(dc, client.right, y);
        }
    }

    ::SetBkMode(dc, oldBk);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(pen);
}

void MainWindow::DrawLocations(HDC dc, const RECT& client) const {
    if (!map_.IsOpen() || pxPerTile_ <= 0) return;

    const HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(0, 220, 90));
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
    const int oldBk = ::SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = ::SetTextColor(dc, RGB(0, 220, 90));

    const auto& items = map_.locations().Items();
    for (std::size_t i = 0; i < items.size(); i++) {
        const Location& loc = items[i];
        RECT r;
        r.left = static_cast<int>(loc.left) * pxPerTile_ - scrollX_;
        r.top = static_cast<int>(loc.top) * pxPerTile_ - scrollY_;
        r.right = (static_cast<int>(loc.right) + 1) * pxPerTile_ - scrollX_;
        r.bottom = (static_cast<int>(loc.bottom) + 1) * pxPerTile_ - scrollY_;
        if (r.right < client.left || r.bottom < client.top ||
            r.left > client.right || r.top > client.bottom) {
            continue;
        }
        ::Rectangle(dc, r.left, r.top, r.right, r.bottom);

        wchar_t label[16];
        ::wsprintfW(label, L"%u", static_cast<unsigned>(i));
        ::TextOutW(dc, r.left + 2, r.top + 1, label, static_cast<int>(wcslen(label)));
    }

    ::SetTextColor(dc, oldColor);
    ::SetBkMode(dc, oldBk);
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(pen);
}

void MainWindow::TileCoordsFromClient(int mx, int my, int& tx, int& ty) const {
    tx = -1;
    ty = -1;
    if (pxPerTile_ <= 0 || !map_.IsOpen()) return;
    tx = (mx + scrollX_) / pxPerTile_;
    ty = (my + scrollY_) / pxPerTile_;
    if (tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) {
        tx = ty = -1;
    }
}

void MainWindow::PaintTileToBuffer(int tx, int ty, u16 value) {
    if (!map_.PaintTile(tx, ty, value) || !dibBits_) return;
    if (!map_.hasTileset()) return;

    u8 block[1024];
    if (!map_.tileset().TileBlock(value, block)) return;

    const Buffer& wpe = map_.tileset().palette();
    if (wpe.size() < 256 * 4) return;
    const int step = 32 / pxPerTile_;
    u8* base = static_cast<u8*>(dibBits_);
    const int stride = image_.width * 4;

    for (int py = 0; py < pxPerTile_; py++) {
        const int sy = py * step;
        u8* row = base + static_cast<std::size_t>(ty * pxPerTile_ + py) * stride
                       + static_cast<std::size_t>(tx * pxPerTile_) * 4;
        const u8* src = block + sy * 32;
        for (int px = 0; px < pxPerTile_; px++) {
            const u8* rgb = wpe.data() + static_cast<std::size_t>(src[px * step]) * 4;
            row[px * 4 + 0] = rgb[2];
            row[px * 4 + 1] = rgb[1];
            row[px * 4 + 2] = rgb[0];
            row[px * 4 + 3] = 0xFF;
        }
    }

    RECT r;
    r.left = tx * pxPerTile_ - scrollX_;
    r.top = ty * pxPerTile_ - scrollY_;
    r.right = r.left + pxPerTile_;
    r.bottom = r.top + pxPerTile_;
    ::InvalidateRect(hwnd_, &r, FALSE);
}

void MainWindow::OnLButtonDown(int mx, int my) {
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    if (tx < 0) return;

    switch (tool_) {
        case Tool::kTerrain:
            BeginEdit();
            PaintTileToBuffer(tx, ty, brushTile_);
            break;
        case Tool::kPicker:
            brushTile_ = map_.TileAt(tx, ty);
            tool_ = Tool::kTerrain;
            break;
        case Tool::kUnit:
            BeginEdit();
            if (map_.PlaceUnit(tx, ty, unitType_, 11, 1500)) {
                ::InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                MessageBeep(MB_ICONWARNING);   // 不可行走，拒绝放置
            }
            break;
        default:
            break;
    }
    cursorTx_ = tx;
    cursorTy_ = ty;
    UpdateTitle();
}

void MainWindow::OnMouseMove(int mx, int my, bool leftDown) {
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    cursorTx_ = tx;
    cursorTy_ = ty;
    if (leftDown && tool_ == Tool::kTerrain && tx >= 0) {
        BeginEdit();                 // 拖出窗口再回来时保持同一笔画
        PaintTileToBuffer(tx, ty, brushTile_);
        UpdateTitle();
    }
}

void MainWindow::OnRButtonDown(int mx, int my) {
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    if (tx < 0) return;
    BeginEdit();
    if (map_.RemoveUnitsAtTile(tx, ty) > 0) {
        CommitEdit(L"删除单位");
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        editOpen_ = false;
    }
}

void MainWindow::OnKeyDown(int vk) {
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrl && vk == 'Z') {
        if (shift) Redo(); else Undo();
        return;
    }
    if (ctrl && vk == 'Y') {
        Redo();
        return;
    }
    switch (vk) {
        case '1': SetLayer(EditLayer::kTerrain); break;
        case '2': SetLayer(EditLayer::kUnits); break;
        case '3': tool_ = Tool::kPicker; break;
        case VK_DELETE:
            if (cursorTx_ >= 0) {
                BeginEdit();
                if (map_.RemoveUnitsAtTile(cursorTx_, cursorTy_) > 0) {
                    CommitEdit(L"删除单位");
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                } else {
                    editOpen_ = false;
                }
            }
            break;
        default:
            return;
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::DrawUnits(HDC dc, const RECT& client) const {
    if (!map_.IsOpen()) return;
    for (const auto& u : map_.units()) {
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        const int sx = tx * pxPerTile_ - scrollX_;
        const int sy = ty * pxPerTile_ - scrollY_;
        if (sx < -pxPerTile_ || sy < -pxPerTile_) continue;
        if (sx > client.right || sy > client.bottom) continue;

        const int size = std::max(3, pxPerTile_ / 2);
        const HBRUSH brush = ::CreateSolidBrush(UnitColor(u.type));
        RECT r;
        r.left = sx + (pxPerTile_ - size) / 2;
        r.top = sy + (pxPerTile_ - size) / 2;
        r.right = r.left + size;
        r.bottom = r.top + size;
        ::FillRect(dc, &r, brush);
        ::DeleteObject(brush);
    }
}

void MainWindow::DrawHud(HDC dc) const {
    wchar_t text[256];
    const wchar_t* toolName = L"地形刷";
    if (tool_ == Tool::kUnit) toolName = L"单位";
    else if (tool_ == Tool::kPicker) toolName = L"吸管";
    else if (tool_ == Tool::kNone) toolName = L"仅查看";

    const wchar_t* layerName = L"地形";
    switch (layer_) {
        case EditLayer::kDoodads: layerName = L"装饰物"; break;
        case EditLayer::kFog: layerName = L"战争迷雾"; break;
        case EditLayer::kLocations: layerName = L"地点"; break;
        case EditLayer::kUnits: layerName = L"单位"; break;
        case EditLayer::kSpUnits: layerName = L"SP单位"; break;
        default: break;
    }

    ::wsprintfW(text, L"层: %s   工具: %s   刷子: 0x%04X   单位: %u   缩放: %dpx/瓦片   撤销: %u / 重做: %u\n"
                      L"左键应用 / 右键删单位 / 1地形 2单位 3吸管 / 滚轮缩放 / Ctrl+Z 撤销 / Ctrl+Y 重做",
                layerName, toolName, static_cast<unsigned>(brushTile_),
                static_cast<unsigned>(unitType_), pxPerTile_,
                static_cast<unsigned>(history_.UndoCount()),
                static_cast<unsigned>(history_.RedoCount()));

    const HGDIOBJ oldFont = ::SelectObject(dc, ::GetStockObject(DEFAULT_GUI_FONT));
    const int oldMode = ::SetBkMode(dc, OPAQUE);
    const COLORREF oldColor = ::SetTextColor(dc, RGB(255, 255, 255));
    const COLORREF oldBk = ::SetBkColor(dc, RGB(32, 32, 32));
    RECT box = {6, 6, 6 + 560, 6 + 44};
    ::DrawTextW(dc, text, -1, &box, DT_LEFT | DT_TOP | DT_NOPREFIX);
    ::SetBkColor(dc, oldBk);
    ::SetTextColor(dc, oldColor);
    ::SetBkMode(dc, oldMode);
    ::SelectObject(dc, oldFont);
}

void MainWindow::Present(HDC dc, const RECT& client) {
    ::FillRect(dc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    if (backBuffer_) {
        const HDC mem = ::CreateCompatibleDC(dc);
        if (mem) {
            const HGDIOBJ old = ::SelectObject(mem, backBuffer_);
            const int w = std::min<int>(client.right - client.left, image_.width - scrollX_);
            const int h = std::min<int>(client.bottom - client.top, image_.height - scrollY_);
            if (w > 0 && h > 0) {
                ::BitBlt(dc, 0, 0, w, h, mem, scrollX_, scrollY_, SRCCOPY);
            }
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }

    DrawGrid(dc, client);
    if (showUnits_) DrawUnits(dc, client);
    if (showLocations_) DrawLocations(dc, client);

    // 光标所在格高亮
    if (cursorTx_ >= 0) {
        const int sx = cursorTx_ * pxPerTile_ - scrollX_;
        const int sy = cursorTy_ * pxPerTile_ - scrollY_;
        RECT r = {sx, sy, sx + pxPerTile_, sy + pxPerTile_};
        ::DrawFocusRect(dc, &r);
    }

    DrawHud(dc);
}

void MainWindow::BeginEdit() {
    if (editOpen_) return;
    editOpen_ = true;
    tilesSnapshot_ = map_.tiles();
    unitsSnapshot_ = map_.units();
}

void MainWindow::CommitEdit(const wchar_t* label) {
    if (!editOpen_) return;
    editOpen_ = false;

    const Buffer& tiles = map_.tiles();
    const bool tilesChanged = tiles != tilesSnapshot_;
    const bool unitsChanged = map_.units().size() != unitsSnapshot_.size() ||
        !std::equal(map_.units().begin(), map_.units().end(), unitsSnapshot_.begin(),
                    [](const UnitRecord& a, const UnitRecord& b) {
                        return a.serial == b.serial && a.x == b.x && a.y == b.y &&
                               a.type == b.type && a.owner == b.owner && a.resource == b.resource;
                    });
    if (!tilesChanged && !unitsChanged) {
        UpdateTitle();
        return;
    }

    EditCommand cmd;
    cmd.label = label;
    cmd.tilesBefore = std::move(tilesSnapshot_);
    cmd.tilesAfter = tiles;
    cmd.unitsBefore = std::move(unitsSnapshot_);
    cmd.unitsAfter = map_.units();
    history_.Push(std::move(cmd));

    tilesSnapshot_.clear();
    unitsSnapshot_.clear();
    UpdateTitle();
}

bool MainWindow::Undo() {
    if (!history_.Undo(map_)) return false;
    RebuildView(pxPerTile_);
    UpdateTitle();
    return true;
}

bool MainWindow::Redo() {
    if (!history_.Redo(map_)) return false;
    RebuildView(pxPerTile_);
    UpdateTitle();
    return true;
}

void MainWindow::OnCommand(int id) {
    switch (id) {
        case kFileNew:
            NewMap();
            break;
        case kFileOpen: {
            if (!MaybeSave()) break;
            std::wstring path;
            if (OpenFileDialog(hwnd_, kMapFilter, path)) OpenMap(path);
            break;
        }
        case kFileClose:
            CloseMap();
            break;
        case kFileSave:
            SaveMap(false);
            break;
        case kFileSaveAs:
            SaveMap(true);
            break;
        case kEditUndo:
            Undo();
            break;
        case kEditRedo:
            Redo();
            break;
        case kEditDelete:
            if (cursorTx_ >= 0) {
                BeginEdit();
                if (map_.RemoveUnitsAtTile(cursorTx_, cursorTy_) > 0) {
                    CommitEdit(L"删除单位");
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                } else {
                    editOpen_ = false;
                }
            }
            break;
        case kEditDeselect:
            cursorTx_ = cursorTy_ = -1;
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kEditCut:
        case kEditCopy:
        case kEditPaste:
        case kEditSelectAll:
            Log(L"[ui] 编辑命令 0x%04X 暂未实现（需要选择/剪贴板模型）", id);
            MessageBeep(MB_ICONWARNING);
            break;
        case kLayerTerrain:
            SetLayer(EditLayer::kTerrain);
            break;
        case kLayerDoodads:
            SetLayer(EditLayer::kDoodads);
            break;
        case kLayerFog:
            SetLayer(EditLayer::kFog);
            break;
        case kLayerLocations:
            SetLayer(EditLayer::kLocations);
            break;
        case kLayerUnits:
            SetLayer(EditLayer::kUnits);
            break;
        case kLayerSpUnits:
            SetLayer(EditLayer::kSpUnits);
            break;
        case kGridTiny:
            ApplyGridPreset(8);
            break;
        case kGridSmall:
            ApplyGridPreset(16);
            break;
        case kGridNormal:
            ApplyGridPreset(32);
            break;
        case kGridLarge:
            ApplyGridPreset(64);
            break;
        case kGridHuge:
            ApplyGridPreset(128);
            break;
        case kGridCustom:
            OpenGridSettings();
            break;
        case kGridOff:
            ApplyGridPreset(0);
            break;
        case kGridColorBlack:
            SetGridColor(RGB(0, 0, 0), id);
            break;
        case kGridColorGray:
            SetGridColor(RGB(128, 128, 128), id);
            break;
        case kGridColorWhite:
            SetGridColor(RGB(255, 255, 255), id);
            break;
        case kGridColorGreen:
            SetGridColor(RGB(0, 200, 0), id);
            break;
        case kGridColorRed:
            SetGridColor(RGB(220, 0, 0), id);
            break;
        case kGridColorBlue:
            SetGridColor(RGB(0, 90, 220), id);
            break;
        case kGridColorOther:
            SetGridColor(gridColor_, id);
            break;
        case kDispAllColors:
        case kDispUnits:
        case kDispDoodads:
        case kDispSpUnits:
        case kDispZergGround:
        case kDispSpecialStates:
        case kDispHideMinimap:
        case kDispColorCycle:
        case kDispDeathFx:
        case kDispFog:
        case kDispLocations:
            ToggleDisplay(id);
            break;
        case kTriggerEditor:
            ShowRecoveredDialog(IDD_DLG_181, hwnd_);
            break;
        case kDebugDialogs:
            ShowRecoveredDialog(IDD_DLG_155, hwnd_);
            break;
        case kFileExit:
            if (MaybeSave()) ::DestroyWindow(hwnd_);
            break;
        case kHelpAbout:
            ::MessageBoxW(hwnd_, L"SCMD Plus 3.0\nWin32 骨架版本", kWindowTitle, MB_OK | MB_ICONINFORMATION);
            break;
        default:
            Log(L"[ui] command %d not implemented", id);
            break;
    }
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* self = reinterpret_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->WndProc(msg, wParam, lParam) : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            OnCommand(LOWORD(wParam));
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            const HDC dc = ::BeginPaint(hwnd_, &ps);
            RECT rc;
            ::GetClientRect(hwnd_, &rc);
            Present(dc, rc);
            ::EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            ::SetCapture(hwnd_);
            OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            ::ReleaseCapture();
            if (tool_ == Tool::kTerrain) CommitEdit(L"绘制地形");
            else if (tool_ == Tool::kUnit) CommitEdit(L"放置单位");
            else editOpen_ = false;
            UpdateTitle();
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                        (wParam & MK_LBUTTON) != 0);
            return 0;
        case WM_RBUTTONDOWN:
            OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_KEYDOWN:
            OnKeyDown(static_cast<int>(wParam));
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int next = NextZoom(pxPerTile_, delta > 0 ? 1 : -1);
            if (next != pxPerTile_ && map_.IsOpen()) RebuildView(next);
            return 0;
        }
        case WM_HSCROLL: {
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            ::GetScrollInfo(hwnd_, SB_HORZ, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINELEFT: pos -= 32; break;
                case SB_LINERIGHT: pos += 32; break;
                case SB_PAGELEFT: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGERIGHT: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            scrollX_ = std::max(0, std::min(pos, static_cast<int>(si.nMax)));
            ::SetScrollPos(hwnd_, SB_HORZ, scrollX_, TRUE);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            ::GetScrollInfo(hwnd_, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= 32; break;
                case SB_LINEDOWN: pos += 32; break;
                case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            scrollY_ = std::max(0, std::min(pos, static_cast<int>(si.nMax)));
            ::SetScrollPos(hwnd_, SB_VERT, scrollY_, TRUE);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE:
            UpdateScrollBars();
            return 0;
        case WM_CLOSE:
            if (MaybeSave()) ::DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            ReleaseBackBuffer();
            ::PostQuitMessage(0);
            return 0;
        default:
            return ::DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

}  // namespace scmd
