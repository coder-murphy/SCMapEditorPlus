#include "ui/main_window.h"

#include <algorithm>
#include <commdlg.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <objbase.h>
#include <shlobj.h>
#include <unordered_map>
#include <utility>
#include <windows.h>
#include <windowsx.h>

#include "common/log.h"
#include "common/string_util.h"
#include "app/app_settings.h"
#include "app/backups.h"
#include "platform/file_dialog.h"
#include "map/map_switches.h"
#include "res/resource.h"
#include "ui/command_ids.h"
#include "ui/dialog_registry.h"
#include "ui/dlg_common.h"
#include "ui/dlg_procs.h"
#include "ui/dialogs.h"

#pragma comment(lib, "msimg32.lib")   // DrawFog 的 AlphaBlend

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
    static const int kSteps[] = {2, 4, 8, 11, 16, 21, 32, 48, 64, 96, 128};
    constexpr int kCount = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));
    int idx = 0;
    for (int i = 0; i < kCount; i++) {
        if (kSteps[i] == current) { idx = i; break; }
    }
    idx += direction;
    if (idx < 0) idx = 0;
    if (idx >= kCount) idx = kCount - 1;
    return kSteps[idx];
}

COLORREF UnitColor(u16 type) {
    if (type >= 176 && type <= 178) return RGB(0, 200, 255);    // 矿物
    if (type == 188) return RGB(0, 255, 130);                   // 气矿
    if (type == 214) return RGB(255, 255, 255);                 // 出生点
    return RGB(255, 150, 0);
}

// “所有颜色”：按单位类型散列出稳定且互不相同的颜色；关闭时沿用分类色
COLORREF UnitColorFor(u16 type, bool allColors) {
    if (!allColors) return UnitColor(type);
    const unsigned h = static_cast<unsigned>(type) * 2654435761u;
    return RGB(64 + (h & 0x7Fu), 64 + ((h >> 8) & 0x7Fu), 64 + ((h >> 16) & 0x7Fu));
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

// 比较两份 MTXM，返回变化瓦片的包围盒；没有任何变化时 any=false
bool DiffTileRect(const Buffer& before, const Buffer& after, int width, int height,
                  int& x0, int& y0, int& x1, int& y1, bool& any) {
    any = false;
    if (width <= 0 || height <= 0) return false;
    const std::size_t need = static_cast<std::size_t>(width) * height * 2;
    if (before.size() < need || after.size() < need) return false;
    x0 = width;
    y0 = height;
    x1 = -1;
    y1 = -1;
    for (int ty = 0; ty < height; ty++) {
        const u8* a = before.data() + static_cast<std::size_t>(ty) * width * 2;
        const u8* b = after.data() + static_cast<std::size_t>(ty) * width * 2;
        if (std::memcmp(a, b, static_cast<std::size_t>(width) * 2) == 0) continue;
        for (int tx = 0; tx < width; tx++) {
            if (a[tx * 2] == b[tx * 2] && a[tx * 2 + 1] == b[tx * 2 + 1]) continue;
            x0 = std::min(x0, tx);
            x1 = std::max(x1, tx);
            y0 = std::min(y0, ty);
            y1 = std::max(y1, ty);
        }
    }
    any = x1 >= x0 && y1 >= y0;
    return true;
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

// 最近地图弹窗使用的一次性命令 ID（不进入菜单资源）
constexpr int kRecentMenuBase = 0x9000;

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
    brushPattern_.SetSingle(brushTile_, L"单格");
    renderer_.SetTileset(&map_.tileset());
    history_.SetLimit(static_cast<std::size_t>(AppSettings::Instance().undoLimit()));

    // 恢复上次会话的视图状态
    {
        const ViewState& view = AppSettings::Instance().view();
        pxPerTile_ = std::clamp(view.zoomPx, 1, 128);
        gridEnabled_ = view.gridEnabled;
        gridCellX_ = std::clamp(view.gridCellX, 1, 256);
        gridCellY_ = std::clamp(view.gridCellY, 1, 256);
        gridColorCommand_ = view.gridColorId;
        gridColor_ = static_cast<COLORREF>(view.gridColorValue);
        layer_ = static_cast<EditLayer>(std::clamp(view.layer, 0, 5));
        showUnits_ = view.showUnits;
        showDoodads_ = view.showDoodads;
        showSpUnits_ = view.showSpUnits;
        showAllColors_ = view.showAllColors;
        showZergGround_ = view.showZergGround;
        showSpecialStates_ = view.showSpecialStates;
        hideMinimap_ = view.hideMinimap;
        allowColorCycle_ = view.allowColorCycle;
        showDeathFx_ = view.showDeathFx;
        showFog_ = view.showFog;
        showLocations_ = view.showLocations;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;          // 地点层双击打开 171
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
        const int kEnableIds[] = {kDispAllColors, kDispUnits, kDispDoodads, kDispSpUnits,
                                  kWindowSpUnits, kEditConvertTerrain,
                                  kDebugEnable, kDebugDataNormal, kDebugDataExact,
                                  kDebugDataFinal, kDebugOverlaps, kDebugCpu,
                                  kDebugClearScreen, kDebugImageSize,
                                  kDebugCompare, kDebugUnitConflict};
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
    ::SetTimer(hwnd_, 1, 60000, nullptr);   // 每分钟检查一次自动备份
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

int MainWindow::FitZoom() const {
    int zoom = pxPerTile_ > 0 ? pxPerTile_ : 8;
    while (zoom > 2 && static_cast<double>(map_.width()) * map_.height() * zoom * zoom >
                           64.0 * 1024.0 * 1024.0) {
        zoom = NextZoom(zoom, -1);
    }
    return zoom;
}

void MainWindow::ApplyZoomPercent(int percent) {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    int px = (kTilePixels * percent + 50) / 100;
    if (px < 1) px = 1;
    // 全图渲染为单个 DIB：限制约 256 MB，避免超大位图分配失败
    const double pixels = static_cast<double>(map_.width()) * map_.height() * px * px;
    if (pixels > 64.0 * 1024.0 * 1024.0) {
        ::MessageBoxW(hwnd_, L"当前地图在该缩放下超出位图内存上限，已保持原比例。",
                      kWindowTitle, MB_ICONWARNING);
        return;
    }
    if (px == pxPerTile_) return;
    if (!RebuildView(px)) MessageBeep(MB_ICONWARNING);
    SyncMenuChecks();
}

bool MainWindow::RebuildView(int pxPerTile) {
    ReleaseBackBuffer();
    pxPerTile_ = pxPerTile;
    if (!map_.IsOpen() || !map_.hasTileset()) return false;
    // 防御超大位图：与地图显示比率菜单共用 256 MB 上限
    const double pixels =
        static_cast<double>(map_.width()) * map_.height() * pxPerTile * pxPerTile;
    if (pixels > 64.0 * 1024.0 * 1024.0) return false;
    const ULONGLONG renderStart = ::GetTickCount64();
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
    lastRenderMs_ = ::GetTickCount64() - renderStart;

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
    ClearSelection();
    AppSettings::Instance().AddRecentMap(path);
    AppSettings::Instance().Save();
    UpdateTitle();
    RebuildView(FitZoom());
    RefreshBrushPalette();
    return true;
}

void MainWindow::NewMap() {
    if (!MaybeSave()) return;
    ShowRecoveredDialog(IDD_DLG_169, hwnd_);
    if (LastDialogResult() != IDOK) return;
    const MapSizeParams& size = LastMapSize();
    if (!map_.CreateNew(size.width, size.height, 0, tilesetDir_)) return;
    mapPath_.clear();
    fileName_.clear();
    history_.Clear();
    editOpen_ = false;
    ClearSelection();
    UpdateTitle();
    RebuildView(FitZoom());
    RefreshBrushPalette();
}

void MainWindow::CloseMap() {
    if (!MaybeSave()) return;
    map_.Close();
    mapPath_.clear();
    fileName_.clear();
    history_.Clear();
    editOpen_ = false;
    ClearSelection();
    ReleaseBackBuffer();
    UpdateTitle();
    RefreshBrushPalette();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

bool MainWindow::SaveMap(bool saveAs) {
    if (!map_.IsOpen()) return false;
    std::wstring path = mapPath_;
    if (saveAs || path.empty()) {
        if (!SaveFileDialog(hwnd_, kMapFilter, L"scx", path)) return false;
    }
    // 覆盖已有文件前自动备份（可在 155 关闭）
    if (!saveAs && !path.empty() && map_.IsOpen()) {
        const AppSettings& settings = AppSettings::Instance();
        if (settings.backupEnabled()) MakeBackup(path, settings.backupCount());
    }
    if (!map_.Save(path)) {
        ::MessageBoxW(hwnd_, L"保存地图失败。", kWindowTitle, MB_ICONWARNING);
        return false;
    }
    mapPath_ = path;
    fileName_ = FileNameOf(path);
    lastBackupTick_ = ::GetTickCount64();
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

void MainWindow::ResizeMapDialog() {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    ShowRecoveredDialog(IDD_DLG_175, hwnd_);
    if (LastDialogResult() != IDOK) return;

    const ResizeParams& p = LastResizeParams();
    if (!map_.Resize(p.width, p.height, p.offsetX, p.offsetY, p.smoothEdges)) {
        ::MessageBoxW(hwnd_, L"重设地图大小失败。", kWindowTitle, MB_ICONWARNING);
        return;
    }
    history_.Clear();     // 旧快照与新尺寸不兼容
    editOpen_ = false;
    ClearSelection();
    RebuildView(FitZoom());
    UpdateTitle();
}

int MainWindow::UnitIndexAtTile(int tx, int ty) const {
    const auto& units = map_.units();
    for (std::size_t i = 0; i < units.size(); i++) {
        if (static_cast<int>(units[i].x) / 32 == tx &&
            static_cast<int>(units[i].y) / 32 == ty) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MainWindow::EditUnitProps() {
    if (!map_.IsOpen() || cursorTx_ < 0) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const int index = UnitIndexAtTile(cursorTx_, cursorTy_);
    if (index < 0) {
        ::MessageBoxW(hwnd_, L"光标所在格没有单位。", kWindowTitle, MB_ICONINFORMATION);
        return;
    }

    const UnitRecord& unit = map_.units()[index];
    UnitProps seed;
    seed.hp = unit.hpPercent;
    seed.shield = unit.shieldPercent;
    seed.energy = unit.energyPercent;
    seed.resource = static_cast<int>(unit.resource);
    SeedUnitProps(seed);

    ShowRecoveredDialog(IDD_DLG_180, hwnd_);
    if (LastDialogResult() != IDOK) return;

    const UnitProps& props = LastUnitProps();
    BeginEdit();
    UnitRecord& target = map_.mutableUnits()[index];
    target.hpPercent = static_cast<u8>(props.hp);
    target.shieldPercent = static_cast<u8>(props.shield);
    target.energyPercent = static_cast<u8>(props.energy);
    target.resource = static_cast<u32>(props.resource);
    map_.MarkDirty();
    CommitEdit(L"修改单位属性");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::EditLocation(int index) {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    auto& items = map_.mutableLocations().MutableItems();
    if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return;
    const Location& loc = items[index];

    LocationRect seed;
    seed.left = loc.left;
    seed.top = loc.top;
    seed.right = loc.right;
    seed.bottom = loc.bottom;
    seed.elevationMask = loc.flags & 0x3Fu;
    seed.stringId = loc.stringId;
    seed.name = Utf8ToWide(map_.strings().At(loc.stringId));
    SeedLocation(seed);

    BeginEdit();
    ShowRecoveredDialog(IDD_DLG_171, hwnd_);
    if (LastDialogResult() == IDOK) {
        const LocationRect& r = LastLocation();
        Location& target = map_.mutableLocations().MutableItems()[index];
        target.left = static_cast<u16>(r.left);
        target.top = static_cast<u16>(r.top);
        target.right = static_cast<u16>(r.right);
        target.bottom = static_cast<u16>(r.bottom);
        target.flags = static_cast<u16>((target.flags & ~0x3Fu) |
                                        (r.elevationMask & 0x3Fu));

        // 名称写回 STR：0 号下标保留为空，非空名称优先复用现有下标
        int sid = r.stringId;
        const std::string newName = WideToUtf8(r.name);
        if (!newName.empty()) {
            if (sid <= 0) {
                sid = static_cast<int>(std::max<std::size_t>(1, map_.strings().Count()));
            } else if (static_cast<std::size_t>(sid) >= map_.strings().Count()) {
                sid = static_cast<int>(map_.strings().Count());
            }
            map_.mutableStrings().Set(static_cast<std::size_t>(sid), newName);
            target.stringId = static_cast<u16>(sid);
        } else if (sid > 0 && static_cast<std::size_t>(sid) < map_.strings().Count()) {
            map_.mutableStrings().Set(static_cast<std::size_t>(sid), std::string());
            target.stringId = static_cast<u16>(sid);
        } else {
            target.stringId = static_cast<u16>(sid);
        }
        map_.MarkDirty();
    }
    CommitEdit(L"编辑地点");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

int MainWindow::SpriteIndexAtTile(int tx, int ty) const {
    if (!map_.IsOpen()) return -1;
    const auto& items = map_.sprites().Items();
    int best = -1;
    int bestDist = 0x7FFFFFFF;
    for (int i = 0; i < static_cast<int>(items.size()); i++) {
        const int dx = std::abs(static_cast<int>(items[i].x) - (tx * kTilePixels + kTilePixels / 2));
        const int dy = std::abs(static_cast<int>(items[i].y) - (ty * kTilePixels + kTilePixels / 2));
        if (dx > 24 || dy > 24) continue;
        if (dx + dy < bestDist) {
            bestDist = dx + dy;
            best = i;
        }
    }
    return best;
}

void MainWindow::OpenSpUnitEditor(int selected) {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (map_.sprites().Count() == 0) {
        ::MessageBoxW(hwnd_, L"地图没有 SP 单位（THG2）。", kWindowTitle, MB_ICONINFORMATION);
        return;
    }
    std::vector<SpriteRecord> units = map_.sprites().Items();
    if (selected < 0) selected = 0;
    SeedSpUnits(units, selected);

    BeginEdit();
    ShowRecoveredDialog(IDD_DLG_164, hwnd_);
    if (LastDialogResult() == IDOK) {
        const auto& edited = LastSpUnits();
        bool changed = edited.size() != map_.sprites().Count();
        if (!changed) {
            for (std::size_t i = 0; i < edited.size(); i++) {
                const SpriteRecord& a = edited[i];
                const SpriteRecord& b = map_.sprites().Items()[i];
                if (a.type != b.type || a.x != b.x || a.y != b.y || a.owner != b.owner ||
                    a.unused != b.unused || a.flags != b.flags) {
                    changed = true;
                    break;
                }
            }
        }
        if (changed) {
            map_.mutableSprites().MutableItems() = edited;
            map_.MarkDirty();
        }
    }
    CommitEdit(L"SP单位属性");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

int MainWindow::LocationIndexAtTile(int tx, int ty) const {
    const auto& items = map_.locations().Items();
    for (int i = static_cast<int>(items.size()) - 1; i >= 0; i--) {
        const Location& loc = items[static_cast<std::size_t>(i)];
        if (tx >= static_cast<int>(loc.left) && tx <= static_cast<int>(loc.right) &&
            ty >= static_cast<int>(loc.top) && ty <= static_cast<int>(loc.bottom)) {
            return i;
        }
    }
    return -1;
}

void MainWindow::EditMapText(int stringIndex, bool richText) {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    const std::wstring seed = Utf8ToWide(map_.strings().At(static_cast<std::size_t>(stringIndex)));

    BeginEdit();
    bool applied = false;
    if (richText) {
        SeedRichText(seed);
        ShowRecoveredDialog(IDD_DLG_187, hwnd_);
        if (LastDialogResult() == IDOK) {
            map_.mutableStrings().Set(static_cast<std::size_t>(stringIndex),
                                       WideToUtf8(LastRichText()));
            applied = true;
        }
    } else {
        SeedEditedString(seed);
        ShowRecoveredDialog(IDD_DLG_189, hwnd_);
        if (LastDialogResult() == IDOK) {
            map_.mutableStrings().Set(static_cast<std::size_t>(stringIndex),
                                       WideToUtf8(LastEditedString()));
            applied = true;
        }
    }
    if (applied) map_.MarkDirty();
    CommitEdit(L"编辑字符串");
    UpdateTitle();
}

void MainWindow::GotoStartPosition() {
    if (!map_.IsOpen()) return;
    for (const auto& u : map_.units()) {
        if (u.type != 214) continue;      // 出生点
        RECT rc;
        ::GetClientRect(hwnd_, &rc);
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        const int viewW = static_cast<int>(rc.right - rc.left);
        const int viewH = static_cast<int>(rc.bottom - rc.top);
        scrollX_ = std::max(0, tx * pxPerTile_ - viewW / 2);
        scrollY_ = std::max(0, ty * pxPerTile_ - viewH / 2);
        UpdateScrollBars();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    ::MessageBoxW(hwnd_, L"地图中没有出生点。", kWindowTitle, MB_ICONINFORMATION);
}

void MainWindow::SaveMapImage() {
    if (!map_.IsOpen() || !map_.hasTileset()) {
        ::MessageBoxW(hwnd_, L"地图未打开或地形集未载入，无法导出图像。", kWindowTitle,
                      MB_ICONINFORMATION);
        return;
    }
    std::wstring initial;
    if (!mapPath_.empty()) {
        initial = mapPath_;
        const std::size_t dot = initial.find_last_of(L'.');
        const std::size_t slash = initial.find_last_of(L"\\/");
        if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
            initial = initial.substr(0, dot);
        }
        initial += L".bmp";
    } else {
        initial = L"map.bmp";
    }
    std::wstring path;
    if (!SaveFileDialog(hwnd_, L"BMP 图像 (*.bmp)\0*.bmp\0所有文件 (*.*)\0*.*\0\0", L"bmp",
                        path, initial.c_str())) {
        return;
    }
    TerrainRenderer renderer;
    renderer.SetTileset(&map_.tileset());
    TerrainImage image;
    if (!renderer.Render(map_.tiles(), map_.width(), map_.height(), 8, image) ||
        !TerrainRenderer::WriteBmp24(path, image)) {
        ::MessageBoxW(hwnd_, L"导出地图图像失败。", kWindowTitle, MB_ICONERROR);
        return;
    }
    Log(L"[ui] map image saved: %s (%dx%d)", path.c_str(), image.width, image.height);
}

void MainWindow::SetDefaultMapEditor() {
    if (::MessageBoxW(hwnd_, L"将 .scx / .scm 文件关联到 SCMD Plus？\n（写入当前用户注册表）",
                      kWindowTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    wchar_t exe[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring command = std::wstring(L"\"") + exe + L"\" \"%1\"";

    auto setValue = [](const std::wstring& sub, const wchar_t* name, const std::wstring& value) {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                              &key, nullptr) != ERROR_SUCCESS) {
            return false;
        }
        const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
        const bool ok = ::RegSetValueExW(key, name, 0, REG_SZ,
                                         reinterpret_cast<const BYTE*>(value.c_str()),
                                         bytes) == ERROR_SUCCESS;
        ::RegCloseKey(key);
        return ok;
    };

    bool ok = true;
    ok = setValue(L"Software\\Classes\\.scx", nullptr, L"SCMDPlus.Map") && ok;
    ok = setValue(L"Software\\Classes\\.scm", nullptr, L"SCMDPlus.Map") && ok;
    ok = setValue(L"Software\\Classes\\SCMDPlus.Map", nullptr, L"SCMD Plus 地图") && ok;
    ok = setValue(L"Software\\Classes\\SCMDPlus.Map\\shell\\open\\command", nullptr, command) && ok;
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    ::MessageBoxW(hwnd_, ok ? L"已设置为默认地图编辑器。" : L"写入注册表失败。", kWindowTitle,
                  ok ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void MainWindow::CreateStartMenuShortcut() {
    wchar_t exe[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t appData[MAX_PATH] = L"";
    if (::GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) return;
    const std::wstring linkPath = std::wstring(appData) +
                                  L"\\Microsoft\\Windows\\Start Menu\\Programs\\SCMD Plus 3.0.lnk";

    wchar_t workDir[MAX_PATH] = L"";
    ::lstrcpynW(workDir, exe, MAX_PATH);
    if (wchar_t* slash = std::wcsrchr(workDir, L'\\')) *slash = L'\0';

    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool saved = false;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        link->SetPath(exe);
        link->SetWorkingDirectory(workDir);
        link->SetDescription(L"SCMD Plus 3.0 地图编辑器");
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            saved = SUCCEEDED(file->Save(linkPath.c_str(), TRUE));
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(hr)) ::CoUninitialize();
    ::MessageBoxW(hwnd_, saved ? L"开始菜单快捷方式已创建。" : L"创建快捷方式失败。", kWindowTitle,
                  saved ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void MainWindow::ShowBrushPalette() {
    if (!map_.IsOpen() || !map_.hasTileset()) {
        Log(L"[brush] 需要先打开地图并载入地形集");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    brushPalette_.Show(hInstance_, hwnd_, &map_.tileset(), brushTile_);
}

void MainWindow::SetUnitPlacement(u16 type, u8 owner, u32 resource) {
    unitType_ = type;
    unitOwner_ = owner;
    unitResource_ = resource;
}

void MainWindow::OpenUnitPlacement() {
    UnitPlacementParams seed;
    seed.type = static_cast<int>(unitType_);
    seed.owner = static_cast<int>(unitOwner_);
    seed.resource = static_cast<int>(unitResource_);
    SeedUnitPlacement(seed);

    ShowRecoveredDialog(IDD_UNIT_PLACE, hwnd_);
    if (LastDialogResult() != IDOK) return;

    const UnitPlacementParams& p = LastUnitPlacement();
    unitType_ = static_cast<u16>(p.type);
    unitOwner_ = static_cast<u8>(p.owner);
    unitResource_ = static_cast<u32>(p.resource);
    if (layer_ != EditLayer::kUnits) SetLayer(EditLayer::kUnits);
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::OpenPlayerRoster() {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    BeginEdit();
    SeedPlayerRoster(map_.players());
    SetDialogDocument(&map_);
    ShowRecoveredDialog(IDD_PLAYER_ROSTER, hwnd_);
    SetDialogDocument(nullptr);

    if (LastDialogResult() == IDOK) {
        const PlayerRoster& edited = LastPlayerRoster();
        if (edited.Snapshot() != map_.players().Snapshot()) {
            map_.mutablePlayers() = edited;
            map_.MarkDirty();
        }
    }

    CommitEdit(L"玩家/势力设定");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OpenRestrictions() {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    BeginEdit();
    SeedRestrictions(map_.restrictions());
    ShowRecoveredDialog(IDD_RESTRICTIONS, hwnd_);
    if (LastDialogResult() == IDOK) {
        const UpgradeTechRestrictions& edited = LastRestrictions();
        if (edited.Snapshot() != map_.restrictions().Snapshot()) {
            map_.mutableRestrictions() = edited;
            map_.MarkDirty();
        }
    }

    CommitEdit(L"升级/科技限制");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::TogglePlacementRule(int commandId) {
    switch (commandId) {
        case kPlaceBuildNormal:
            buildNormalRules_ = true;
            placeBuildingsAnywhere_ = false;
            break;
        case kPlaceBuildingAnywhere:
            placeBuildingsAnywhere_ = true;
            buildNormalRules_ = false;
            break;
        case kPlaceUnitAnywhere: placeUnitsAnywhere_ = !placeUnitsAnywhere_; break;
        case kPlaceUnitGrid: unitGridDisplay_ = !unitGridDisplay_; break;
        case kPlaceGroundOverlap: allowGroundOverlap_ = !allowGroundOverlap_; break;
        case kPlaceAirOverlap: allowAirOverlap_ = !allowAirOverlap_; break;
        case kPlaceAutoAddon: autoConnectAddon_ = !autoConnectAddon_; break;
        case kPlaceMineralDistance: mineralBaseDistance_ = !mineralBaseDistance_; break;
        case kClipboardDoodads: clipboardDoodads_ = !clipboardDoodads_; break;
        default:
            return;
    }
    SyncMenuChecks();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

bool MainWindow::PlaceUnitAtTile(int tx, int ty) {
    if (!map_.IsOpen()) return false;

    BeginEdit();
    const bool force = placeUnitsAnywhere_ || placeBuildingsAnywhere_;
    u32 resource = unitResource_;
    if (resource == 0) {
        if (unitType_ >= 176 && unitType_ <= 178) {
            resource = static_cast<u32>(AppSettings::Instance().defaultMineral());
        } else if (unitType_ == 188) {
            resource = static_cast<u32>(AppSettings::Instance().defaultGas());
        }
    }
    if (!map_.PlaceUnit(tx, ty, unitType_, unitOwner_, resource, force)) {
        editOpen_ = false;
        return false;
    }
    CommitEdit(L"放置单位");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void MainWindow::BeginSelection(int tx, int ty) {
    if (!map_.IsOpen()) return;
    if (tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) return;
    selecting_ = true;
    hasSelection_ = true;
    selAnchorX_ = selX0_ = selX1_ = tx;
    selAnchorY_ = selY0_ = selY1_ = ty;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::UpdateSelection(int tx, int ty) {
    if (!selecting_ || !map_.IsOpen()) return;
    tx = std::clamp(tx, 0, map_.width() - 1);
    ty = std::clamp(ty, 0, map_.height() - 1);
    selX0_ = std::min(selAnchorX_, tx);
    selX1_ = std::max(selAnchorX_, tx);
    selY0_ = std::min(selAnchorY_, ty);
    selY1_ = std::max(selAnchorY_, ty);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SelectRect(int x0, int y0, int x1, int y1) {
    if (!map_.IsOpen() || map_.width() <= 0 || map_.height() <= 0) {
        hasSelection_ = false;
        selecting_ = false;
        return;
    }
    x0 = std::clamp(x0, 0, map_.width() - 1);
    x1 = std::clamp(x1, 0, map_.width() - 1);
    y0 = std::clamp(y0, 0, map_.height() - 1);
    y1 = std::clamp(y1, 0, map_.height() - 1);
    selX0_ = std::min(x0, x1);
    selX1_ = std::max(x0, x1);
    selY0_ = std::min(y0, y1);
    selY1_ = std::max(y0, y1);
    hasSelection_ = true;
    selecting_ = false;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SelectAllTiles() {
    if (!map_.IsOpen()) return;
    SelectRect(0, 0, map_.width() - 1, map_.height() - 1);
}

void MainWindow::ClearSelection() {
    hasSelection_ = false;
    selecting_ = false;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::DrawSelection(HDC dc, const RECT&) const {
    if (!hasSelection_ || !map_.IsOpen() || pxPerTile_ <= 0) return;
    RECT r;
    r.left = selX0_ * pxPerTile_ - scrollX_;
    r.top = selY0_ * pxPerTile_ - scrollY_;
    r.right = (selX1_ + 1) * pxPerTile_ - scrollX_;
    r.bottom = (selY1_ + 1) * pxPerTile_ - scrollY_;
    const HBRUSH brush = ::CreateSolidBrush(RGB(255, 255, 0));
    ::FrameRect(dc, &r, brush);
    ::DeleteObject(brush);
}

void MainWindow::CopySelection(bool cut) {
    if (!map_.IsOpen() || !hasSelection_) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const int w = selX1_ - selX0_ + 1;
    const int h = selY1_ - selY0_ + 1;
    clipboard_.width = w;
    clipboard_.height = h;
    clipboard_.tiles.assign(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            clipboard_.tiles[static_cast<std::size_t>(y) * w + x] =
                map_.TileAt(selX0_ + x, selY0_ + y);
        }
    }

    clipboard_.units.clear();
    for (const UnitRecord& u : map_.units()) {
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        if (tx < selX0_ || tx > selX1_ || ty < selY0_ || ty > selY1_) continue;
        UnitRecord relative = u;
        relative.x = static_cast<u16>(u.x - selX0_ * 32);
        relative.y = static_cast<u16>(u.y - selY0_ * 32);
        clipboard_.units.push_back(relative);
    }

    clipboard_.doodads.clear();
    if (clipboardDoodads_) {
        for (const Doodad& d : map_.doodads().Items()) {
            const int tx = static_cast<int>(d.x) / 32;
            const int ty = static_cast<int>(d.y) / 32;
            if (tx < selX0_ || tx > selX1_ || ty < selY0_ || ty > selY1_) continue;
            Doodad relative = d;
            relative.x = static_cast<u16>(d.x - selX0_ * 32);
            relative.y = static_cast<u16>(d.y - selY0_ * 32);
            clipboard_.doodads.push_back(relative);
        }
    }

    if (cut) {
        BeginEdit();
        map_.PaintRect(selX0_, selY0_, selX1_, selY1_, 0);
        auto& units = map_.mutableUnits();
        units.erase(std::remove_if(units.begin(), units.end(),
                                   [this](const UnitRecord& u) {
                                       const int tx = static_cast<int>(u.x) / 32;
                                       const int ty = static_cast<int>(u.y) / 32;
                                       return tx >= selX0_ && tx <= selX1_ &&
                                              ty >= selY0_ && ty <= selY1_;
                                   }),
                    units.end());
        if (clipboardDoodads_) {
            map_.mutableDoodads().RemoveInTileRect(selX0_, selY0_, selX1_, selY1_);
        }
        map_.MarkDirty();
        CommitEdit(L"剪切选区");
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    Log(L"[clip] copy %dx%d units=%u cut=%d", w, h,
        static_cast<unsigned>(clipboard_.units.size()), cut ? 1 : 0);
    UpdateTitle();
}

void MainWindow::PasteClipboard() {
    if (!map_.IsOpen() || clipboard_.Empty()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    int tx = 0;
    int ty = 0;
    if (cursorTx_ >= 0) {
        tx = cursorTx_;
        ty = cursorTy_;
    } else if (hasSelection_) {
        tx = selX0_;
        ty = selY0_;
    } else {
        tx = (map_.width() - clipboard_.width) / 2;
        ty = (map_.height() - clipboard_.height) / 2;
    }
    if (!PasteClipboardAt(tx, ty)) MessageBeep(MB_ICONWARNING);
}

bool MainWindow::PasteClipboardAt(int tx, int ty) {
    if (!map_.IsOpen() || clipboard_.Empty()) return false;
    const int w = std::min(clipboard_.width, map_.width());
    const int h = std::min(clipboard_.height, map_.height());
    if (w <= 0 || h <= 0) return false;
    tx = std::clamp(tx, 0, map_.width() - w);
    ty = std::clamp(ty, 0, map_.height() - h);

    BeginEdit();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            map_.PaintTile(tx + x, ty + y, clipboard_.tiles[static_cast<std::size_t>(y) * clipboard_.width + x]);
        }
    }

    auto& units = map_.mutableUnits();
    units.erase(std::remove_if(units.begin(), units.end(),
                               [tx, ty, w, h](const UnitRecord& u) {
                                   const int ux = static_cast<int>(u.x) / 32;
                                   const int uy = static_cast<int>(u.y) / 32;
                                   return ux >= tx && ux < tx + w && uy >= ty && uy < ty + h;
                               }),
                units.end());

    u32 nextSerial = 1;
    for (const UnitRecord& u : units) {
        if (u.serial >= nextSerial) nextSerial = u.serial + 1;
    }
    for (const UnitRecord& src : clipboard_.units) {
        if (static_cast<int>(src.x) / 32 >= w || static_cast<int>(src.y) / 32 >= h) continue;
        const int nx = static_cast<int>(src.x) + tx * 32;
        const int ny = static_cast<int>(src.y) + ty * 32;
        if (nx < 0 || ny < 0 || nx >= map_.width() * 32 || ny >= map_.height() * 32) continue;
        UnitRecord r = src;
        r.serial = nextSerial++;
        r.x = static_cast<u16>(nx);
        r.y = static_cast<u16>(ny);
        units.push_back(r);
    }

    if (!clipboard_.doodads.empty()) {
        map_.mutableDoodads().RemoveInTileRect(tx, ty, tx + w - 1, ty + h - 1);
        for (const Doodad& src : clipboard_.doodads) {
            const int nx = static_cast<int>(src.x) + tx * 32;
            const int ny = static_cast<int>(src.y) + ty * 32;
            if (nx < 0 || ny < 0 || nx >= map_.width() * 32 || ny >= map_.height() * 32) continue;
            Doodad d = src;
            d.x = static_cast<u16>(nx);
            d.y = static_cast<u16>(ny);
            map_.mutableDoodads().MutableItems().push_back(d);
        }
    }

    map_.MarkDirty();
    CommitEdit(L"粘贴");
    SelectRect(tx, ty, tx + w - 1, ty + h - 1);
    Log(L"[clip] paste at %d,%d size=%dx%d units=%u", tx, ty, w, h,
        static_cast<unsigned>(clipboard_.units.size()));
    UpdateTitle();
    return true;
}

int MainWindow::OverlapSelectedUnitsAt(int tx, int ty) {
    if (!map_.IsOpen() || !hasSelection_) return 0;
    if (tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) return 0;

    BeginEdit();
    int moved = 0;
    for (UnitRecord& u : map_.mutableUnits()) {
        const int ux = static_cast<int>(u.x) / 32;
        const int uy = static_cast<int>(u.y) / 32;
        if (ux < selX0_ || ux > selX1_ || uy < selY0_ || uy > selY1_) continue;
        u.x = static_cast<u16>(tx * 32);
        if (u.type == 188) {
            u.y = static_cast<u16>(ty * 32);
        } else {
            u.y = static_cast<u16>(ty * 32 + 16);
        }
        moved++;
    }

    if (moved > 0) {
        map_.MarkDirty();
        CommitEdit(L"重叠所选单位");
        UpdateTitle();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        editOpen_ = false;
    }
    return moved;
}

int MainWindow::DeleteInvalidUnits() {
    if (!map_.IsOpen()) return 0;

    BeginEdit();
    const int removed = map_.RemoveInvalidUnits();
    if (removed > 0) {
        CommitEdit(L"删除非法单位");
        UpdateTitle();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        editOpen_ = false;
    }
    return removed;
}

void MainWindow::SetBrushTile(u16 value) {
    brushTile_ = value;
    brushPattern_.SetSingle(value, L"单格");
    SetLayer(EditLayer::kTerrain);   // 选中刷子即切回地形工具
}

void MainWindow::RefreshBrushPalette() {
    if (!brushPalette_.IsOpen()) return;
    brushPalette_.Refresh(map_.hasTileset() ? &map_.tileset() : nullptr, brushTile_);
}

void MainWindow::OpenCustomBrush() {
    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    TerrainBrush current;
    if (hasSelection_) {
        const int w = selX1_ - selX0_ + 1;
        const int h = selY1_ - selY0_ + 1;
        std::vector<u16> tiles;
        tiles.reserve(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                tiles.push_back(map_.TileAt(selX0_ + x, selY0_ + y));
            }
        }
        current.SetFromTiles(w, h, std::move(tiles), L"当前选择");
    } else {
        current.SetSingle(brushTile_, L"当前刷子");
    }

    SeedCustomBrushes(customBrushes_, current, customBrushIndex_);
    ShowRecoveredDialog(IDD_DLG_182, hwnd_);
    customBrushes_ = LastCustomBrushes();
    if (LastCustomBrushAccepted()) {
        SetBrushPattern(LastCustomBrush());
        customBrushIndex_ = -1;
        SetLayer(EditLayer::kTerrain);
        UpdateTitle();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::SetBrushPattern(const TerrainBrush& brush) {
    brushPattern_ = brush;
    if (brushPattern_.Empty()) brushPattern_.SetSingle(brushTile_, L"单格");
    brushTile_ = brushPattern_.At(brushPattern_.width / 2, brushPattern_.height / 2);
    if (brushPalette_.IsOpen()) brushPalette_.SetSelected(brushTile_);
}

bool MainWindow::PaintBrushAt(int tx, int ty) {
    if (!map_.IsOpen() || tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) {
        return false;
    }
    BeginEdit();
    StampBrushAt(tx, ty);
    CommitEdit(L"绘制地形");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void MainWindow::StampBrushAt(int tx, int ty) {
    if (!map_.IsOpen()) return;
    if (brushPattern_.Empty()) {
        PaintTileToBuffer(tx, ty, brushTile_);
        return;
    }

    const int ox = tx - brushPattern_.width / 2;
    const int oy = ty - brushPattern_.height / 2;
    for (int y = 0; y < brushPattern_.height; y++) {
        for (int x = 0; x < brushPattern_.width; x++) {
            PaintTileToBuffer(ox + x, oy + y, brushPattern_.At(x, y));
        }
    }
}

void MainWindow::SetLayer(EditLayer layer) {
    layer_ = layer;
    switch (layer) {
        case EditLayer::kTerrain:
            tool_ = Tool::kTerrain;
            break;
        case EditLayer::kUnits:
            tool_ = Tool::kUnit;
            break;
        case EditLayer::kSpUnits:
            tool_ = Tool::kNone;   // SP 单位尚无类型选择面板，仅显示
            showSpUnits_ = true;
            break;
        default:
            tool_ = Tool::kNone;   // 装饰物/迷雾/地点：只显示，不放置
            break;
    }
    if (layer == EditLayer::kLocations) showLocations_ = true;
    else if (layer == EditLayer::kDoodads) showDoodads_ = true;
    else if (layer == EditLayer::kFog) showFog_ = true;
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
        case kDispFog:
        case kLayerFogToggle: showFog_ = !showFog_; break;
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

    static const int kZoomIds[] = {kZoom25, kZoom33, kZoom50, kZoom66, kZoom100,
                                   kZoom150, kZoom200, kZoom300, kZoom400};
    static const int kZoomPercents[] = {25, 33, 50, 66, 100, 150, 200, 300, 400};
    int checkedZoom = 0;
    for (int i = 0; i < 9; i++) {
        if ((kTilePixels * kZoomPercents[i] + 50) / 100 == pxPerTile_) {
            checkedZoom = kZoomIds[i];
            break;
        }
    }
    CheckOnly(bar, kZoomIds, 9, checkedZoom);

    const int kDebugDataIds[] = {kDebugDataNormal, kDebugDataExact, kDebugDataFinal};
    CheckOnly(bar, kDebugDataIds, 3,
              debugMode_ ? kDebugDataIds[debugDataMode_ % 3] : 0);

    struct Toggle { int id; bool on; };
    const Toggle toggles[] = {
        {kDispAllColors, showAllColors_}, {kDispUnits, showUnits_},
        {kDispDoodads, showDoodads_}, {kDispSpUnits, showSpUnits_},
        {kDispZergGround, showZergGround_}, {kDispSpecialStates, showSpecialStates_},
        {kDispHideMinimap, hideMinimap_}, {kDispColorCycle, allowColorCycle_},
        {kDispDeathFx, showDeathFx_}, {kDispFog, showFog_}, {kLayerFogToggle, showFog_},
        {kDispLocations, showLocations_},
        {kDebugEnable, debugMode_}, {kDebugOverlaps, showOverlapMarkers_},
        {kDebugCpu, showCpuTiming_}, {kDebugImageSize, showDebugImageSize_},
        {kDebugCompare, showCompareData_}, {kDebugUnitConflict, showUnitConflict_},
    };
    for (const Toggle& t : toggles) {
        if (HMENU menu = FindMenuContaining(bar, t.id)) {
            ::CheckMenuItem(menu, t.id, MF_BYCOMMAND | (t.on ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    const Toggle placementToggles[] = {
        {kPlaceBuildNormal, buildNormalRules_},
        {kPlaceUnitGrid, unitGridDisplay_},
        {kPlaceGroundOverlap, allowGroundOverlap_},
        {kPlaceAirOverlap, allowAirOverlap_},
        {kPlaceUnitAnywhere, placeUnitsAnywhere_},
        {kPlaceBuildingAnywhere, placeBuildingsAnywhere_},
        {kPlaceAutoAddon, autoConnectAddon_},
        {kPlaceMineralDistance, mineralBaseDistance_},
        {kClipboardDoodads, clipboardDoodads_},
    };
    for (const Toggle& t : placementToggles) {
        if (HMENU menu = FindMenuContaining(bar, t.id)) {
            ::CheckMenuItem(menu, t.id, MF_BYCOMMAND | (t.on ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    ::DrawMenuBar(hwnd_);
}

void MainWindow::DrawGrid(HDC dc, const RECT& client) const {
    const bool unitGrid = unitGridDisplay_ && layer_ == EditLayer::kUnits;
    if ((!gridEnabled_ && !unitGrid) || !map_.IsOpen() || pxPerTile_ <= 0) return;

    // 方格尺寸按源像素定义（32 = 一格），随缩放换算到屏幕；
    // 单位层“按方格显示”在网格关闭时使用 1 格（32 px）间距
    const int srcCellX = gridEnabled_ ? gridCellX_ : 32;
    const int srcCellY = gridEnabled_ ? gridCellY_ : 32;
    const int cellX = srcCellX * pxPerTile_ / 32;
    const int cellY = srcCellY * pxPerTile_ / 32;
    if (cellX < 2 && cellY < 2) return;

    const HPEN pen = ::CreatePen(PS_DOT, 1, gridColor_);
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const int oldBk = ::SetBkMode(dc, TRANSPARENT);
    const int offX = gridEnabled_ ? gridOffsetX_ : 0;
    const int offY = gridEnabled_ ? gridOffsetY_ : 0;

    if (cellX >= 2) {
        int x = offX * pxPerTile_ / 32 - scrollX_;
        while (x < client.left) x += cellX;
        for (; x < client.right; x += cellX) {
            ::MoveToEx(dc, x, client.top, nullptr);
            ::LineTo(dc, x, client.bottom);
        }
    }
    if (cellY >= 2) {
        int y = offY * pxPerTile_ / 32 - scrollY_;
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

void MainWindow::DrawDoodads(HDC dc, const RECT& client) const {
    if (!map_.IsOpen() || pxPerTile_ <= 0) return;
    const HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(255, 80, 255));
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));

    for (const Doodad& d : map_.doodads().Items()) {
        const int sx = static_cast<int>(d.x) * pxPerTile_ / kTilePixels - scrollX_;
        const int sy = static_cast<int>(d.y) * pxPerTile_ / kTilePixels - scrollY_;
        const int size = std::max(3, pxPerTile_ / 2);
        RECT r = {sx - size / 2, sy - size / 2, sx - size / 2 + size, sy - size / 2 + size};
        if (r.right < client.left || r.bottom < client.top || r.left > client.right ||
            r.top > client.bottom) {
            continue;
        }
        ::Rectangle(dc, r.left, r.top, r.right, r.bottom);
    }

    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(pen);
}

void MainWindow::DrawSprites(HDC dc, const RECT& client) const {
    if (!map_.IsOpen() || pxPerTile_ <= 0) return;
    const HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(120, 220, 255));
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));

    for (const SpriteRecord& s : map_.sprites().Items()) {
        const int sx = static_cast<int>(s.x) * pxPerTile_ / kTilePixels - scrollX_;
        const int sy = static_cast<int>(s.y) * pxPerTile_ / kTilePixels - scrollY_;
        const int size = std::max(3, pxPerTile_ / 2);
        RECT r = {sx - size / 2, sy - size / 2, sx - size / 2 + size, sy - size / 2 + size};
        if (r.right < client.left || r.bottom < client.top || r.left > client.right ||
            r.top > client.bottom) {
            continue;
        }
        if (s.DrawAsSprite()) {
            ::Ellipse(dc, r.left, r.top, r.right, r.bottom);   // bit12：按精灵绘制
        } else {
            ::Rectangle(dc, r.left, r.top, r.right, r.bottom);
        }
    }

    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(pen);
}

void MainWindow::DrawFog(HDC dc, const RECT& client) const {
    if (!map_.IsOpen() || !map_.fog().present() || pxPerTile_ <= 0) return;

    const int w = map_.width();
    const int h = map_.height();
    const int tx0 = std::max(0, scrollX_ / pxPerTile_);
    const int ty0 = std::max(0, scrollY_ / pxPerTile_);
    const int tx1 = std::min(w - 1, (scrollX_ + static_cast<int>(client.right)) / pxPerTile_);
    const int ty1 = std::min(h - 1, (scrollY_ + static_cast<int>(client.bottom)) / pxPerTile_);
    if (tx0 > tx1 || ty0 > ty1) return;

    // 1×1 预乘黑色源，逐行把连续的迷雾格合并成一个矩形做 AlphaBlend
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = -1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HDC mem = ::CreateCompatibleDC(dc);
    const HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!mem || !bmp || !bits) {
        if (bmp) ::DeleteObject(bmp);
        if (mem) ::DeleteDC(mem);
        return;
    }
    *static_cast<u32*>(bits) = 0x78000000u;   // ARGB：黑色，alpha 0x78
    const HGDIOBJ old = ::SelectObject(mem, bmp);
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0xFF, AC_SRC_ALPHA};

    for (int ty = ty0; ty <= ty1; ty++) {
        int runStart = -1;
        for (int tx = tx0; tx <= tx1 + 1; tx++) {
            const bool fog = tx <= tx1 && map_.fog().IsFogged(tx, ty);
            if (fog && runStart < 0) runStart = tx;
            if (!fog && runStart >= 0) {
                const int left = runStart * pxPerTile_ - scrollX_;
                const int top = ty * pxPerTile_ - scrollY_;
                ::AlphaBlend(dc, left, top, (tx - runStart) * pxPerTile_, pxPerTile_, mem, 0, 0,
                             1, 1, bf);
                runStart = -1;
            }
        }
    }

    ::SelectObject(mem, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
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

    if (layer_ == EditLayer::kFog) {
        BeginEdit();
        fogPaint_ = !map_.fog().IsFogged(tx, ty);
        if (map_.mutableFog().SetFogged(tx, ty, fogPaint_)) {
            map_.MarkDirty();
            RECT r;
            r.left = tx * pxPerTile_ - scrollX_;
            r.top = ty * pxPerTile_ - scrollY_;
            r.right = r.left + pxPerTile_;
            r.bottom = r.top + pxPerTile_;
            ::InvalidateRect(hwnd_, &r, FALSE);
        }
        cursorTx_ = tx;
        cursorTy_ = ty;
        UpdateTitle();
        return;
    }

    switch (tool_) {
        case Tool::kTerrain:
            BeginEdit();
            StampBrushAt(tx, ty);
            break;
        case Tool::kPicker:
            SetBrushTile(map_.TileAt(tx, ty));
            tool_ = Tool::kTerrain;
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Tool::kUnit:
            if (!PlaceUnitAtTile(tx, ty)) {
                MessageBeep(MB_ICONWARNING);   // 坐标越界或不可行走
            }
            break;
        case Tool::kSelect:
            BeginSelection(tx, ty);
            break;
        default:
            break;
    }
    cursorTx_ = tx;
    cursorTy_ = ty;
    UpdateTitle();
}

void MainWindow::OnLButtonDblClk(int mx, int my) {
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    if (tx < 0) return;
    if (layer_ == EditLayer::kLocations) {
        const int index = LocationIndexAtTile(tx, ty);
        if (index >= 0) EditLocation(index);
        return;
    }
    if (layer_ == EditLayer::kSpUnits) {
        const int index = SpriteIndexAtTile(tx, ty);
        if (index >= 0) OpenSpUnitEditor(index);
    }
}

void MainWindow::OnMouseMove(int mx, int my, bool leftDown) {
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    cursorTx_ = tx;
    cursorTy_ = ty;
    if (leftDown && layer_ == EditLayer::kFog && tx >= 0) {
        if (map_.mutableFog().SetFogged(tx, ty, fogPaint_)) {
            map_.MarkDirty();
            RECT r;
            r.left = tx * pxPerTile_ - scrollX_;
            r.top = ty * pxPerTile_ - scrollY_;
            r.right = r.left + pxPerTile_;
            r.bottom = r.top + pxPerTile_;
            ::InvalidateRect(hwnd_, &r, FALSE);
        }
        UpdateTitle();
        return;
    }
    if (leftDown && tool_ == Tool::kSelect && selecting_ && tx >= 0) {
        UpdateSelection(tx, ty);
        UpdateTitle();
        return;
    }
    if (leftDown && tool_ == Tool::kTerrain && tx >= 0) {
        BeginEdit();                 // 拖出窗口再回来时保持同一笔画
        StampBrushAt(tx, ty);
        UpdateTitle();
    }
}

void MainWindow::OnRButtonDown(int mx, int my) {
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    if (tx < 0) return;
    if (tool_ == Tool::kSelect) {
        ClearSelection();
        return;
    }
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
        case '4': tool_ = Tool::kSelect; break;
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
        const HBRUSH brush = ::CreateSolidBrush(UnitColorFor(u.type, showAllColors_));
        RECT r;
        r.left = sx + (pxPerTile_ - size) / 2;
        r.top = sy + (pxPerTile_ - size) / 2;
        r.right = r.left + size;
        r.bottom = r.top + size;
        ::FillRect(dc, &r, brush);
        ::DeleteObject(brush);
    }
}

void MainWindow::DrawOverlapMarkers(HDC dc, const RECT& client) const {
    if (!showOverlapMarkers_ || !map_.IsOpen() || pxPerTile_ <= 0) return;
    std::unordered_map<unsigned, int> counts;
    for (const UnitRecord& u : map_.units()) {
        const int tx = static_cast<int>(u.x) / kTilePixels;
        const int ty = static_cast<int>(u.y) / kTilePixels;
        if (tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) continue;
        counts[static_cast<unsigned>(ty) * static_cast<unsigned>(map_.width()) +
               static_cast<unsigned>(tx)]++;
    }
    const HBRUSH brush = ::CreateSolidBrush(RGB(255, 0, 0));
    for (const auto& entry : counts) {
        if (entry.second < 2) continue;
        const int tx = static_cast<int>(entry.first % static_cast<unsigned>(map_.width()));
        const int ty = static_cast<int>(entry.first / static_cast<unsigned>(map_.width()));
        const int sx = tx * pxPerTile_ - scrollX_;
        const int sy = ty * pxPerTile_ - scrollY_;
        RECT r = {sx, sy, sx + pxPerTile_, sy + pxPerTile_};
        if (r.right < 0 || r.bottom < 0 || r.left > client.right || r.top > client.bottom) continue;
        ::FrameRect(dc, &r, brush);
    }
    ::DeleteObject(brush);
}

void MainWindow::DrawUnitConflictSizes(HDC dc, const RECT& client) const {
    if (!showUnitConflict_ || !map_.IsOpen() || pxPerTile_ <= 0) return;
    const HPEN pen = ::CreatePen(PS_DOT, 1, RGB(255, 80, 80));
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
    for (const UnitRecord& u : map_.units()) {
        const int sx = static_cast<int>(u.x) / kTilePixels * pxPerTile_ - scrollX_;
        const int sy = static_cast<int>(u.y) / kTilePixels * pxPerTile_ - scrollY_;
        RECT r = {sx - pxPerTile_, sy - pxPerTile_, sx + pxPerTile_ * 2, sy + pxPerTile_ * 2};
        if (r.right < 0 || r.bottom < 0 || r.left > client.right || r.top > client.bottom) continue;
        ::Rectangle(dc, r.left, r.top, r.right, r.bottom);
    }
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(pen);
}

void MainWindow::DrawMinimap(HDC dc, const RECT& client) const {
    if (hideMinimap_ || !map_.IsOpen() || pxPerTile_ <= 0) return;
    const int viewW = client.right - client.left;
    const int viewH = client.bottom - client.top;
    const int maxSide = std::min(180, std::min(viewW, viewH) / 3);
    if (maxSide < 32) return;

    int mw = maxSide;
    int mh = maxSide;
    if (map_.width() >= map_.height()) {
        mh = std::max(16, maxSide * map_.height() / map_.width());
    } else {
        mw = std::max(16, maxSide * map_.width() / map_.height());
    }
    const int mx = viewW - mw - 10;
    const int my = viewH - mh - 10;

    RECT box = {mx - 2, my - 2, mx + mw + 2, my + mh + 2};
    HBRUSH back = ::CreateSolidBrush(RGB(0, 0, 0));
    ::FillRect(dc, &box, back);
    ::DeleteObject(back);

    // 地形：直接从整图 DIB 缩放采样
    if (backBuffer_ && image_.width > 0 && image_.height > 0) {
        const HDC mem = ::CreateCompatibleDC(dc);
        if (mem) {
            const HGDIOBJ old = ::SelectObject(mem, backBuffer_);
            const int oldMode = ::SetStretchBltMode(dc, COLORONCOLOR);
            ::StretchBlt(dc, mx, my, mw, mh, mem, 0, 0, image_.width, image_.height, SRCCOPY);
            ::SetStretchBltMode(dc, oldMode);
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }

    // 单位：颜色沿用主视图规则
    const int mapPxW = std::max(1, map_.width() * kTilePixels);
    const int mapPxH = std::max(1, map_.height() * kTilePixels);
    for (const UnitRecord& u : map_.units()) {
        const int x = mx + static_cast<int>(u.x) * mw / mapPxW;
        const int y = my + static_cast<int>(u.y) * mh / mapPxH;
        RECT r = {x, y, x + 2, y + 2};
        HBRUSH b = ::CreateSolidBrush(UnitColorFor(u.type, showAllColors_));
        ::FillRect(dc, &r, b);
        ::DeleteObject(b);
    }

    // 当前视口范围
    if (image_.width > 0 && image_.height > 0) {
        const int vx0 = mx + scrollX_ * mw / image_.width;
        const int vy0 = my + scrollY_ * mh / image_.height;
        const int vx1 = std::min(mx + mw, mx + (scrollX_ + viewW) * mw / image_.width);
        const int vy1 = std::min(my + mh, my + (scrollY_ + viewH) * mh / image_.height);
        RECT vp = {vx0, vy0, std::max(vx0 + 1, vx1), std::max(vy0 + 1, vy1)};
        HBRUSH viewBrush = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FrameRect(dc, &vp, viewBrush);
        ::DeleteObject(viewBrush);
    }

    RECT frame = {mx - 1, my - 1, mx + mw + 1, my + mh + 1};
    HBRUSH border = ::CreateSolidBrush(RGB(160, 160, 160));
    ::FrameRect(dc, &frame, border);
    ::DeleteObject(border);
}

void MainWindow::DrawHud(HDC dc) const {
    wchar_t text[1024];
    const wchar_t* toolName = L"地形刷";
    if (tool_ == Tool::kUnit) toolName = L"单位";
    else if (tool_ == Tool::kPicker) toolName = L"吸管";
    else if (tool_ == Tool::kSelect) toolName = L"框选";
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

    const unsigned brushGroup = (brushTile_ & 0x7FF0u) >> 4;
    const unsigned brushIndex = brushTile_ & 0x000Fu;
    ::wsprintfW(text, L"层: %s   工具: %s   刷子: 0x%04X (组 %u/索引 %u) [%ls %dx%d]   单位: %u / 玩家 %u / 资源 %u   缩放: %dpx/瓦片   撤销: %u / 重做: %u\n"
                      L"左键应用 / 右键删单位 / 1地形 2单位 3吸管 4框选 / Ctrl+X/C/V 剪贴板 / 滚轮缩放 / Ctrl+Z 撤销 / 编辑层→刷子 自定义图案",
                layerName, toolName, static_cast<unsigned>(brushTile_), brushGroup, brushIndex,
                brushPattern_.name.c_str(), brushPattern_.width, brushPattern_.height,
                static_cast<unsigned>(unitType_), static_cast<unsigned>(unitOwner_),
                static_cast<unsigned>(unitResource_), pxPerTile_,
                static_cast<unsigned>(history_.UndoCount()),
                static_cast<unsigned>(history_.RedoCount()));

    int extraLines = 0;
    if (debugMode_) {
        wchar_t dbg[320] = L"";
        const int tx = cursorTx_;
        const int ty = cursorTy_;
        const unsigned tile = (map_.IsOpen() && tx >= 0) ? map_.TileAt(tx, ty) : 0;
        const int unitsAt = (map_.IsOpen() && tx >= 0) ? map_.UnitCountAtTile(tx, ty) : 0;
        if (debugDataMode_ == 0) {
            ::wsprintfW(dbg, L"[调试] 光标格 %d,%d   单位 %d   渲染 %I64ums", tx, ty, unitsAt,
                        lastRenderMs_);
        } else if (debugDataMode_ == 1) {
            ::wsprintfW(dbg, L"[调试] 格 %d,%d   像素 %d,%d   MTXM 0x%04X   单位 %d   渲染 %I64ums",
                        tx, ty, tx * kTilePixels, ty * kTilePixels, tile, unitsAt, lastRenderMs_);
        } else {
            ::wsprintfW(dbg,
                        L"[调试] 格 %d,%d  像素 %d,%d  MTXM 0x%04X  单位 %d  图像 %dx%d  "
                        L"选区 %s  渲染 %I64ums",
                        tx, ty, tx * kTilePixels, ty * kTilePixels, tile, unitsAt,
                        image_.width, image_.height, hasSelection_ ? L"有" : L"无",
                        lastRenderMs_);
        }
        ::lstrcatW(text, L"\n");
        ::lstrcatW(text, dbg);
        extraLines++;
    }
    if (showCompareData_) {
        wchar_t cmp[192];
        if (editOpen_) {
            const Buffer& now = map_.tiles();
            int changed = 0;
            const std::size_t n = std::max(now.size(), tilesSnapshot_.size());
            for (std::size_t i = 0; i + 1 < n; i += 2) {
                const u8 b0 = i < now.size() ? now[i] : 0;
                const u8 b1 = i < tilesSnapshot_.size() ? tilesSnapshot_[i] : 0;
                const u8 b2 = i + 1 < now.size() ? now[i + 1] : 0;
                const u8 b3 = i + 1 < tilesSnapshot_.size() ? tilesSnapshot_[i + 1] : 0;
                if (b0 != b1 || b2 != b3) changed++;
            }
            ::wsprintfW(cmp, L"[对比] 地形改动 %d 格   单位 %u -> %u", changed,
                        static_cast<unsigned>(unitsSnapshot_.size()),
                        static_cast<unsigned>(map_.units().size()));
        } else {
            ::lstrcpyW(cmp, L"[对比] 当前无未提交编辑");
        }
        ::lstrcatW(text, L"\n");
        ::lstrcatW(text, cmp);
        extraLines++;
    }
    if (showDebugImageSize_) {
        wchar_t sizeText[128];
        ::wsprintfW(sizeText, L"[图像] %dx%d 像素   缩放 %d px/瓦片", image_.width, image_.height,
                    pxPerTile_);
        ::lstrcatW(text, L"\n");
        ::lstrcatW(text, sizeText);
        extraLines++;
    }

    const HGDIOBJ oldFont = ::SelectObject(dc, ::GetStockObject(DEFAULT_GUI_FONT));
    const int oldMode = ::SetBkMode(dc, OPAQUE);
    const COLORREF oldColor = ::SetTextColor(dc, RGB(255, 255, 255));
    const COLORREF oldBk = ::SetBkColor(dc, RGB(32, 32, 32));
    RECT box = {6, 6, 6 + 980, 6 + 48 + extraLines * 18};
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

    if (showDoodads_) DrawDoodads(dc, client);
    if (showSpUnits_) DrawSprites(dc, client);
    if (showFog_) DrawFog(dc, client);
    DrawGrid(dc, client);
    if (showUnits_) DrawUnits(dc, client);
    if (showOverlapMarkers_) DrawOverlapMarkers(dc, client);
    if (showUnitConflict_) DrawUnitConflictSizes(dc, client);
    if (showLocations_) DrawLocations(dc, client);
    DrawSelection(dc, client);

    // 光标所在格高亮
    if (cursorTx_ >= 0) {
        const int sx = cursorTx_ * pxPerTile_ - scrollX_;
        const int sy = cursorTy_ * pxPerTile_ - scrollY_;
        RECT r = {sx, sy, sx + pxPerTile_, sy + pxPerTile_};
        ::DrawFocusRect(dc, &r);
    }

    DrawMinimap(dc, client);
    DrawHud(dc);
}

void MainWindow::BeginEdit() {
    if (editOpen_) return;
    editOpen_ = true;
    tilesSnapshot_ = map_.tiles();
    unitsSnapshot_ = map_.units();
    triggersSnapshot_ = map_.triggers().Serialize();
    locationsSnapshot_ = map_.locations().Serialize();
    stringsSnapshot_ = map_.strings().Serialize();
    playersSnapshot_ = map_.players().Snapshot();
    restrictionsSnapshot_ = map_.restrictions().Snapshot();
    doodadsSnapshot_ = map_.doodads().Serialize();
    fogSnapshot_ = map_.fog().Serialize();
    spritesSnapshot_ = map_.sprites().Serialize();
    wavSnapshot_ = map_.sounds().Serialize();
    swnmSnapshot_ = map_.switchNames().Serialize();
    mbrfSnapshot_ = map_.briefing().Serialize();
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
                               a.type == b.type && a.owner == b.owner && a.resource == b.resource &&
                               a.hpPercent == b.hpPercent && a.shieldPercent == b.shieldPercent &&
                               a.energyPercent == b.energyPercent;
                    });

    const Buffer triggersAfter = map_.triggers().Serialize();
    const Buffer locationsAfter = map_.locations().Serialize();
    const Buffer stringsAfter = map_.strings().Serialize();
    const Buffer playersAfter = map_.players().Snapshot();
    const Buffer restrictionsAfter = map_.restrictions().Snapshot();
    const Buffer doodadsAfter = map_.doodads().Serialize();
    const Buffer fogAfter = map_.fog().Serialize();
    const Buffer spritesAfter = map_.sprites().Serialize();
    const Buffer wavAfter = map_.sounds().Serialize();
    const Buffer swnmAfter = map_.switchNames().Serialize();
    const Buffer mbrfAfter = map_.briefing().Serialize();
    const bool metaChanged = triggersAfter != triggersSnapshot_ ||
                             locationsAfter != locationsSnapshot_ ||
                             stringsAfter != stringsSnapshot_;
    const bool playersChanged = playersAfter != playersSnapshot_;
    const bool restrictionsChanged = restrictionsAfter != restrictionsSnapshot_;
    const bool decorChanged = doodadsAfter != doodadsSnapshot_ || fogAfter != fogSnapshot_ ||
                              spritesAfter != spritesSnapshot_;
    const bool extraChanged = wavAfter != wavSnapshot_ || swnmAfter != swnmSnapshot_ ||
                              mbrfAfter != mbrfSnapshot_;

    if (!tilesChanged && !unitsChanged && !metaChanged && !playersChanged &&
        !restrictionsChanged && !decorChanged && !extraChanged) {
        tilesSnapshot_.clear();
        unitsSnapshot_.clear();
        triggersSnapshot_.clear();
        locationsSnapshot_.clear();
        stringsSnapshot_.clear();
        playersSnapshot_.clear();
        restrictionsSnapshot_.clear();
        doodadsSnapshot_.clear();
        fogSnapshot_.clear();
        spritesSnapshot_.clear();
        wavSnapshot_.clear();
        swnmSnapshot_.clear();
        mbrfSnapshot_.clear();
        UpdateTitle();
        return;
    }

    EditCommand cmd;
    cmd.label = label;
    cmd.tilesBefore = std::move(tilesSnapshot_);
    cmd.tilesAfter = tiles;
    cmd.unitsBefore = std::move(unitsSnapshot_);
    cmd.unitsAfter = map_.units();
    if (metaChanged) {
        cmd.hasMeta = true;
        cmd.triggersBefore = std::move(triggersSnapshot_);
        cmd.triggersAfter = triggersAfter;
        cmd.locationsBefore = std::move(locationsSnapshot_);
        cmd.locationsAfter = locationsAfter;
        cmd.stringsBefore = std::move(stringsSnapshot_);
        cmd.stringsAfter = stringsAfter;
    }
    if (playersChanged) {
        cmd.hasPlayers = true;
        cmd.playersBefore = std::move(playersSnapshot_);
        cmd.playersAfter = playersAfter;
    }
    if (restrictionsChanged) {
        cmd.hasRestrictions = true;
        cmd.restrictionsBefore = std::move(restrictionsSnapshot_);
        cmd.restrictionsAfter = restrictionsAfter;
    }
    if (decorChanged) {
        cmd.hasDecor = true;
        cmd.doodadsBefore = std::move(doodadsSnapshot_);
        cmd.doodadsAfter = doodadsAfter;
        cmd.fogBefore = std::move(fogSnapshot_);
        cmd.fogAfter = fogAfter;
        cmd.spritesBefore = std::move(spritesSnapshot_);
        cmd.spritesAfter = spritesAfter;
    }
    if (extraChanged) {
        cmd.hasScenarioExtra = true;
        cmd.wavBefore = std::move(wavSnapshot_);
        cmd.wavAfter = wavAfter;
        cmd.swnmBefore = std::move(swnmSnapshot_);
        cmd.swnmAfter = swnmAfter;
        cmd.mbrfBefore = std::move(mbrfSnapshot_);
        cmd.mbrfAfter = mbrfAfter;
    }
    history_.Push(std::move(cmd));

    triggersSnapshot_.clear();
    locationsSnapshot_.clear();
    stringsSnapshot_.clear();
    playersSnapshot_.clear();
    restrictionsSnapshot_.clear();
    doodadsSnapshot_.clear();
    fogSnapshot_.clear();
    spritesSnapshot_.clear();
    wavSnapshot_.clear();
    swnmSnapshot_.clear();
    mbrfSnapshot_.clear();
    UpdateTitle();
}

void MainWindow::RefreshAfterHistory(const Buffer& oldTiles) {
    // 没有地形缓冲（未加载地形集）时只需让客户区重画（单位层由 Present 叠加）
    if (!dibBits_ || !map_.hasTileset() || image_.pixels.empty() || pxPerTile_ <= 0) {
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    bool any = false;
    if (!DiffTileRect(oldTiles, map_.tiles(), map_.width(), map_.height(), x0, y0, x1, y1, any)) {
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!any) {
        ::InvalidateRect(hwnd_, nullptr, FALSE);   // 只有单位层变化
        return;
    }

    const long long area = static_cast<long long>(x1 - x0 + 1) * (y1 - y0 + 1);
    const long long total = static_cast<long long>(map_.width()) * map_.height();
    if (area * 4 > total) {                        // 改动过大：整图重建更划算
        const int sx = scrollX_;
        const int sy = scrollY_;
        RebuildView(pxPerTile_);
        scrollX_ = sx;
        scrollY_ = sy;
        UpdateScrollBars();
        return;
    }

    if (!renderer_.RenderRegion(map_.tiles(), map_.width(), map_.height(), pxPerTile_,
                                x0, y0, x1, y1, image_)) {
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // 把 image_ 中变化区域同步到 DIB（逐行 memcpy，避免整图拷贝）
    const std::size_t stride = static_cast<std::size_t>(image_.width) * 4;
    const std::size_t left = static_cast<std::size_t>(x0 * pxPerTile_) * 4;
    const std::size_t bytes = static_cast<std::size_t>((x1 - x0 + 1) * pxPerTile_) * 4;
    const int top = y0 * pxPerTile_;
    const int bottom = (y1 + 1) * pxPerTile_;
    u8* dst = static_cast<u8*>(dibBits_);
    for (int y = top; y < bottom; y++) {
        std::memcpy(dst + static_cast<std::size_t>(y) * stride + left,
                    image_.pixels.data() + static_cast<std::size_t>(y) * stride + left, bytes);
    }

    RECT r;
    r.left = x0 * pxPerTile_ - scrollX_;
    r.top = y0 * pxPerTile_ - scrollY_;
    r.right = (x1 + 1) * pxPerTile_ - scrollX_;
    r.bottom = (y1 + 1) * pxPerTile_ - scrollY_;
    ::InvalidateRect(hwnd_, &r, FALSE);
    Log(L"[render] 局部重绘 %dx%d 格 (%d,%d)-(%d,%d)", x1 - x0 + 1, y1 - y0 + 1, x0, y0, x1, y1);
}

bool MainWindow::Undo() {
    const Buffer before = map_.tiles();
    if (!history_.Undo(map_)) return false;
    RefreshAfterHistory(before);
    UpdateTitle();
    return true;
}

bool MainWindow::Redo() {
    const Buffer before = map_.tiles();
    if (!history_.Redo(map_)) return false;
    RefreshAfterHistory(before);
    UpdateTitle();
    return true;
}

bool MainWindow::ApplyRadarFogCommand(int id) {
    enum class Kind { kNone, kPlace, kRemove, kDisableFog, kAllowFog } kind = Kind::kNone;
    int player = -2;
    if (id >= kPlaceRadarP1 && id <= kPlaceRadarP1 + 7) {
        kind = Kind::kPlace;
        player = id - kPlaceRadarP1;
    } else if (id == kPlaceRadarAll) {
        kind = Kind::kPlace;
        player = -1;
    } else if (id >= kRemoveRadarP1 && id <= kRemoveRadarP1 + 7) {
        kind = Kind::kRemove;
        player = id - kRemoveRadarP1;
    } else if (id == kRemoveRadarAll) {
        kind = Kind::kRemove;
        player = -1;
    } else if (id >= kDisableFogP1 && id <= kDisableFogP1 + 7) {
        kind = Kind::kDisableFog;
        player = id - kDisableFogP1;
    } else if (id == kDisableFogAll) {
        kind = Kind::kDisableFog;
        player = -1;
    } else if (id >= kAllowFogP1 && id <= kAllowFogP1 + 7) {
        kind = Kind::kAllowFog;
        player = id - kAllowFogP1;
    } else if (id == kAllowFogAll) {
        kind = Kind::kAllowFog;
        player = -1;
    } else {
        return false;
    }

    if (!map_.IsOpen()) {
        MessageBeep(MB_ICONWARNING);
        return true;
    }

    BeginEdit();
    int changed = 0;
    const wchar_t* label = L"";
    switch (kind) {
        case Kind::kPlace:
            changed = PlaceMapRevealers(map_, player);
            label = L"放置小雷达";
            break;
        case Kind::kRemove:
            changed = RemoveMapRevealers(map_, player);
            label = L"删除小雷达";
            break;
        case Kind::kDisableFog:
            changed = SetFogForPlayers(map_, player, false) ? 1 : 0;
            label = L"禁止迷雾";
            break;
        case Kind::kAllowFog:
            changed = SetFogForPlayers(map_, player, true) ? 1 : 0;
            label = L"允许迷雾";
            break;
        default:
            break;
    }

    if (changed == 0) {
        editOpen_ = false;   // 没有实际改动，不产生历史
    } else {
        CommitEdit(label);
    }
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void MainWindow::OnCommand(int id) {
    if (ApplyRadarFogCommand(id)) return;
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
        case kFileRecentMaps: {
            const std::vector<std::wstring>& recent = AppSettings::Instance().recentMaps();
            if (recent.empty()) {
                ::MessageBoxW(hwnd_, L"还没有最近打开的地图。", kWindowTitle, MB_ICONINFORMATION);
                break;
            }
            HMENU popup = ::CreatePopupMenu();
            for (std::size_t i = 0; i < recent.size(); i++) {
                wchar_t label[512];
                ::wsprintfW(label, L"%u. %ls", static_cast<unsigned>(i) + 1,
                            FileNameOf(recent[i]).c_str());
                ::AppendMenuW(popup, MF_STRING, kRecentMenuBase + static_cast<int>(i), label);
            }
            POINT pt;
            ::GetCursorPos(&pt);
            const int chosen = ::TrackPopupMenu(
                popup, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                pt.x, pt.y, 0, hwnd_, nullptr);
            ::DestroyMenu(popup);
            if (chosen < kRecentMenuBase ||
                chosen >= kRecentMenuBase + static_cast<int>(recent.size())) {
                break;
            }
            const std::wstring path = recent[static_cast<std::size_t>(chosen - kRecentMenuBase)];
            if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                ::MessageBoxW(hwnd_, L"该地图文件已不存在。", kWindowTitle, MB_ICONWARNING);
                break;
            }
            if (MaybeSave()) OpenMap(path);
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
        case kFileSaveImage:
            SaveMapImage();
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
            ClearSelection();
            break;
        case kEditSelectTool:
            tool_ = Tool::kSelect;
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kEditProperties:
            EditUnitProps();
            break;
        case kScenarioResize:
            ResizeMapDialog();
            break;
        case kScenarioDescription:
            EditMapText(2, true);      // STR[2] 地图描述（富文本）
            break;
        case kScenarioStrings:
            EditMapText(1, false);     // STR[1] 地图名
            break;
        case kScenarioForces:
            OpenPlayerRoster();
            break;
        case kScenarioUnits:
            ShowRecoveredDialog(IDD_DLG_172, hwnd_);
            break;
        case kScenarioUpgrades:
            OpenRestrictions();
            break;
        case kScenarioTech:
            OpenRestrictions();
            break;
        case kScenarioGotoStart:
            GotoStartPosition();
            break;
        case kScenarioSounds: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_SOUNDS, hwnd_);
            SetDialogDocument(nullptr);
            CommitEdit(L"编辑声音");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kScenarioBriefing: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_BRIEFING, hwnd_);
            SetDialogDocument(nullptr);
            CommitEdit(L"编辑任务简报");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kLayerBrush:
            OpenCustomBrush();
            break;
        case kWindowBrush:
            ShowBrushPalette();
            break;
        case kWindowUnitPanel:
            OpenUnitPlacement();
            break;
        case kWindowSpUnits: {
            const int index = cursorTx_ >= 0 ? SpriteIndexAtTile(cursorTx_, cursorTy_) : -1;
            OpenSpUnitEditor(index);
            break;
        }
        case kPlaceBuildNormal:
        case kPlaceUnitGrid:
        case kPlaceGroundOverlap:
        case kPlaceAirOverlap:
        case kPlaceUnitAnywhere:
        case kPlaceBuildingAnywhere:
        case kPlaceAutoAddon:
        case kPlaceMineralDistance:
        case kClipboardDoodads:
            TogglePlacementRule(id);
            break;
        case kOptionsEditorSettings:
            ShowRecoveredDialog(IDD_DLG_155, hwnd_);
            break;
        case kAdvancedCheckUpdate:
            ShowRecoveredDialog(IDD_DLG_174, hwnd_);
            break;
        case kAdvancedBackup:
            ShowRecoveredDialog(IDD_DLG_173, hwnd_);
            break;
        case kAdvancedDefaultEditor:
            SetDefaultMapEditor();
            break;
        case kAdvancedShortcut:
            CreateStartMenuShortcut();
            break;
        case kWindowTerrain:
            InvalidateRect(hwnd_, nullptr, FALSE);   // 地形视图即主视图，重绘即可
            break;
        case kWindowDoodads:
            Log(L"[ui] 装饰物窗口暂未实现");
            MessageBeep(MB_ICONWARNING);
            break;
        case kEditCut:
            CopySelection(true);
            break;
        case kEditCopy:
            CopySelection(false);
            break;
        case kEditPaste:
            PasteClipboard();
            break;
        case kEditSelectAll:
            SelectAllTiles();
            break;
        case kEditOverlapUnits: {
            if (!map_.IsOpen() || !hasSelection_) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            const bool cursorInside = cursorTx_ >= selX0_ && cursorTx_ <= selX1_ &&
                                      cursorTy_ >= selY0_ && cursorTy_ <= selY1_;
            const int tx = cursorInside ? cursorTx_ : SelectionCenterX();
            const int ty = cursorInside ? cursorTy_ : SelectionCenterY();
            if (OverlapSelectedUnitsAt(tx, ty) == 0) MessageBeep(MB_ICONWARNING);
            break;
        }
        case kDeleteInvalidUnits:
            if (DeleteInvalidUnits() == 0) MessageBeep(MB_ICONWARNING);
            break;
        case kFixDoodadErrors: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            const int fixed = map_.mutableDoodads().FixErrors(map_.width(), map_.height());
            if (fixed > 0) {
                map_.MarkDirty();
                CommitEdit(L"纠正装饰物错误");
                UpdateTitle();
                ::InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                editOpen_ = false;
                MessageBeep(MB_ICONWARNING);
            }
            break;
        }
        case kEditConvertTerrain: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            int x0 = 0, y0 = 0, x1 = map_.width() - 1, y1 = map_.height() - 1;
            if (hasSelection_) {
                x0 = std::min(selX0_, selX1_);
                x1 = std::max(selX0_, selX1_);
                y0 = std::min(selY0_, selY1_);
                y1 = std::max(selY0_, selY1_);
            } else if (cursorTx_ >= 0) {
                x0 = x1 = cursorTx_;
                y0 = y1 = cursorTy_;
            } else {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            const int removed = map_.mutableDoodads().RemoveInTileRect(x0, y0, x1, y1);
            if (removed > 0) {
                map_.MarkDirty();
                CommitEdit(L"转换地形");
            } else {
                editOpen_ = false;
                MessageBeep(MB_ICONWARNING);
            }
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kRandomizeMinerals: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            if (RandomizeMineralTypes(map_) > 0) {
                CommitEdit(L"矿物外形随机");
            } else {
                editOpen_ = false;
            }
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kZoom25: ApplyZoomPercent(25); break;
        case kZoom33: ApplyZoomPercent(33); break;
        case kZoom50: ApplyZoomPercent(50); break;
        case kZoom66: ApplyZoomPercent(66); break;
        case kZoom100: ApplyZoomPercent(100); break;
        case kZoom150: ApplyZoomPercent(150); break;
        case kZoom200: ApplyZoomPercent(200); break;
        case kZoom300: ApplyZoomPercent(300); break;
        case kZoom400: ApplyZoomPercent(400); break;
        case kLayerTerrain:
            SetLayer(EditLayer::kTerrain);
            break;
        case kLayerDoodads:
            SetLayer(EditLayer::kDoodads);
            break;
        case kLayerFog:
            SetLayer(EditLayer::kFog);
            break;
        case kFogDefaultUnexplored:
        case kFogDefaultExplored: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            map_.mutableFog().SetAll(id == kFogDefaultUnexplored ? 0xFF : 0x00,
                                     map_.width(), map_.height());
            map_.MarkDirty();
            CommitEdit(L"战争迷雾默认");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
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
        case kDebugEnable:
            debugMode_ = !debugMode_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugDataNormal:
        case kDebugDataExact:
        case kDebugDataFinal:
            debugDataMode_ = id == kDebugDataNormal ? 0 : (id == kDebugDataExact ? 1 : 2);
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugOverlaps:
            showOverlapMarkers_ = !showOverlapMarkers_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugCompare:
            showCompareData_ = !showCompareData_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugUnitConflict:
            showUnitConflict_ = !showUnitConflict_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugCpu:
            showCpuTiming_ = !showCpuTiming_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugImageSize:
            showDebugImageSize_ = !showDebugImageSize_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugClearScreen:
            ::InvalidateRect(hwnd_, nullptr, TRUE);
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
        case kLayerFogToggle:
        case kDispLocations:
            ToggleDisplay(id);
            break;
        case kLocRename: {
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            POINT pt;
            ::GetCursorPos(&pt);
            ::ScreenToClient(hwnd_, &pt);
            int tx = -1, ty = -1;
            TileCoordsFromClient(pt.x, pt.y, tx, ty);
            const int index = tx >= 0 ? LocationIndexAtTile(tx, ty) : -1;
            if (index < 0) {
                ::MessageBoxW(hwnd_, L"光标不在任何地点上。", kWindowTitle,
                              MB_ICONINFORMATION);
                break;
            }
            EditLocation(index);
            break;
        }
        case kTriggerEditor:
            if (!map_.IsOpen()) {
                MessageBeep(MB_ICONWARNING);
                break;
            }
            BeginEdit();
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_DLG_181, hwnd_);
            SetDialogDocument(nullptr);
            CommitEdit(L"编辑触发");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugDialogs:
            ShowRecoveredDialog(IDD_DLG_155, hwnd_);
            break;
        case kFileExit:
            if (MaybeSave()) ::DestroyWindow(hwnd_);
            break;
        case kHelpAbout:
            ShowRecoveredDialog(IDD_DLG_103, hwnd_);
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
        case BrushPalette::kMsgSelected:
            SetBrushTile(static_cast<u16>(wParam));
            return 0;
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
        case WM_TIMER: {
            if (wParam != 1) return 0;
            const AppSettings& settings = AppSettings::Instance();
            if (!settings.autoBackupEnabled() || !map_.IsOpen() || !map_.IsDirty() ||
                mapPath_.empty()) {
                return 0;
            }
            if (::GetFileAttributesW(mapPath_.c_str()) == INVALID_FILE_ATTRIBUTES) return 0;
            const ULONGLONG now = ::GetTickCount64();
            const ULONGLONG interval =
                static_cast<ULONGLONG>(settings.autoBackupMinutes()) * 60000ull;
            if (lastBackupTick_ != 0 && now - lastBackupTick_ < interval) return 0;
            if (MakeBackup(mapPath_, settings.backupCount())) {
                lastBackupTick_ = now;
                Log(L"[backup] auto %s", mapPath_.c_str());
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            ::SetCapture(hwnd_);
            OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONDBLCLK:
            OnLButtonDblClk(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            ::ReleaseCapture();
            if (selecting_) {
                selecting_ = false;
                UpdateTitle();
                return 0;
            }
            if (layer_ == EditLayer::kFog && editOpen_) CommitEdit(L"编辑战争迷雾");
            else if (tool_ == Tool::kTerrain) CommitEdit(L"绘制地形");
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
        case WM_DESTROY: {
            // 保存视图状态（缩放/方格/编辑层/显示选项）；无头回归时跳过
            if (AppSettings::Instance().persistViewOnExit()) {
                ViewState& view = AppSettings::Instance().mutableView();
                view.zoomPx = pxPerTile_;
                view.gridEnabled = gridEnabled_;
                view.gridCellX = gridCellX_;
                view.gridCellY = gridCellY_;
                view.gridColorId = gridColorCommand_;
                view.gridColorValue = static_cast<unsigned>(gridColor_);
                view.layer = static_cast<int>(layer_);
                view.showUnits = showUnits_;
                view.showDoodads = showDoodads_;
                view.showSpUnits = showSpUnits_;
                view.showAllColors = showAllColors_;
                view.showZergGround = showZergGround_;
                view.showSpecialStates = showSpecialStates_;
                view.hideMinimap = hideMinimap_;
                view.allowColorCycle = allowColorCycle_;
                view.showDeathFx = showDeathFx_;
                view.showFog = showFog_;
                view.showLocations = showLocations_;
                AppSettings::Instance().Save();
            }
            ReleaseBackBuffer();
            ::PostQuitMessage(0);
            return 0;
        }
        default:
            return ::DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

}  // namespace scmd
