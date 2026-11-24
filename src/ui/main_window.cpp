#include "ui/main_window.h"

#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <unordered_map>
#include <utility>
#include <windows.h>
#include <windowsx.h>

#include "common/log.h"
#include "common/string_util.h"
#include "app/app_settings.h"
#include "app/backups.h"
#include "players/player_model.h"
#include "platform/file_dialog.h"
#include "map/map_switches.h"
#include "terrain/edge_placer.h"
#include "res/resource.h"
#include "ui/command_ids.h"
#include "ui/dialog_registry.h"
#include "ui/dlg_common.h"
#include "ui/dlg_procs.h"
#include "ui/dialogs.h"

#pragma comment(lib, "msimg32.lib")   // DrawFog 的 AlphaBlend
#pragma comment(lib, "comctl32.lib")  // 右侧栏场景树（TreeView）

namespace scmd {

namespace {
constexpr const wchar_t* kWindowClass = L"SCMDPlusMainWindow";
constexpr const wchar_t* kWindowTitle = L"SCMD Plus 3.0";

// 布局常量：左侧栏宽度 / 状态栏高度 / 场景树控件 ID
constexpr int kSidebarWidth = 140;
constexpr int kStatusBarHeight = 22;
constexpr int kSceneTreeId = 0x8FF0;
constexpr int kHScrollId = 0x8FF1;
constexpr int kVScrollId = 0x8FF2;
constexpr int kStatusBarId = 0x8FF3;

constexpr const wchar_t* kMapFilter =
    L"StarCraft 地图 (*.scx;*.scm)\0*.scx;*.scm\0所有文件 (*.*)\0*.*\0\0";

// 同进程多窗口：记录所有存活主窗口（加速键分派与退出时机）
std::vector<MainWindow*>& LiveWindows() {
    static std::vector<MainWindow*> windows;
    return windows;
}

// 状态栏用的地形集名称（与 MapDocument 内部顺序一致）
const wchar_t* const kEraNamesStatus[8] = {
    L"Badlands", L"Space", L"Installation", L"Ashworld",
    L"Jungle", L"Desert", L"Ice", L"Twilight",
};

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

// 0x80D1：允许颜色循环时按时间旋转色相，让单位标记缓慢变色
COLORREF RotateHue(COLORREF color, int degrees) {
    const int r = GetRValue(color);
    const int g = GetGValue(color);
    const int b = GetBValue(color);
    const int mx = std::max(r, std::max(g, b));
    const int mn = std::min(r, std::min(g, b));
    const int d = mx - mn;
    if (d == 0) return color;
    int h = 0;
    if (mx == r) h = 60 * (g - b) / d;
    else if (mx == g) h = 120 + 60 * (b - r) / d;
    else h = 240 + 60 * (r - g) / d;
    h = (h + degrees) % 360;
    if (h < 0) h += 360;
    const int s = (d * 255) / mx;
    const int v = mx;
    const int sector = h / 60;
    const int f = h % 60;
    const int p = v * (255 - s) / 255;
    const int q = v * (255 - (s * f) / 60) / 255;
    const int t = v * (255 - (s * (60 - f)) / 60) / 255;
    switch (sector) {
        case 0: return RGB(v, t, p);
        case 1: return RGB(q, v, p);
        case 2: return RGB(p, v, t);
        case 3: return RGB(p, q, v);
        case 4: return RGB(t, p, v);
        default: return RGB(v, p, q);
    }
}

COLORREF CycleColor(COLORREF color, bool enabled) {
    if (!enabled) return color;
    return RotateHue(color, static_cast<int>((::GetTickCount64() / 40ull) % 360ull));
}

// 附属建筑 → 宿主与附属槽偏移（瓦片；宿主左上角为原点）
struct AddonRule {
    u16 addon;
    u16 parent;
    int dx;
    int dy;
};
const AddonRule kAddonRules[] = {
    {107, 106, 4, 1},   // Comsat Station → Command Center
    {108, 106, 4, 1},   // Nuclear Silo → Command Center
    {120, 113, 4, 1},   // Machine Shop → Factory
    {115, 114, 3, 1},   // Control Tower → Starport
    {118, 116, 3, 1},   // Physics Lab → Science Facility
    {117, 116, 3, 1},   // Covert Ops → Science Facility
};
constexpr int kMineralBaseMaxDistance = 12;

const AddonRule* FindAddonRule(u16 type) {
    for (const AddonRule& rule : kAddonRules) {
        if (rule.addon == type) return &rule;
    }
    return nullptr;
}

bool IsResourceType(u16 type) {
    return (type >= 176 && type <= 178) || type == 188;
}

// 光标落在某个兼容宿主的附属槽 2×2 内时，吸附到槽左上角
bool SnapAddonToParent(const MapDocument& map, u16 type, int& tx, int& ty) {
    const AddonRule* rule = FindAddonRule(type);
    if (!rule) return false;
    for (const UnitRecord& u : map.units()) {
        if (u.type != rule->parent) continue;
        const int sx = static_cast<int>(u.x) / kTilePixels + rule->dx;
        const int sy = static_cast<int>(u.y) / kTilePixels + rule->dy;
        if (tx >= sx && tx <= sx + 1 && ty >= sy && ty <= sy + 1) {
            tx = sx;
            ty = sy;
            return true;
        }
    }
    return false;
}

bool ResourceNearBase(const MapDocument& map, int tx, int ty, int maxDistance) {
    for (const UnitRecord& u : map.units()) {
        if (u.type != 214) continue;   // 出生点视作基地
        const int sx = static_cast<int>(u.x) / kTilePixels;
        const int sy = static_cast<int>(u.y) / kTilePixels;
        if (std::max(std::abs(tx - sx), std::abs(ty - sy)) <= maxDistance) return true;
    }
    return false;
}

// 单位碰撞盒（像素，相对单位中心；来源：staredit wiki List of units，覆盖 0..132）
struct UnitBox {
    u16 type;
    u8 left;
    u8 right;
    u8 up;
    u8 down;
};
const UnitBox kUnitBoxes[] = {
    {0, 8, 8, 9, 10},
    {1, 7, 7, 10, 11},
    {2, 16, 15, 16, 15},
    {3, 16, 15, 16, 15},
    {5, 16, 15, 16, 15},
    {7, 11, 11, 11, 11},
    {8, 19, 18, 15, 14},
    {9, 32, 32, 33, 16},
    {10, 11, 11, 13, 14},
    {11, 24, 24, 16, 20},
    {12, 37, 37, 29, 29},
    {13, 7, 7, 7, 7},
    {14, 7, 7, 14, 14},
    {15, 8, 8, 9, 10},
    {16, 7, 7, 10, 11},
    {17, 16, 15, 16, 15},
    {19, 16, 15, 16, 15},
    {20, 8, 8, 9, 10},
    {21, 19, 18, 15, 14},
    {22, 32, 32, 33, 16},
    {23, 16, 15, 16, 15},
    {25, 16, 15, 16, 15},
    {27, 37, 37, 29, 29},
    {28, 37, 37, 29, 29},
    {29, 37, 37, 29, 29},
    {30, 16, 15, 16, 15},
    {32, 11, 11, 7, 14},
    {33, 13, 13, 13, 17},
    {34, 8, 8, 7, 11},
    {35, 8, 7, 8, 7},
    {36, 16, 15, 16, 15},
    {37, 8, 7, 4, 11},
    {38, 10, 10, 10, 12},
    {39, 19, 18, 16, 15},
    {40, 9, 9, 9, 9},
    {41, 11, 11, 11, 11},
    {42, 25, 24, 25, 24},
    {43, 22, 21, 22, 21},
    {44, 22, 21, 22, 21},
    {45, 24, 23, 24, 23},
    {46, 13, 13, 12, 12},
    {47, 12, 11, 12, 11},
    {48, 19, 18, 16, 15},
    {49, 24, 23, 24, 23},
    {50, 8, 8, 9, 10},
    {51, 7, 7, 10, 11},
    {52, 14, 14, 14, 14},
    {53, 10, 10, 10, 12},
    {54, 8, 7, 4, 11},
    {55, 22, 21, 22, 21},
    {56, 22, 21, 22, 21},
    {57, 25, 24, 25, 24},
    {58, 24, 24, 16, 20},
    {59, 16, 15, 16, 15},
    {60, 18, 17, 16, 15},
    {61, 12, 11, 6, 19},
    {62, 22, 21, 22, 21},
    {63, 16, 15, 16, 15},
    {64, 11, 11, 11, 11},
    {65, 11, 11, 5, 13},
    {66, 15, 16, 15, 16},
    {67, 12, 11, 10, 13},
    {68, 16, 15, 16, 15},
    {69, 20, 19, 16, 15},
    {70, 18, 17, 16, 15},
    {71, 22, 21, 22, 21},
    {72, 32, 31, 32, 31},
    {73, 8, 7, 8, 7},
    {74, 12, 11, 6, 19},
    {75, 12, 11, 6, 19},
    {76, 16, 15, 16, 15},
    {77, 11, 11, 5, 13},
    {78, 15, 16, 15, 16},
    {79, 12, 11, 10, 13},
    {80, 18, 17, 16, 15},
    {81, 16, 15, 16, 15},
    {82, 32, 31, 32, 31},
    {83, 16, 15, 16, 15},
    {84, 16, 15, 16, 15},
    {85, 2, 2, 2, 2},
    {86, 22, 21, 22, 21},
    {87, 12, 11, 14, 13},
    {88, 18, 17, 16, 15},
    {89, 16, 15, 16, 15},
    {90, 16, 15, 16, 15},
    {91, 15, 16, 15, 16},
    {92, 15, 16, 15, 16},
    {93, 16, 15, 16, 15},
    {94, 16, 15, 16, 15},
    {95, 16, 15, 16, 15},
    {96, 16, 15, 16, 15},
    {97, 16, 15, 16, 15},
    {98, 24, 24, 16, 20},
    {99, 7, 7, 10, 11},
    {100, 7, 7, 10, 11},
    {101, 13, 13, 13, 17},
    {102, 37, 37, 29, 29},
    {103, 15, 16, 15, 16},
    {104, 7, 7, 10, 11},
    {105, 60, 59, 40, 39},
    {106, 58, 58, 41, 41},
    {107, 37, 31, 16, 25},
    {108, 37, 31, 16, 25},
    {109, 38, 38, 22, 26},
    {110, 56, 56, 32, 31},
    {111, 48, 56, 40, 32},
    {112, 40, 44, 32, 24},
    {113, 56, 56, 40, 40},
    {114, 48, 48, 40, 38},
    {115, 47, 28, 24, 22},
    {116, 48, 48, 38, 38},
    {117, 47, 28, 24, 22},
    {118, 47, 28, 24, 22},
    {119, 48, 47, 32, 31},
    {120, 39, 31, 24, 24},
    {121, 48, 47, 48, 47},
    {122, 48, 48, 32, 28},
    {123, 48, 47, 32, 22},
    {124, 16, 16, 32, 16},
    {125, 32, 32, 24, 16},
    {126, 48, 47, 32, 31},
    {127, 48, 47, 32, 31},
    {128, 16, 15, 16, 15},
    {129, 16, 15, 16, 15},
    {130, 58, 58, 41, 41},
    {131, 49, 49, 32, 32},
    {132, 49, 49, 32, 32},
    {133, 49, 49, 32, 32},
    {134, 32, 31, 32, 31},
    {135, 40, 40, 32, 24},
    {136, 48, 48, 32, 4},
    {137, 28, 28, 32, 24},
    {138, 38, 32, 28, 28},
    {139, 44, 32, 32, 20},
    {140, 40, 32, 32, 31},
    {141, 28, 28, 32, 24},
    {142, 36, 40, 28, 18},
    {143, 24, 23, 24, 23},
    {144, 24, 23, 24, 23},
    {145, 48, 47, 48, 47},
    {146, 24, 23, 24, 23},
    {147, 80, 79, 32, 40},
    {148, 80, 79, 32, 40},
    {149, 64, 63, 32, 21},
    {150, 32, 31, 32, 31},
    {151, 40, 32, 32, 31},
    {152, 40, 32, 32, 31},
    {153, 16, 15, 16, 15},
    {154, 56, 56, 39, 39},
    {155, 36, 40, 16, 20},
    {156, 16, 16, 12, 20},
    {157, 48, 48, 32, 24},
    {158, 64, 63, 48, 47},
    {159, 44, 44, 16, 28},
    {160, 48, 48, 32, 40},
    {161, 48, 47, 48, 47},
    {162, 20, 20, 16, 16},
    {163, 24, 40, 24, 24},
    {164, 40, 40, 24, 24},
    {165, 32, 32, 24, 24},
    {166, 36, 36, 24, 20},
    {167, 48, 48, 40, 32},
    {168, 64, 63, 48, 47},
    {169, 40, 47, 32, 24},
    {170, 44, 44, 28, 28},
    {171, 32, 32, 32, 20},
    {172, 32, 32, 16, 16},
    {173, 64, 63, 48, 47},
    {174, 112, 111, 48, 47},
    {175, 80, 79, 34, 63},
    {176, 32, 31, 16, 15},
    {177, 32, 31, 16, 15},
    {178, 32, 31, 16, 15},
    {179, 32, 31, 32, 31},
    {180, 32, 31, 32, 31},
    {181, 32, 31, 32, 31},
    {182, 16, 15, 16, 15},
    {183, 16, 15, 16, 15},
    {184, 32, 31, 32, 31},
    {185, 16, 15, 16, 15},
    {186, 16, 15, 16, 15},
    {187, 16, 15, 16, 15},
    {188, 64, 63, 32, 31},
    {189, 48, 47, 32, 31},
    {190, 80, 69, 38, 47},
    {191, 48, 47, 32, 31},
    {192, 48, 47, 32, 31},
    {193, 48, 47, 32, 31},
    {194, 48, 47, 32, 31},
    {195, 48, 47, 32, 31},
    {196, 48, 47, 32, 31},
    {197, 48, 47, 32, 31},
    {198, 48, 47, 32, 31},
    {199, 48, 47, 32, 31},
    {200, 56, 63, 28, 43},
    {201, 48, 47, 32, 31},
    {202, 80, 79, 80, 79},
    {203, 32, 31, 32, 31},
    {204, 128, 127, 64, 63},
    {205, 25, 44, 17, 20},
    {206, 44, 25, 17, 20},
    {207, 41, 28, 17, 20},
    {208, 28, 41, 17, 20},
    {209, 32, 31, 32, 31},
    {210, 16, 15, 16, 15},
    {211, 16, 15, 16, 15},
    {212, 16, 15, 16, 15},
    {213, 16, 15, 16, 15},
    {214, 48, 48, 32, 32},
    {215, 16, 15, 16, 15},
    {216, 16, 15, 16, 15},
    {217, 16, 15, 16, 15},
    {218, 16, 15, 16, 15},
    {219, 16, 15, 16, 15},
    {220, 16, 15, 16, 15},
    {221, 16, 15, 16, 15},
    {222, 16, 15, 16, 15},
    {223, 16, 15, 16, 15},
    {224, 16, 15, 16, 15},
    {225, 16, 15, 16, 15},
    {226, 16, 15, 16, 15},
    {227, 16, 15, 16, 15},
};

// 空中单位（依据名称/资料整理，覆盖 0..132 中可放置的空中单位）
bool IsAirUnitType(u16 type) {
    switch (type) {
        case 8: case 9: case 11: case 12: case 14:
        case 21: case 22: case 27: case 28: case 29:
        case 42: case 43: case 44: case 45: case 47: case 49:
        case 55: case 56: case 57: case 58: case 60: case 62:
        case 69: case 70: case 71: case 72: case 73:
        case 80: case 82: case 84: case 86: case 88:
        case 98: case 102:
            return true;
        default:
            return false;
    }
}

const UnitBox* FindUnitBox(u16 type) {
    for (const UnitBox& box : kUnitBoxes) {
        if (box.type == type) return &box;
    }
    return nullptr;
}

bool IsBuildingType(u16 type) {
    if (type == 128 || type == 129) return false;   // 水晶不是建筑
    return (type >= 106 && type <= 127) || (type >= 130 && type <= 175);
}

// 地面/空中重叠规则：未勾选允许重叠时，同层碰撞盒相交即拒绝。
// 未知类型不额外限制，避免误拒绝。
bool UnitOverlapsSameLayer(const MapDocument& map, u16 type, int tx, int ty, bool allowGround,
                           bool allowAir) {
    if (type == 214) return false;   // 出生点是标记，不参与碰撞
    const UnitBox* box = FindUnitBox(type);
    if (!box) return false;
    const bool air = IsAirUnitType(type);
    if (air ? allowAir : allowGround) return false;
    const int cx = tx * kTilePixels;
    const int cy = ty * kTilePixels + 16;
    const int x1 = cx - box->left;
    const int x2 = cx + box->right;
    const int y1 = cy - box->up;
    const int y2 = cy + box->down;
    for (const UnitRecord& u : map.units()) {
        if (u.type == 214) continue;
        if (IsAirUnitType(u.type) != air) continue;
        const UnitBox* other = FindUnitBox(u.type);
        if (!other) continue;
        const int ux1 = static_cast<int>(u.x) - other->left;
        const int ux2 = static_cast<int>(u.x) + other->right;
        const int uy1 = static_cast<int>(u.y) - other->up;
        const int uy2 = static_cast<int>(u.y) + other->down;
        if (x1 <= ux2 && x2 >= ux1 && y1 <= uy2 && y2 >= uy1) return true;
    }
    return false;
}

// 建筑正常规则：碰撞盒覆盖的瓦片必须可行走，且不与其它已知建筑相交。
// 未知类型不额外限制，避免误拒绝。
bool BuildingFits(const MapDocument& map, u16 type, int tx, int ty) {
    const UnitBox* box = FindUnitBox(type);
    if (!box) return true;
    const int cx = tx * kTilePixels;
    const int cy = ty * kTilePixels + 16;
    const int x1 = cx - box->left;
    const int x2 = cx + box->right;
    const int y1 = cy - box->up;
    const int y2 = cy + box->down;
    const int tx0 = x1 / kTilePixels;
    const int tx1 = x2 / kTilePixels;
    const int ty0 = y1 / kTilePixels;
    const int ty1 = y2 / kTilePixels;
    if (tx0 < 0 || ty0 < 0 || tx1 >= map.width() || ty1 >= map.height()) return false;
    for (int y = ty0; y <= ty1; y++) {
        for (int x = tx0; x <= tx1; x++) {
            if (!map.IsWalkableTile(x, y)) return false;
        }
    }
    for (const UnitRecord& u : map.units()) {
        if (u.type == 214) continue;           // 出生点标记不阻挡建筑
        if (IsAirUnitType(u.type)) continue;   // 空中单位不阻挡建筑
        const UnitBox* other = FindUnitBox(u.type);
        if (!other) continue;
        const int ux1 = static_cast<int>(u.x) - other->left;
        const int ux2 = static_cast<int>(u.x) + other->right;
        const int uy1 = static_cast<int>(u.y) - other->up;
        const int uy2 = static_cast<int>(u.y) + other->down;
        if (x1 <= ux2 && x2 >= ux1 && y1 <= uy2 && y2 >= uy1) return false;
    }
    return true;
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

// 找到 target 子菜单在父菜单中的位置（用于解除整个 POPUP 的 GRAYED）
bool FindSubmenuPosition(HMENU menu, HMENU target, HMENU& parent, int& position) {
    if (!menu) return false;
    const int count = ::GetMenuItemCount(menu);
    for (int i = 0; i < count; i++) {
        HMENU sub = ::GetSubMenu(menu, i);
        if (!sub) continue;
        if (sub == target) {
            parent = menu;
            position = i;
            return true;
        }
        if (FindSubmenuPosition(sub, target, parent, position)) return true;
    }
    return false;
}

// 枚举本程序的全部主窗口（含其他进程实例），用于排列/最小化
BOOL CALLBACK CollectAppWindows(HWND hwnd, LPARAM param) {
    wchar_t cls[64] = L"";
    if (::GetClassNameW(hwnd, cls, 64) == 0) return TRUE;
    if (::lstrcmpW(cls, kWindowClass) != 0) return TRUE;
    if (!::IsWindowVisible(hwnd)) return TRUE;
    reinterpret_cast<std::vector<HWND>*>(param)->push_back(hwnd);
    return TRUE;
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

// 向下 / 向上整除（正确处理负余数）
int FloorDivI(int a, int b) {
    int q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0)) q--;
    return q;
}
int CeilDivI(int a, int b) {
    int q = a / b;
    if (a % b != 0 && (a < 0) == (b < 0)) q++;
    return q;
}

struct PixelRectI {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// 按描绘模式把拖拽起止像素换成 MRGN 开区间矩形（right/bottom 不含）
PixelRectI BuildLocationRect(LocationDrawMode mode, int px0, int py0, int px1, int py1,
                             int cellX, int cellY, int offX, int offY, int mapW, int mapH) {
    PixelRectI rc;
    if (mode == LocationDrawMode::kAnywhere) {
        rc.left = std::min(px0, px1);
        rc.top = std::min(py0, py1);
        rc.right = std::max(px0, px1) + 1;
        rc.bottom = std::max(py0, py1) + 1;
    } else {
        int cx = mode == LocationDrawMode::kNeat ? kTilePixels : cellX;
        int cy = mode == LocationDrawMode::kNeat ? kTilePixels : cellY;
        const int ox = mode == LocationDrawMode::kNeat ? 0 : offX;
        const int oy = mode == LocationDrawMode::kNeat ? 0 : offY;
        if (cx <= 0) cx = kTilePixels;
        if (cy <= 0) cy = kTilePixels;
        rc.left = ox + FloorDivI(std::min(px0, px1) - ox, cx) * cx;
        rc.top = oy + FloorDivI(std::min(py0, py1) - oy, cy) * cy;
        rc.right = ox + CeilDivI(std::max(px0, px1) - ox, cx) * cx;
        rc.bottom = oy + CeilDivI(std::max(py0, py1) - oy, cy) * cy;
        if (rc.right <= rc.left) rc.right = rc.left + cx;
        if (rc.bottom <= rc.top) rc.bottom = rc.top + cy;
    }
    rc.left = std::clamp(rc.left, 0, mapW);
    rc.top = std::clamp(rc.top, 0, mapH);
    rc.right = std::clamp(rc.right, 0, mapW);
    rc.bottom = std::clamp(rc.bottom, 0, mapH);
    if (rc.left >= mapW) rc.left = std::max(0, mapW - 1);
    if (rc.top >= mapH) rc.top = std::max(0, mapH - 1);
    if (rc.right <= rc.left) rc.right = std::min(mapW, rc.left + 1);
    if (rc.bottom <= rc.top) rc.bottom = std::min(mapH, rc.top + 1);
    if (rc.right <= rc.left || rc.bottom <= rc.top) return PixelRectI();
    return rc;
}
}  // namespace

MainWindow::ClipboardData& MainWindow::SharedClipboard() {
    static ClipboardData data;
    return data;
}

bool MainWindow::TranslateAccelForMessage(MSG& msg) {
    for (MainWindow* win : LiveWindows()) {
        if (win->hwnd_ == msg.hwnd && win->TranslateAccel(msg)) return true;
    }
    return false;
}

int MainWindow::LiveWindowCount() {
    return static_cast<int>(LiveWindows().size());
}

bool MainWindow::Create(HINSTANCE hInstance) {    hInstance_ = hInstance;
    brushPattern_.SetSingle(brushTile_, L"单格");
    renderer_.SetTileset(&map_.tileset());
    history_.SetLimit(static_cast<std::size_t>(AppSettings::Instance().undoLimit()));
    soundEnabled_ = AppSettings::Instance().soundEnabled();

    // 恢复上次会话的视图状态
    ReloadViewState();

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
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = ::CreateWindowExW(0, kWindowClass, kWindowTitle,
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1180, 825,
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
                                  kDebugImageSizeAlways,
                                  kDebugClearDoodads, kDebugClearUnits, kDebugClearDuplicates,
                                  kDebugCompare, kDebugUnitConflict};
        for (int id : kEnableIds) {
            if (HMENU menu = FindMenuContaining(bar, id)) {
                ::EnableMenuItem(menu, id, MF_BYCOMMAND | MF_ENABLED);
            }
        }
        // 原程序没有 MPQ 文件管理入口（167 标题是版本信息），追加到「文件」子菜单
        if (HMENU file = ::GetSubMenu(bar, 0)) {
            ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(file, MF_STRING, kMpqFileManager, L"MPQ 文件管理...");
        }
        // 「调试」菜单追加本工程的崖壁自动铺设
        if (HMENU debug = ::GetSubMenu(bar, 9)) {
            ::AppendMenuW(debug, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(debug, MF_STRING, kAutoEdgeCliffs, L"自动铺设崖壁");
        }
        // 「触发」菜单追加只读文本视图
        if (HMENU trig = ::GetSubMenu(bar, 6)) {
            ::AppendMenuW(trig, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(trig, MF_STRING, kTriggerTextView, L"文本视图...");
        }
        // 原资源把「字体大小」整个 POPUP 灰掉了，这里解除
        if (HMENU fonts = FindMenuContaining(bar, kOutputFont8)) {
            HMENU parent = nullptr;
            int position = 0;
            if (FindSubmenuPosition(bar, fonts, parent, position) && parent) {
                ::EnableMenuItem(parent, static_cast<UINT>(position),
                                 MF_BYPOSITION | MF_ENABLED);
            }
        }
    }
    SyncMenuChecks();

    CreateSceneTree();
    CreateChrome();
    UpdateTitle();
    ::ShowWindow(hwnd_, AppSettings::Instance().startMaximized() ? SW_MAXIMIZE : SW_SHOW);
    ::UpdateWindow(hwnd_);
    ::SetTimer(hwnd_, 1, 60000, nullptr);   // 每分钟检查一次自动备份
    ::SetTimer(hwnd_, 2, 120, nullptr);     // 颜色循环 / 死亡效果动画

    if (AppSettings::Instance().autoCheckUpdate()) {
        Output(2, L"自动检查更新：已是最新版本（SCMD Plus 3.0.0.0）");
    }
    return true;
}

bool MainWindow::TranslateAccel(MSG& msg) const {
    return hAccel_ && ::TranslateAcceleratorW(hwnd_, hAccel_, &msg) != 0;
}

// 从 AppSettings 恢复视图状态（启动时与无头回归重置时共用）
void MainWindow::ReloadViewState() {
    const ViewState& view = AppSettings::Instance().view();
    pxPerTile_ = std::clamp(view.zoomPx, 1, 128);
    gridEnabled_ = view.gridEnabled;
    gridCellX_ = std::clamp(view.gridCellX, 1, 256);
    gridCellY_ = std::clamp(view.gridCellY, 1, 256);
    staticGridEnabled_ = view.staticGridEnabled;
    staticGridCellX_ = std::clamp(view.staticGridCellX, 1, 256);
    staticGridCellY_ = std::clamp(view.staticGridCellY, 1, 256);
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
    SyncMenuChecks();
    LayoutSceneTree();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
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

// “地图载入最大值”（155-1129）：勾选时开图自动适配缩放，否则沿用保存的缩放
int MainWindow::LoadZoom() const {
    if (AppSettings::Instance().loadFitZoom()) return FitZoom();
    return std::clamp(AppSettings::Instance().view().zoomPx, 1, 128);
}

void MainWindow::ApplyZoomPercent(int percent) {
    if (!map_.IsOpen()) {
        BeepWarning();
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
    if (!RebuildView(px)) BeepWarning();
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
    bool rendered = false;
    try {
        rendered = renderer_.Render(map_.tiles(), map_.width(), map_.height(), pxPerTile, image_);
    } catch (...) {
        rendered = false;
    }
    if (!rendered) {
        image_.pixels.clear();
        image_.width = image_.height = 0;
        return false;
    }

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
    const RECT view = MapViewRect();
    const int viewW = std::max(0, static_cast<int>(view.right - view.left));
    const int viewH = std::max(0, static_cast<int>(view.bottom - view.top));
    const int maxX = std::max(0, image_.width - viewW);
    const int maxY = std::max(0, image_.height - viewH);
    scrollX_ = std::min(scrollX_, maxX);
    scrollY_ = std::min(scrollY_, maxY);

    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, image_.width - 1);
    si.nPage = static_cast<UINT>(std::max(0, viewW));
    si.nPos = scrollX_;
    if (hScroll_) {
        ::SetScrollInfo(hScroll_, SB_CTL, &si, TRUE);
        ::EnableWindow(hScroll_, maxX > 0);
    }
    si.nMax = std::max(0, image_.height - 1);
    si.nPage = static_cast<UINT>(std::max(0, viewH));
    si.nPos = scrollY_;
    if (vScroll_) {
        ::SetScrollInfo(vScroll_, SB_CTL, &si, TRUE);
        ::EnableWindow(vScroll_, maxY > 0);
    }
}

void MainWindow::CreateChrome() {
    if (!hwnd_) return;
    if (!hScroll_) {
        hScroll_ = ::CreateWindowExW(
            0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_HORZ, 0, 0, 10, 10, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHScrollId)), hInstance_, nullptr);
    }
    if (!vScroll_) {
        vScroll_ = ::CreateWindowExW(
            0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_VERT, 0, 0, 10, 10, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVScrollId)), hInstance_, nullptr);
    }
    if (!statusBar_) {
        statusBar_ = ::CreateWindowExW(
            0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 10, 10,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusBarId)), hInstance_,
            nullptr);
    }
    LayoutChrome();
    UpdateStatusBar();
}

void MainWindow::LayoutChrome() {
    if (!hwnd_) return;
    if (statusBar_) {
        const RECT st = StatusRect();
        ::SetWindowPos(statusBar_, nullptr, st.left, st.top, st.right - st.left,
                       st.bottom - st.top, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rc;
        ::GetClientRect(hwnd_, &rc);
        const int w = std::max(0L, rc.right);
        int edges[5] = {w * 16 / 100, w * 26 / 100, w * 44 / 100, w * 54 / 100, w};
        for (int i = 1; i < 5; i++) {
            if (edges[i] < edges[i - 1]) edges[i] = edges[i - 1];
        }
        ::SendMessageW(statusBar_, SB_SETPARTS, 5, reinterpret_cast<LPARAM>(edges));
    }
    if (hScroll_) {
        const RECT r = HScrollRect();
        ::SetWindowPos(hScroll_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (vScroll_) {
        const RECT r = VScrollRect();
        ::SetWindowPos(vScroll_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::UpdateStatusBar() {
    if (!statusBar_) return;
    wchar_t coords[96] = L"";
    if (map_.IsOpen() && cursorTx_ >= 0 && cursorTy_ >= 0) {
        ::wsprintfW(coords, L"%d, %d (%d,%d)", cursorTx_ * kTilePixels, cursorTy_ * kTilePixels,
                    cursorTx_, cursorTy_);
    }
    const wchar_t* layers[] = {L"地形", L"装饰物", L"迷雾", L"Locations", L"单位", L"SP单位"};
    const wchar_t* layer = layers[std::clamp(static_cast<int>(layer_), 0, 5)];

    wchar_t player[128] = L"";
    wchar_t force[64] = L"";
    wchar_t mapInfo[160] = L"";
    if (map_.IsOpen()) {
        const PlayerSlot& p = map_.players().player(0);
        ::wsprintfW(player, L"Player 1: %s %s", PlayerColorName(p.color),
                    PlayerRaceName(p.race));
        ::wsprintfW(force, L"Force %u", static_cast<unsigned>(p.force) + 1);
        const unsigned era = map_.era() < 8 ? map_.era() : 0;
        ::wsprintfW(mapInfo, L"%dx%d  ERA %u  %s", map_.width(), map_.height(), era,
                    kEraNamesStatus[era]);
    }
    ::SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(coords));
    ::SendMessageW(statusBar_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(layer));
    ::SendMessageW(statusBar_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(player));
    ::SendMessageW(statusBar_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(force));
    ::SendMessageW(statusBar_, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(mapInfo));
}

int MainWindow::LogHeight() const {
    const int h = OutputPanelHeight();
    if (h <= 0 || !hwnd_) return 0;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const int content = std::max(0, static_cast<int>(rc.bottom - rc.top) - kStatusBarHeight);
    return std::min(h, content / 3);
}

RECT MainWindow::MapViewRect() const {
    RECT rc = {0, 0, 0, 0};
    if (!hwnd_) return rc;
    ::GetClientRect(hwnd_, &rc);
    const int logH = LogHeight();
    const int sbW = ::GetSystemMetrics(SM_CXVSCROLL);
    const int sbH = ::GetSystemMetrics(SM_CYHSCROLL);
    const int maxSide = std::max(0, static_cast<int>(rc.right - rc.left)) / 3;
    const int sideW = std::min(kSidebarWidth, maxSide);
    RECT r = {sideW, 0, rc.right - sbW, rc.bottom - kStatusBarHeight - sbH - logH};
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

RECT MainWindow::SideRect() const {
    RECT side = {0, 0, 0, 0};
    if (!hwnd_) return side;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const RECT view = MapViewRect();
    side.left = 0;
    side.top = 0;
    side.right = view.left;
    side.bottom = rc.bottom - kStatusBarHeight;
    if (side.right < side.left) side.right = side.left;
    if (side.bottom < side.top) side.bottom = side.top;
    return side;
}

RECT MainWindow::LogRect() const {
    RECT r = {0, 0, 0, 0};
    if (!hwnd_) return r;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const RECT view = MapViewRect();
    const int sbW = ::GetSystemMetrics(SM_CXVSCROLL);
    const int sbH = ::GetSystemMetrics(SM_CYHSCROLL);
    r.left = view.left;
    r.right = rc.right - sbW;
    r.bottom = rc.bottom - kStatusBarHeight - sbH;
    r.top = view.bottom;
    return r;
}

RECT MainWindow::StatusRect() const {
    RECT r = {0, 0, 0, 0};
    if (!hwnd_) return r;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    r.left = 0;
    r.right = rc.right;
    r.bottom = rc.bottom;
    r.top = rc.bottom - kStatusBarHeight;
    return r;
}

RECT MainWindow::HScrollRect() const {
    RECT r = {0, 0, 0, 0};
    if (!hwnd_) return r;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const RECT view = MapViewRect();
    const int sbH = ::GetSystemMetrics(SM_CYHSCROLL);
    r.left = view.left;
    r.right = rc.right - ::GetSystemMetrics(SM_CXVSCROLL);
    r.bottom = rc.bottom - kStatusBarHeight;
    r.top = r.bottom - sbH;
    return r;
}

RECT MainWindow::VScrollRect() const {
    RECT r = {0, 0, 0, 0};
    if (!hwnd_) return r;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    const int sbW = ::GetSystemMetrics(SM_CXVSCROLL);
    r.left = rc.right - sbW;
    r.right = rc.right;
    r.top = 0;
    r.bottom = HScrollRect().top;
    return r;
}

RECT MainWindow::MinimapRect() const {
    RECT empty = {0, 0, 0, 0};
    if (hideMinimap_ || !map_.IsOpen() || pxPerTile_ <= 0) return empty;
    const RECT side = SideRect();
    const int sideW = static_cast<int>(side.right - side.left);
    if (sideW < 40) return empty;
    const int avail = std::min(sideW - 12, 180);
    if (avail < 32) return empty;
    int mw = avail;
    int mh = avail;
    if (map_.width() >= map_.height()) {
        mh = std::max(16, avail * map_.height() / map_.width());
    } else {
        mw = std::max(16, avail * map_.width() / map_.height());
    }
    const int left = static_cast<int>(side.left) + (sideW - mw) / 2;
    RECT r = {left, side.top + 6, left + mw, side.top + 6 + mh};
    return r;
}

RECT MainWindow::SceneTreeRect() const {
    const RECT side = SideRect();
    const RECT mm = MinimapRect();
    const int top = (mm.bottom > mm.top) ? static_cast<int>(mm.bottom) + 4
                                         : static_cast<int>(side.top) + 4;
    RECT r = {side.left + 4, top, side.right - 4, side.bottom - 4};
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

bool MainWindow::PointInMapView(int mx, int my) const {
    const RECT view = MapViewRect();
    return mx >= view.left && my >= view.top && mx < view.right && my < view.bottom;
}

bool MainWindow::OpenMap(const std::wstring& path) {    if (!map_.Open(path, tilesetDir_)) {
        Output(0, L"打开地图失败: " + FileNameOf(path));
        ::MessageBoxW(hwnd_, L"无法打开该地图文件。", kWindowTitle, MB_ICONWARNING);
        return false;
    }
    mapPath_ = path;
    fileName_ = FileNameOf(path);
    {
        wchar_t msg[256];
        ::wsprintfW(msg, L"打开地图: %ls  %dx%d", fileName_.c_str(), map_.width(), map_.height());
        Output(2, msg);
    }
    history_.Clear();
    editOpen_ = false;
    locationDrawing_ = false;
    ClearSelection();
    AppSettings::Instance().AddRecentMap(path);
    AppSettings::Instance().Save();
    UpdateTitle();
    RebuildView(LoadZoom());
    LayoutSceneTree();
    UpdateStatusBar();
    RefreshBrushPalette();
    RefreshDoodadPalette();
    RefreshSceneTree();
    CheckCompatibility();
    return true;
}

// 帮助菜单外链：交给系统默认浏览器；失败时写入输出面板
void MainWindow::OpenHelpUrl(const wchar_t* url) {
    const HINSTANCE r =
        ::ShellExecuteW(hwnd_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32) {
        Output(1, std::wstring(L"无法打开链接: ") + url);
    } else {
        Output(2, std::wstring(L"已打开链接: ") + url);
    }
}

// 163：删除越界/类型非法单位与 SP 单位、纠正装饰物、修正非法地点矩形
int RepairCompatibilityIssues(MapDocument& map) {
    if (!map.IsOpen()) return 0;
    const int w = map.width();
    const int h = map.height();
    if (w <= 0 || h <= 0) return 0;
    const u32 mapPxW = static_cast<u32>(w) * kTilePixels;
    const u32 mapPxH = static_cast<u32>(h) * kTilePixels;

    int fixed = map.RemoveInvalidUnits();
    fixed += map.mutableDoodads().FixErrors(w, h);
    {
        auto& units = map.mutableUnits();
        const std::size_t before = units.size();
        units.erase(std::remove_if(units.begin(), units.end(),
                                   [&](const UnitRecord& u) {
                                       return u.x >= mapPxW || u.y >= mapPxH || u.type >= 228;
                                   }),
                    units.end());
        fixed += static_cast<int>(before - units.size());
    }
    {
        auto& locs = map.mutableLocations().MutableItems();
        for (Location& l : locs) {
            if (l.right > l.left && l.bottom > l.top && l.right <= mapPxW &&
                l.bottom <= mapPxH) {
                continue;
            }
            if (l.left >= mapPxW) l.left = mapPxW > 32 ? mapPxW - 32 : 0;
            if (l.top >= mapPxH) l.top = mapPxH > 32 ? mapPxH - 32 : 0;
            if (l.right <= l.left) l.right = std::min<u32>(l.left + 32, mapPxW);
            if (l.bottom <= l.top) l.bottom = std::min<u32>(l.top + 32, mapPxH);
            fixed++;
        }
    }
    {
        auto& sprites = map.mutableSprites().MutableItems();
        const std::size_t before = sprites.size();
        sprites.erase(std::remove_if(sprites.begin(), sprites.end(),
                                     [&](const SpriteRecord& s) {
                                         return s.x >= mapPxW || s.y >= mapPxH;
                                     }),
                      sprites.end());
        fixed += static_cast<int>(before - sprites.size());
    }
    if (fixed > 0) map.MarkDirty();
    return fixed;
}

// 163：开图后检测兼容性问题；有问题才弹列表，「保存」自动修复可修复项
void MainWindow::CheckCompatibility() {
    if (!map_.IsOpen()) return;
    const int w = map_.width();
    const int h = map_.height();
    if (w <= 0 || h <= 0) return;
    const u32 mapPxW = static_cast<u32>(w) * kTilePixels;
    const u32 mapPxH = static_cast<u32>(h) * kTilePixels;

    int outUnits = 0;
    int badTypeUnits = 0;
    for (const UnitRecord& u : map_.units()) {
        if (u.x >= mapPxW || u.y >= mapPxH) outUnits++;
        if (u.type >= 228) badTypeUnits++;
    }
    int badLocations = 0;
    for (const Location& l : map_.locations().Items()) {
        if (l.right <= l.left || l.bottom <= l.top || l.right > mapPxW || l.bottom > mapPxH) {
            badLocations++;
        }
    }
    int badDoodads = 0;
    for (const Doodad& d : map_.doodads().Items()) {
        if (d.x >= mapPxW || d.y >= mapPxH) badDoodads++;
    }
    int badSprites = 0;
    for (const SpriteRecord& s : map_.sprites().Items()) {
        if (s.x >= mapPxW || s.y >= mapPxH) badSprites++;
    }

    std::vector<std::wstring> issues;
    wchar_t line[192];
    if (outUnits > 0) {
        ::wsprintfW(line, L"单位坐标越界 %d 个（点「保存」删除）", outUnits);
        issues.push_back(line);
    }
    if (badTypeUnits > 0) {
        ::wsprintfW(line, L"单位类型超出 units.dat 范围（>=228）%d 个（点「保存」删除）",
                    badTypeUnits);
        issues.push_back(line);
    }
    if (badLocations > 0) {
        ::wsprintfW(line, L"地点范围异常 %d 个（点「保存」修正为 1 格）", badLocations);
        issues.push_back(line);
    }
    if (badDoodads > 0) {
        ::wsprintfW(line, L"装饰物坐标越界 %d 个（点「保存」纠正）", badDoodads);
        issues.push_back(line);
    }
    if (badSprites > 0) {
        ::wsprintfW(line, L"SP单位坐标越界 %d 个（点「保存」删除）", badSprites);
        issues.push_back(line);
    }
    const Buffer& tiles = map_.tiles();
    const std::size_t expectTiles =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 2u;
    if (tiles.size() < expectTiles) {
        ::wsprintfW(line, L"地形数据不足：%u B（应为 %u B）",
                    static_cast<unsigned>(tiles.size()),
                    static_cast<unsigned>(expectTiles));
        issues.push_back(line);
    }
    const AppSettings& settings = AppSettings::Instance();
    if (settings.maxUnits() > 0 &&
        map_.units().size() > static_cast<std::size_t>(settings.maxUnits())) {
        ::wsprintfW(line, L"单位数量 %u 超过「最多单位」上限 %d（仅提示）",
                    static_cast<unsigned>(map_.units().size()), settings.maxUnits());
        issues.push_back(line);
    }
    if (settings.maxLocations() > 0 &&
        map_.locations().Count() > static_cast<std::size_t>(settings.maxLocations())) {
        ::wsprintfW(line, L"地点数量 %u 超过「Location编号」上限 %d（仅提示）",
                    static_cast<unsigned>(map_.locations().Count()),
                    settings.maxLocations());
        issues.push_back(line);
    }

    if (issues.empty()) {
        Log(L"[compat] no issues");
        return;
    }
    for (const std::wstring& issue : issues) Output(1, L"[兼容性] " + issue);
    if (suppressDialogs_) return;

    SeedCompatIssues(issues);
    ShowRecoveredDialog(IDD_DLG_163, hwnd_);
    if (LastDialogResult() != IDOK) {
        Output(2, L"[兼容性] 已忽略，未修改地图");
        return;
    }

    // 自动修复：删除越界/类型非法单位与 SP 单位、纠正装饰物、修正地点矩形
    BeginEdit();
    const int fixed = RepairCompatibilityIssues(map_);
    if (fixed > 0) {
        CommitEdit(L"兼容性修复");
    } else {
        editOpen_ = false;
    }
    UpdateTitle();
    RefreshSceneTree();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
    Output(2, L"[兼容性] 已修复 " + std::to_wstring(fixed) + L" 项");
}

void MainWindow::NewMap() {
    if (!MaybeSave()) return;
    ShowRecoveredDialog(IDD_DLG_169, hwnd_);
    if (LastDialogResult() != IDOK) return;
    const MapSizeParams& size = LastMapSize();
    if (!map_.CreateNew(size.width, size.height, static_cast<u16>(size.era), tilesetDir_,
                        size.triggerPreset)) {
        return;
    }
    {
        wchar_t msg[128];
        ::wsprintfW(msg, L"新建地图: %dx%d", size.width, size.height);
        Output(2, msg);
    }
    mapPath_.clear();
    fileName_.clear();
    history_.Clear();
    editOpen_ = false;
    locationDrawing_ = false;
    ClearSelection();
    UpdateTitle();
    RebuildView(LoadZoom());
    LayoutSceneTree();
    UpdateStatusBar();
    RefreshBrushPalette();
    RefreshDoodadPalette();
    RefreshSceneTree();
}

void MainWindow::CloseMap() {
    if (!MaybeSave()) return;
    if (map_.IsOpen()) Output(2, L"关闭地图: " + fileName_);
    map_.Close();
    mapPath_.clear();
    fileName_.clear();
    history_.Clear();
    editOpen_ = false;
    locationDrawing_ = false;
    ClearSelection();
    ReleaseBackBuffer();
    UpdateTitle();
    LayoutSceneTree();
    UpdateStatusBar();
    RefreshBrushPalette();
    RefreshDoodadPalette();
    RefreshSceneTree();
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
        Output(0, L"保存地图失败: " + FileNameOf(path));
        ::MessageBoxW(hwnd_, L"保存地图失败。", kWindowTitle, MB_ICONWARNING);
        return false;
    }
    mapPath_ = path;
    fileName_ = FileNameOf(path);
    {
        wchar_t msg[256];
        ::wsprintfW(msg, L"保存地图: %ls  (%ls)", fileName_.c_str(),
                    saveAs ? L"另存为" : L"覆盖");
        Output(2, msg);
    }
    lastBackupTick_ = ::GetTickCount64();
    RefreshSceneTree();
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
        BeepWarning();
        return;
    }
    ResizeParams seed;
    seed.width = map_.width();
    seed.height = map_.height();
    seed.offsetX = 0;
    seed.offsetY = 0;
    seed.smoothEdges = false;
    seed.currentEra = static_cast<int>(map_.era());
    seed.era = -1;
    SeedResizeParams(seed);

    ShowRecoveredDialog(IDD_DLG_175, hwnd_);
    if (LastDialogResult() != IDOK) return;

    const ResizeParams& p = LastResizeParams();
    if (!map_.Resize(p.width, p.height, p.offsetX, p.offsetY, p.smoothEdges)) {
        ::MessageBoxW(hwnd_, L"重设地图大小失败。", kWindowTitle, MB_ICONWARNING);
        return;
    }
    if (p.era >= 0 && p.era != static_cast<int>(map_.era())) {
        map_.SetEra(static_cast<u16>(p.era));
    }
    history_.Clear();     // 旧快照与新尺寸不兼容
    editOpen_ = false;
    ClearSelection();
    RebuildView(LoadZoom());
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
        BeepWarning();
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
    // UNIT +26 是激活状态位（bit0 隐形 / bit1 钻地 / bit2 升起 / bit3 幻影 / bit4 无敌）
    seed.flags = unit.queueCount & 0x001Fu;
    seed.ammo = unit.stackCount;                  // +24 机库/载弹数量
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
    target.stackCount = static_cast<u16>(props.ammo);
    target.queueCount = static_cast<u16>((target.queueCount & ~0x001Fu) |
                                         (props.flags & 0x001Fu));
    // 勾选的状态同时标记为“可应用”，否则隐形/钻地等在该单位上会被游戏忽略
    target.validState = static_cast<u16>(target.validState | (props.flags & 0x001Fu));
    map_.MarkDirty();
    CommitEdit(L"修改单位属性");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::EditLocation(int index) {
    if (!map_.IsOpen()) {
        BeepWarning();
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
        target.left = static_cast<u32>(r.left);
        target.top = static_cast<u32>(r.top);
        target.right = static_cast<u32>(r.right);
        target.bottom = static_cast<u32>(r.bottom);
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
        BeepWarning();
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
    // 记住最后一次编辑的类型/玩家/标志，供 SP 层继续放置
    const auto& editedFinal = LastSpUnits();
    if (selected >= 0 && static_cast<std::size_t>(selected) < editedFinal.size()) {
        spPlaceType_ = editedFinal[static_cast<std::size_t>(selected)].type;
        spPlaceOwner_ = editedFinal[static_cast<std::size_t>(selected)].owner;
        spPlaceFlags_ = editedFinal[static_cast<std::size_t>(selected)].flags;
    }
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

int MainWindow::LocationIndexAtTile(int tx, int ty) const {
    // MRGN 坐标是像素，right/bottom 为开区间；用瓦片中心点做命中
    const int px = tx * kTilePixels + kTilePixels / 2;
    const int py = ty * kTilePixels + kTilePixels / 2;
    const auto& items = map_.locations().Items();
    for (int i = static_cast<int>(items.size()) - 1; i >= 0; i--) {
        const Location& loc = items[static_cast<std::size_t>(i)];
        if (px >= static_cast<int>(loc.left) && px < static_cast<int>(loc.right) &&
            py >= static_cast<int>(loc.top) && py < static_cast<int>(loc.bottom)) {
            return i;
        }
    }
    return -1;
}

void MainWindow::EditMapText(int stringIndex, bool richText) {
    if (!map_.IsOpen()) {
        BeepWarning();
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
        const RECT view = MapViewRect();
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        const int viewW = static_cast<int>(view.right - view.left);
        const int viewH = static_cast<int>(view.bottom - view.top);
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

void MainWindow::AutoPlaceCliffs() {
    if (!map_.IsOpen() || !map_.hasTileset()) {
        BeepWarning();
        return;
    }
    const int width = map_.width();
    const int height = map_.height();
    const std::size_t count = static_cast<std::size_t>(width) * height;
    const Buffer& tiles = map_.tiles();
    if (tiles.size() < count * 2) {
        BeepWarning();
        return;
    }

    // 可行走掩码：CV5 组号 → 通行位
    Buffer walkable(count, 0);
    for (std::size_t i = 0; i < count; i++) {
        const u16 value = static_cast<u16>(tiles[i * 2] | (tiles[i * 2 + 1] << 8));
        const u16 group = static_cast<u16>((value & 0x7FF0) >> 4);
        walkable[i] = map_.tileset().IsWalkable(group) ? 1 : 0;
    }

    BeginEdit();
    Buffer stamped = tiles;
    const int changed =
        EdgePlacer().StampOutward(walkable, width, height, map_.tileset(), stamped);
    if (changed <= 0) {
        editOpen_ = false;
        BeepWarning();
        return;
    }
    map_.mutableTiles() = std::move(stamped);
    map_.MarkDirty();
    CommitEdit(L"自动铺设崖壁");
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

// 同进程多窗口：新窗口拥有独立的文档/撤销栈，剪贴板为进程级共享
void MainWindow::NewWindow() {
    MainWindow* win = new MainWindow();
    win->SetSelfOwned(true);
    win->SetSuppressDialogs(suppressDialogs_);
    win->tilesetDir_ = tilesetDir_;
    if (!win->Create(hInstance_)) {
        delete win;
        Output(0, L"新建窗口失败");
        return;
    }
    win->LoadBrushFiles(AppSettings::Instance().brushFiles());
    Output(2, L"已新建窗口");
}

void MainWindow::MinimizeWindows() {
    std::vector<HWND> windows;
    ::EnumWindows(&CollectAppWindows, reinterpret_cast<LPARAM>(&windows));
    for (HWND w : windows) ::ShowWindow(w, SW_MINIMIZE);
    Output(2, L"窗口已最小化");
}

void MainWindow::ArrangeWindows(bool horizontal) {
    std::vector<HWND> windows;
    ::EnumWindows(&CollectAppWindows, reinterpret_cast<LPARAM>(&windows));
    if (windows.empty()) return;

    RECT work = {};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int totalW = static_cast<int>(work.right - work.left);
    const int totalH = static_cast<int>(work.bottom - work.top);
    const int count = static_cast<int>(windows.size());

    for (int i = 0; i < count; i++) {
        ::ShowWindow(windows[static_cast<std::size_t>(i)], SW_RESTORE);
        if (horizontal) {
            // 左右分栏
            const int w = totalW / count;
            const int x = static_cast<int>(work.left) + i * w;
            const int width = (i == count - 1) ? static_cast<int>(work.right) - x : w;
            ::MoveWindow(windows[static_cast<std::size_t>(i)], x, static_cast<int>(work.top),
                         width, totalH, TRUE);
        } else {
            // 上下分行
            const int h = totalH / count;
            const int y = static_cast<int>(work.top) + i * h;
            const int height = (i == count - 1) ? static_cast<int>(work.bottom) - y : h;
            ::MoveWindow(windows[static_cast<std::size_t>(i)], static_cast<int>(work.left), y,
                         totalW, height, TRUE);
        }
    }
    Output(2, horizontal ? L"窗口已水平排列" : L"窗口已垂直排列");
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
        BeepWarning();
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
        BeepWarning();
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
        BeepWarning();
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
        case kDoodadAnywhere:
            placeDoodadsAnywhere_ = !placeDoodadsAnywhere_;
            doodadPalette_.SetAllowAnywhere(placeDoodadsAnywhere_);
            break;
        case kClipboardDoodads: clipboardDoodads_ = !clipboardDoodads_; break;
        default:
            return;
    }
    SyncMenuChecks();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

bool MainWindow::PlaceUnitAtTile(int tx, int ty) {
    if (!map_.IsOpen()) return false;

    const bool force = placeUnitsAnywhere_ || placeBuildingsAnywhere_;
    if (autoConnectAddon_ && !force) SnapAddonToParent(map_, unitType_, tx, ty);
    if (mineralBaseDistance_ && !force && IsResourceType(unitType_) &&
        !ResourceNearBase(map_, tx, ty, kMineralBaseMaxDistance)) {
        Output(1, L"矿物/气矿距出生点超过 12 格，已拒绝放置");
        return false;
    }
    if (buildNormalRules_ && !force && IsBuildingType(unitType_) &&
        !BuildingFits(map_, unitType_, tx, ty)) {
        Output(1, L"建筑超出可行走区域或与其它建筑重叠，已拒绝放置");
        return false;
    }
    if (!force && UnitOverlapsSameLayer(map_, unitType_, tx, ty, allowGroundOverlap_,
                                       allowAirOverlap_)) {
        Output(1, L"与同层单位重叠，已拒绝放置（可在 选项→单位 中允许重叠）");
        return false;
    }
    // “最多单位”（155-1094）：0 = 不限制
    const int maxUnits = AppSettings::Instance().maxUnits();
    if (maxUnits > 0 && map_.units().size() >= static_cast<std::size_t>(maxUnits)) {
        Output(1, L"已达“最多单位”上限，已拒绝放置（可在 155 基本设置调整）");
        return false;
    }

    BeginEdit();
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

void MainWindow::SetDoodadPlacement(u16 type, u8 owner, bool disabled) {
    doodadType_ = type;
    doodadOwner_ = owner;
    doodadDisabled_ = disabled;
}

void MainWindow::SetDoodadAnywhere(bool allow) {
    placeDoodadsAnywhere_ = allow;
    doodadPalette_.SetAllowAnywhere(allow);
    SyncMenuChecks();
}

bool MainWindow::PlaceDoodadAtTile(int tx, int ty) {
    if (!map_.IsOpen()) return false;
    if (tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) return false;
    if (!placeDoodadsAnywhere_ && !map_.IsWalkableTile(tx, ty)) {
        Output(1, L"装饰物只能放在可通行地形上，已拒绝放置（可在 选项→装饰物 中允许任意放置）");
        return false;
    }

    BeginEdit();
    if (!map_.PlaceDoodad(tx, ty, doodadType_, doodadOwner_, doodadDisabled_,
                          placeDoodadsAnywhere_)) {
        editOpen_ = false;
        return false;
    }
    CommitEdit(L"放置装饰物");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

int MainWindow::RemoveDoodadsAtTile(int tx, int ty) {
    if (!map_.IsOpen()) return 0;
    BeginEdit();
    const int removed = map_.RemoveDoodadsAtTile(tx, ty);
    if (removed > 0) {
        CommitEdit(L"删除装饰物");
        UpdateTitle();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        editOpen_ = false;
    }
    return removed;
}

void MainWindow::SetLocationDrawMode(int mode) {
    locationDrawMode_ = static_cast<LocationDrawMode>(std::clamp(mode, 0, 2));
    SyncMenuChecks();
}

int MainWindow::AddLocationFromDrag(int px0, int py0, int px1, int py1) {
    if (!map_.IsOpen()) return -1;
    const PixelRectI r =
        BuildLocationRect(locationDrawMode_, px0, py0, px1, py1, gridCellX_, gridCellY_,
                          gridOffsetX_, gridOffsetY_, map_.width() * kTilePixels,
                          map_.height() * kTilePixels);
    if (r.right <= r.left || r.bottom <= r.top) return -1;
    // “Location编号”（155-1013）：地点数量上限，0 = 不限制
    const int maxLocations = AppSettings::Instance().maxLocations();
    if (maxLocations > 0 &&
        map_.locations().Count() >= static_cast<std::size_t>(maxLocations)) {
        Output(1, L"已达“Location编号”上限，已拒绝新建地点（可在 155 基本设置调整）");
        return -1;
    }

    Location loc;
    loc.left = static_cast<u32>(r.left);
    loc.top = static_cast<u32>(r.top);
    loc.right = static_cast<u32>(r.right);
    loc.bottom = static_cast<u32>(r.bottom);
    loc.stringId = 0;
    loc.flags = 0;

    BeginEdit();
    map_.mutableLocations().MutableItems().push_back(loc);
    map_.MarkDirty();
    CommitEdit(L"描绘地点");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return static_cast<int>(map_.locations().Count()) - 1;
}

void MainWindow::SetSpPlacement(u16 type, u8 owner, u16 flags) {
    spPlaceType_ = type;
    spPlaceOwner_ = owner;
    spPlaceFlags_ = flags;
}

void MainWindow::SetSpAlignGrid(bool align) {
    spAlignGrid_ = align;
    SyncMenuChecks();
}

bool MainWindow::PlaceSpriteAtPixels(int px, int py) {
    if (!map_.IsOpen()) return false;
    const int maxX = map_.width() * kTilePixels;
    const int maxY = map_.height() * kTilePixels;
    if (px < 0 || py < 0 || px >= maxX || py >= maxY) return false;
    // “SP单位”（155-1009）：0 = 不限制
    const int maxSpUnits = AppSettings::Instance().maxSpUnits();
    if (maxSpUnits > 0 &&
        map_.sprites().Count() >= static_cast<std::size_t>(maxSpUnits)) {
        Output(1, L"已达“SP单位”上限，已拒绝放置（可在 155 基本设置调整）");
        return false;
    }

    SpriteRecord s;
    s.type = spPlaceType_;
    s.owner = spPlaceOwner_;
    s.flags = spPlaceFlags_;
    if (spAlignGrid_) {
        s.x = static_cast<u16>((px / kTilePixels) * kTilePixels + kTilePixels / 2);
        s.y = static_cast<u16>((py / kTilePixels) * kTilePixels + kTilePixels / 2);
    } else {
        s.x = static_cast<u16>(px);
        s.y = static_cast<u16>(py);
    }

    BeginEdit();
    map_.mutableSprites().MutableItems().push_back(s);
    map_.MarkDirty();
    CommitEdit(L"放置SP单位");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

int MainWindow::RemoveSpriteAtTile(int tx, int ty) {
    const int index = SpriteIndexAtTile(tx, ty);
    if (index < 0) return 0;
    BeginEdit();
    auto& items = map_.mutableSprites().MutableItems();
    items.erase(items.begin() + index);
    map_.MarkDirty();
    CommitEdit(L"删除SP单位");
    UpdateTitle();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return 1;
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
        BeepWarning();
        return;
    }

    const int w = selX1_ - selX0_ + 1;
    const int h = selY1_ - selY0_ + 1;
    SharedClipboard().width = w;
    SharedClipboard().height = h;
    SharedClipboard().tiles.assign(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            SharedClipboard().tiles[static_cast<std::size_t>(y) * w + x] =
                map_.TileAt(selX0_ + x, selY0_ + y);
        }
    }

    SharedClipboard().units.clear();
    for (const UnitRecord& u : map_.units()) {
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        if (tx < selX0_ || tx > selX1_ || ty < selY0_ || ty > selY1_) continue;
        UnitRecord relative = u;
        relative.x = static_cast<u16>(u.x - selX0_ * 32);
        relative.y = static_cast<u16>(u.y - selY0_ * 32);
        SharedClipboard().units.push_back(relative);
    }

    SharedClipboard().doodads.clear();
    if (clipboardDoodads_) {
        for (const Doodad& d : map_.doodads().Items()) {
            const int tx = static_cast<int>(d.x) / 32;
            const int ty = static_cast<int>(d.y) / 32;
            if (tx < selX0_ || tx > selX1_ || ty < selY0_ || ty > selY1_) continue;
            Doodad relative = d;
            relative.x = static_cast<u16>(d.x - selX0_ * 32);
            relative.y = static_cast<u16>(d.y - selY0_ * 32);
            SharedClipboard().doodads.push_back(relative);
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
        static_cast<unsigned>(SharedClipboard().units.size()), cut ? 1 : 0);
    UpdateTitle();
}

void MainWindow::PasteClipboard() {
    if (!map_.IsOpen() || SharedClipboard().Empty()) {
        BeepWarning();
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
        tx = (map_.width() - SharedClipboard().width) / 2;
        ty = (map_.height() - SharedClipboard().height) / 2;
    }
    if (!PasteClipboardAt(tx, ty)) BeepWarning();
}

bool MainWindow::PasteClipboardAt(int tx, int ty) {
    if (!map_.IsOpen() || SharedClipboard().Empty()) return false;
    const int w = std::min(SharedClipboard().width, map_.width());
    const int h = std::min(SharedClipboard().height, map_.height());
    if (w <= 0 || h <= 0) return false;
    tx = std::clamp(tx, 0, map_.width() - w);
    ty = std::clamp(ty, 0, map_.height() - h);

    BeginEdit();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            map_.PaintTile(tx + x, ty + y, SharedClipboard().tiles[static_cast<std::size_t>(y) * SharedClipboard().width + x]);
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
    for (const UnitRecord& src : SharedClipboard().units) {
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

    if (!SharedClipboard().doodads.empty()) {
        map_.mutableDoodads().RemoveInTileRect(tx, ty, tx + w - 1, ty + h - 1);
        for (const Doodad& src : SharedClipboard().doodads) {
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
        static_cast<unsigned>(SharedClipboard().units.size()));
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

void MainWindow::LoadBrushFiles(const std::vector<std::wstring>& paths) {
    for (const std::wstring& path : paths) {
        std::vector<TerrainBrush> loaded;
        if (!LoadBrushes(path, loaded)) {
            Log(L"[brush] 无法读取 %s", path.c_str());
            continue;
        }
        for (TerrainBrush& brush : loaded) {
            const bool exists = std::any_of(
                customBrushes_.begin(), customBrushes_.end(), [&brush](const TerrainBrush& b) {
                    return b.width == brush.width && b.height == brush.height &&
                           _wcsicmp(b.name.c_str(), brush.name.c_str()) == 0;
                });
            if (!exists) customBrushes_.push_back(std::move(brush));
        }
    }
}

void MainWindow::ShowDoodadPalette() {
    if (!map_.IsOpen()) {
        Log(L"[doodad] 需要先打开地图");
        BeepWarning();
        return;
    }
    if (layer_ != EditLayer::kDoodads) SetLayer(EditLayer::kDoodads);
    if (!doodadPalette_.Show(hInstance_, hwnd_, &map_.doodads(), doodadType_, doodadOwner_,
                             doodadDisabled_, placeDoodadsAnywhere_)) {
        wchar_t msg[96];
        ::wsprintfW(msg, L"装饰物窗口创建失败（错误 %u）", doodadPalette_.lastError());
        Output(0, msg);
        BeepWarning();
    }
}

void MainWindow::RefreshDoodadPalette() {
    if (!doodadPalette_.IsOpen()) return;
    doodadPalette_.Refresh(map_.IsOpen() ? &map_.doodads() : nullptr, doodadType_, doodadOwner_,
                           doodadDisabled_);
}

void MainWindow::OpenCustomBrush() {
    if (!map_.IsOpen()) {
        BeepWarning();
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
            tool_ = Tool::kNone;   // 装饰物/迷雾/地点/SP：按层在鼠标事件中处理
            break;
    }
    if (layer == EditLayer::kLocations) showLocations_ = true;
    else if (layer == EditLayer::kDoodads) showDoodads_ = true;
    else if (layer == EditLayer::kFog) showFog_ = true;
    SyncMenuChecks();
    UpdateStatusBar();
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
    seed.staticEnabled = staticGridEnabled_;
    seed.staticCellX = staticGridCellX_;
    seed.staticCellY = staticGridCellY_;
    seed.staticOffsetX = staticGridOffsetX_;
    seed.staticOffsetY = staticGridOffsetY_;
    SeedGridSettings(seed);

    ShowRecoveredDialog(IDD_DLG_166, hwnd_);
    if (LastGridSettingsAccepted()) {
        const GridSettings& s = LastGridSettings();
        gridEnabled_ = s.enabled;
        gridCellX_ = s.cellX;
        gridCellY_ = s.cellY;
        gridOffsetX_ = s.offsetX;
        gridOffsetY_ = s.offsetY;
        staticGridEnabled_ = s.staticEnabled;
        staticGridCellX_ = s.staticCellX;
        staticGridCellY_ = s.staticCellY;
        staticGridOffsetX_ = s.staticOffsetX;
        staticGridOffsetY_ = s.staticOffsetY;
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
        case kDispFogExplored:
        case kLayerFogToggle: showFog_ = !showFog_; break;
        case kDispLocations: showLocations_ = !showLocations_; break;
        default: return;
    }
    SyncMenuChecks();
    LayoutSceneTree();
    UpdateStatusBar();
    ::InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::SyncMenuChecks() {
    HMENU bar = ::GetMenu(hwnd_);
    if (!bar) return;

    CheckOnly(bar, kLayerIds, 6, kLayerIds[static_cast<int>(layer_)]);
    CheckOnly(bar, kGridIds, 7, GridPresetOf(gridEnabled_, gridCellX_, gridCellY_));
    CheckOnly(bar, kGridColorIds, 7, gridColorCommand_);
    {
        static const int kLocModeIds[] = {kLocDrawNeat, kLocDrawGrid, kLocDrawAnywhere};
        CheckOnly(bar, kLocModeIds, 3, kLocModeIds[static_cast<int>(locationDrawMode_)]);
    }

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

    // 输出面板：显示 / 字体大小 / 提示详细度
    if (HMENU menu = FindMenuContaining(bar, kOutputShow)) {
        ::CheckMenuItem(menu, kOutputShow,
                        MF_BYCOMMAND | (outputVisible_ ? MF_CHECKED : MF_UNCHECKED));
    }
    {
        const int kFontIds[] = {kOutputFont8, kOutputFont10, kOutputFont12, kOutputFont14,
                                kOutputFont16};
        const int kFontPoints[] = {8, 10, 12, 14, 16};
        int checkedFont = kOutputFont12;
        for (int i = 0; i < 5; i++) {
            if (kFontPoints[i] == outputFontPt_) checkedFont = kFontIds[i];
        }
        CheckOnly(bar, kFontIds, 5, checkedFont);

        const int kVerbIds[] = {kOutputVerbOff, kOutputVerbSimple, kOutputVerbNormal,
                                kOutputVerbHigh, kOutputVerbDebug};
        CheckOnly(bar, kVerbIds, 5, kVerbIds[std::clamp(outputVerbosity_, 0, 4)]);
    }

    struct Toggle { int id; bool on; };
    const Toggle toggles[] = {
        {kDispAllColors, showAllColors_}, {kDispUnits, showUnits_},
        {kDispDoodads, showDoodads_}, {kDispSpUnits, showSpUnits_},
        {kDispZergGround, showZergGround_}, {kDispSpecialStates, showSpecialStates_},
        {kDispHideMinimap, hideMinimap_}, {kDispColorCycle, allowColorCycle_},
        {kDispDeathFx, showDeathFx_}, {kDispFog, showFog_},
        {kDispFogExplored, showFog_}, {kLayerFogToggle, showFog_},
        {kDispLocations, showLocations_},
        {kDebugEnable, debugMode_}, {kDebugOverlaps, showOverlapMarkers_},
        {kDebugCpu, showCpuTiming_}, {kDebugImageSize, showDebugImageSize_},
        {kDebugImageSizeAlways, showImageSizeAlways_},
        {kDebugCompare, showCompareData_}, {kDebugUnitConflict, showUnitConflict_},
        {kEnableSound, soundEnabled_},
        {kAutoCheckUpdate, AppSettings::Instance().autoCheckUpdate()},
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
        {kDoodadAnywhere, placeDoodadsAnywhere_},
        {kSpAlignGrid, spAlignGrid_},
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
    if ((!gridEnabled_ && !staticGridEnabled_ && !unitGrid) || !map_.IsOpen() ||
        pxPerTile_ <= 0) {
        return;
    }

    const int oldBk = ::SetBkMode(dc, TRANSPARENT);

    // 方格尺寸按源像素定义（32 = 一格），随缩放换算到屏幕；
    // 单位层“按方格显示”在网格关闭时使用 1 格（32 px）间距
    auto drawLines = [&](int srcCellX, int srcCellY, int offX, int offY, int penStyle) {
        const int cellX = srcCellX * pxPerTile_ / 32;
        const int cellY = srcCellY * pxPerTile_ / 32;
        if (cellX < 2 && cellY < 2) return;
        const HPEN pen = ::CreatePen(penStyle, 1, gridColor_);
        const HGDIOBJ oldPen = ::SelectObject(dc, pen);
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
        ::SelectObject(dc, oldPen);
        ::DeleteObject(pen);
    };

    // 第一套：主方格（虚线）；第二套：静态方格（实线），两套独立开关/尺寸/偏移
    if (gridEnabled_ || unitGrid) {
        drawLines(gridEnabled_ ? gridCellX_ : 32, gridEnabled_ ? gridCellY_ : 32,
                  gridEnabled_ ? gridOffsetX_ : 0, gridEnabled_ ? gridOffsetY_ : 0, PS_DOT);
    }
    if (staticGridEnabled_) {
        drawLines(staticGridCellX_, staticGridCellY_, staticGridOffsetX_, staticGridOffsetY_,
                  PS_SOLID);
    }

    ::SetBkMode(dc, oldBk);
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
        r.left = static_cast<int>(loc.left) * pxPerTile_ / kTilePixels - scrollX_;
        r.top = static_cast<int>(loc.top) * pxPerTile_ / kTilePixels - scrollY_;
        r.right = static_cast<int>(loc.right) * pxPerTile_ / kTilePixels - scrollX_;
        r.bottom = static_cast<int>(loc.bottom) * pxPerTile_ / kTilePixels - scrollY_;
        if (r.right <= r.left) r.right = r.left + 1;
        if (r.bottom <= r.top) r.bottom = r.top + 1;
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

void MainWindow::DrawLocationPreview(HDC dc, const RECT& client) const {
    if (!map_.IsOpen() || pxPerTile_ <= 0) return;
    const PixelRectI r =
        BuildLocationRect(locationDrawMode_, locDrawX0_, locDrawY0_, locDrawX1_, locDrawY1_,
                          gridCellX_, gridCellY_, gridOffsetX_, gridOffsetY_,
                          map_.width() * kTilePixels, map_.height() * kTilePixels);
    if (r.right <= r.left || r.bottom <= r.top) return;

    RECT screen;
    screen.left = r.left * pxPerTile_ / kTilePixels - scrollX_;
    screen.top = r.top * pxPerTile_ / kTilePixels - scrollY_;
    screen.right = r.right * pxPerTile_ / kTilePixels - scrollX_;
    screen.bottom = r.bottom * pxPerTile_ / kTilePixels - scrollY_;
    if (screen.right <= screen.left) screen.right = screen.left + 1;
    if (screen.bottom <= screen.top) screen.bottom = screen.top + 1;
    if (screen.right < client.left || screen.bottom < client.top ||
        screen.left > client.right || screen.top > client.bottom) {
        return;
    }

    const HPEN pen = ::CreatePen(PS_DOT, 1, RGB(0, 220, 90));
    const HGDIOBJ oldPen = ::SelectObject(dc, pen);
    const HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
    ::Rectangle(dc, screen.left, screen.top, screen.right, screen.bottom);
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
    if (!PointInMapView(mx, my)) return;   // 侧栏 / 日志区域不参与地图操作
    tx = (mx + scrollX_) / pxPerTile_;
    ty = (my + scrollY_) / pxPerTile_;
    if (tx < 0 || ty < 0 || tx >= map_.width() || ty >= map_.height()) {
        tx = ty = -1;
    }
}

void MainWindow::PixelCoordsFromClient(int mx, int my, int& px, int& py) const {
    px = 0;
    py = 0;
    if (pxPerTile_ <= 0 || !map_.IsOpen()) return;
    if (!PointInMapView(mx, my)) return;
    px = (mx + scrollX_) * kTilePixels / pxPerTile_;
    py = (my + scrollY_) * kTilePixels / pxPerTile_;
    px = std::clamp(px, 0, map_.width() * kTilePixels);
    py = std::clamp(py, 0, map_.height() * kTilePixels);
}

void MainWindow::BeginLocationDraw(int mx, int my) {
    if (!map_.IsOpen()) return;
    int px = 0, py = 0;
    PixelCoordsFromClient(mx, my, px, py);
    locationDrawing_ = true;
    locDrawX0_ = locDrawX1_ = px;
    locDrawY0_ = locDrawY1_ = py;
    BeginEdit();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::UpdateLocationDraw(int mx, int my) {
    if (!locationDrawing_) return;
    int px = 0, py = 0;
    PixelCoordsFromClient(mx, my, px, py);
    locDrawX1_ = px;
    locDrawY1_ = py;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::FinishLocationDraw() {
    if (!locationDrawing_) return;
    locationDrawing_ = false;
    if (!map_.IsOpen()) {
        editOpen_ = false;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    AddLocationFromDrag(locDrawX0_, locDrawY0_, locDrawX1_, locDrawY1_);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
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
    if (PointInOutputPanel(mx, my)) return;
    if (ClickMinimapAt(mx, my)) return;

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

    if (layer_ == EditLayer::kDoodads && tool_ == Tool::kNone) {
        if (!PlaceDoodadAtTile(tx, ty)) BeepWarning();
        cursorTx_ = tx;
        cursorTy_ = ty;
        UpdateTitle();
        return;
    }

    if (layer_ == EditLayer::kLocations && tool_ == Tool::kNone) {
        // 点在已有地点上留给双击（打开 171），空白处开始拖拽描绘
        if (LocationIndexAtTile(tx, ty) < 0) BeginLocationDraw(mx, my);
        cursorTx_ = tx;
        cursorTy_ = ty;
        UpdateTitle();
        return;
    }

    if (layer_ == EditLayer::kSpUnits && tool_ == Tool::kNone) {
        // 点在已有标记上留给双击（打开 164）
        if (SpriteIndexAtTile(tx, ty) < 0) {
            int px = 0, py = 0;
            PixelCoordsFromClient(mx, my, px, py);
            if (!PlaceSpriteAtPixels(px, py)) BeepWarning();
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
                BeepWarning();   // 坐标越界或不可行走
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
    if (PointInOutputPanel(mx, my)) return;
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
    UpdateStatusBar();
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
    if (leftDown && locationDrawing_) {
        UpdateLocationDraw(mx, my);
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
    if (PointInOutputPanel(mx, my)) return;
    int tx = -1, ty = -1;
    TileCoordsFromClient(mx, my, tx, ty);
    if (tx < 0) return;
    if (layer_ == EditLayer::kDoodads) {
        if (RemoveDoodadsAtTile(tx, ty) == 0) BeepWarning();
        return;
    }
    if (layer_ == EditLayer::kSpUnits) {
        if (RemoveSpriteAtTile(tx, ty) == 0) BeepWarning();
        return;
    }
    if (tool_ == Tool::kSelect) {
        ClearSelection();
        return;
    }
    BeginEdit();
    if (RemoveUnitsAtTileWithFx(tx, ty) > 0) {
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
    if (vk == VK_ESCAPE && locationDrawing_) {
        locationDrawing_ = false;
        editOpen_ = false;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
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
                if (RemoveUnitsAtTileWithFx(cursorTx_, cursorTy_) > 0) {
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

void MainWindow::DrawCreep(HDC dc, const RECT& client) const {
    if (!map_.IsOpen() || !map_.hasTileset()) return;
    const Tileset& ts = map_.tileset();
    if (!ts.HasCreep()) return;

    // CV5 里的 creep 图案是 4×4 槽位（空槽不画）
    u16 tileValue[16] = {};
    bool hasSlot[16] = {};
    int slotCount = 0;
    for (int i = 0; i < 16; i++) {
        if (ts.CreepTile(i, tileValue[i])) {
            hasSlot[i] = true;
            slotCount++;
        }
    }
    if (slotCount == 0) return;

    const Buffer& wpe = ts.palette();
    if (wpe.size() < 256 * 4) return;

    // 预解码每个槽位的 32×32 BGRA 图块
    std::vector<Buffer> blocks(16);
    for (int i = 0; i < 16; i++) {
        if (!hasSlot[i]) continue;
        u8 index[1024] = {};
        if (!ts.TileBlock(tileValue[i], index)) continue;
        blocks[i].resize(32 * 32 * 4);
        for (int p = 0; p < 1024; p++) {
            const u8* rgb = wpe.data() + static_cast<std::size_t>(index[p]) * 4;
            blocks[i][p * 4 + 0] = rgb[2];
            blocks[i][p * 4 + 1] = rgb[1];
            blocks[i][p * 4 + 2] = rgb[0];
            blocks[i][p * 4 + 3] = 0xFF;
        }
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 32;
    bi.bmiHeader.biHeight = -32;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    const int oldMode = ::SetStretchBltMode(dc, COLORONCOLOR);
    for (const auto& u : map_.units()) {
        // 产生虫族地皮的建筑：基地三形态 / 地刺 / 孢子 / 下沉
        const u16 type = u.type;
        if (type != 131 && type != 132 && type != 133 && type != 143 && type != 144 &&
            type != 145) {
            continue;
        }
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        const int originX = tx - 2;
        const int originY = ty - 3;
        for (int slot = 0; slot < 16; slot++) {
            if (!hasSlot[slot] || blocks[slot].empty()) continue;
            const int mx = originX + (slot % 4);
            const int my = originY + (slot / 4);
            if (mx < 0 || my < 0 || mx >= map_.width() || my >= map_.height()) continue;
            const int dx = mx * pxPerTile_ - scrollX_;
            const int dy = my * pxPerTile_ - scrollY_;
            if (dx + pxPerTile_ <= client.left || dx >= client.right ||
                dy + pxPerTile_ <= client.top || dy >= client.bottom) {
                continue;
            }
            ::StretchDIBits(dc, dx, dy, pxPerTile_, pxPerTile_, 0, 0, 32, 32,
                            blocks[slot].data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        }
    }
    ::SetStretchBltMode(dc, oldMode);
}

void MainWindow::DrawUnits(HDC dc, const RECT& client) const {
    if (!map_.IsOpen()) return;

    // 显示选项 → 特殊状态：非默认 validState 的单位加黄色圆环
    HPEN specialPen = nullptr;
    HGDIOBJ oldPen = nullptr;
    HGDIOBJ oldBrush = nullptr;
    if (showSpecialStates_) {
        specialPen = ::CreatePen(PS_SOLID, 1, RGB(255, 230, 0));
        oldPen = ::SelectObject(dc, specialPen);
        oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
    }

    for (const auto& u : map_.units()) {
        const int tx = static_cast<int>(u.x) / 32;
        const int ty = static_cast<int>(u.y) / 32;
        const int sx = tx * pxPerTile_ - scrollX_;
        const int sy = ty * pxPerTile_ - scrollY_;
        if (sx < -pxPerTile_ || sy < -pxPerTile_) continue;
        if (sx > client.right || sy > client.bottom) continue;

        const int size = std::max(3, pxPerTile_ / 2);
        const HBRUSH brush =
            ::CreateSolidBrush(CycleColor(UnitColorFor(u.type, showAllColors_), allowColorCycle_));
        RECT r;
        r.left = sx + (pxPerTile_ - size) / 2;
        r.top = sy + (pxPerTile_ - size) / 2;
        r.right = r.left + size;
        r.bottom = r.top + size;
        ::FillRect(dc, &r, brush);
        ::DeleteObject(brush);

        if (specialPen && u.validState != 0x0018) {
            ::Ellipse(dc, sx + 1, sy + 1, sx + pxPerTile_ - 1, sy + pxPerTile_ - 1);
        }
    }

    if (specialPen) {
        ::SelectObject(dc, oldBrush);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(specialPen);
    }
}

void MainWindow::DrawDeathMarks(HDC dc, const RECT& client) const {
    if (!showDeathFx_ || deathMarks_.empty() || pxPerTile_ <= 1) return;
    const ULONGLONG now = ::GetTickCount64();
    const HPEN pen = ::CreatePen(PS_SOLID, std::max(1, pxPerTile_ / 8), RGB(255, 60, 60));
    const HGDIOBJ old = ::SelectObject(dc, pen);
    const int pad = std::max(1, pxPerTile_ / 4);
    for (const DeathMark& m : deathMarks_) {
        if (now - m.tick > 500) continue;
        const int sx = m.tx * pxPerTile_ - scrollX_;
        const int sy = m.ty * pxPerTile_ - scrollY_;
        if (sx < -pxPerTile_ || sy < -pxPerTile_ || sx > client.right || sy > client.bottom) continue;
        ::MoveToEx(dc, sx + pad, sy + pad, nullptr);
        ::LineTo(dc, sx + pxPerTile_ - pad, sy + pxPerTile_ - pad);
        ::MoveToEx(dc, sx + pxPerTile_ - pad, sy + pad, nullptr);
        ::LineTo(dc, sx + pad, sy + pxPerTile_ - pad);
    }
    ::SelectObject(dc, old);
    ::DeleteObject(pen);
}

int MainWindow::RemoveUnitsAtTileWithFx(int tx, int ty) {    std::vector<std::pair<int, int>> removed;
    for (const auto& u : map_.units()) {
        const int ux = static_cast<int>(u.x) / kTilePixels;
        const int uy = static_cast<int>(u.y) / kTilePixels;
        if (ux == tx && uy == ty) removed.emplace_back(ux, uy);
    }
    const int count = map_.RemoveUnitsAtTile(tx, ty);
    if (count > 0 && showDeathFx_) {
        const ULONGLONG now = ::GetTickCount64();
        for (const auto& p : removed) deathMarks_.push_back({p.first, p.second, now});
    }
    return count;
}

void MainWindow::Output(int severity, const std::wstring& text) {
    if (outputVerbosity_ <= 0 || severity > outputVerbosity_) return;
    outputLines_.push_back({text, severity});
    constexpr std::size_t kMaxLines = 500;
    if (outputLines_.size() > kMaxLines) {
        outputLines_.erase(outputLines_.begin(),
                           outputLines_.begin() + (outputLines_.size() - kMaxLines));
    }
    if (outputVisible_ && hwnd_) ::InvalidateRect(hwnd_, nullptr, FALSE);
}

int MainWindow::OutputPanelHeight() const {
    if (!outputVisible_ || outputVerbosity_ <= 0) return 0;
    const int lineH = outputFontPt_ * 4 / 3;   // 96 DPI 近似字号
    return lineH * 7 + 14;
}

bool MainWindow::PointInOutputPanel(int mx, int my) const {
    const RECT r = LogRect();
    if (r.right <= r.left || r.bottom <= r.top) return false;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

void MainWindow::DrawOutputPanel(HDC dc, const RECT&) const {
    const RECT log = LogRect();
    if (log.right <= log.left || log.bottom <= log.top) return;
    const int top = static_cast<int>(log.top);
    const int panelH = static_cast<int>(log.bottom - log.top);

    ::FillRect(dc, &log, reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));

    HPEN border = ::CreatePen(PS_SOLID, 1, RGB(96, 96, 96));
    HGDIOBJ oldPen = ::SelectObject(dc, border);
    ::MoveToEx(dc, log.left, top, nullptr);
    ::LineTo(dc, log.right, top);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(border);

    const int dpi = ::GetDeviceCaps(dc, LOGPIXELSY);
    HFONT font = ::CreateFontW(-::MulDiv(outputFontPt_, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE,
                               FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               L"Microsoft YaHei");
    HGDIOBJ oldFont = ::SelectObject(dc, font);
    const int oldMode = ::SetBkMode(dc, TRANSPARENT);
    const int lineH = outputFontPt_ * dpi / 72 + 2;
    const int maxLines = std::max(0, (panelH - 8) / lineH);
    const std::size_t total = outputLines_.size();
    const std::size_t first =
        total > static_cast<std::size_t>(maxLines) ? total - static_cast<std::size_t>(maxLines) : 0;
    int y = top + 4;
    for (std::size_t i = first; i < total; i++) {
        const OutputLine& line = outputLines_[i];
        COLORREF color = RGB(220, 220, 220);
        switch (line.severity) {
            case 0: color = RGB(255, 120, 120); break;
            case 1: color = RGB(255, 200, 120); break;
            case 3: color = RGB(160, 220, 255); break;
            case 4: color = RGB(150, 150, 150); break;
            default: break;
        }
        ::SetTextColor(dc, color);
        RECT r = {log.left + 8, y, log.right - 8, y + lineH};
        ::DrawTextW(dc, line.text.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        y += lineH;
    }
    ::SetBkMode(dc, oldMode);
    ::SelectObject(dc, oldFont);
    ::DeleteObject(font);
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

bool MainWindow::ClickMinimapAt(int mx, int my) {
    if (!map_.IsOpen()) return false;
    const RECT mm = MinimapRect();
    if (mm.right <= mm.left || mm.bottom <= mm.top) return false;
    if (mx < mm.left || mx >= mm.right || my < mm.top || my >= mm.bottom) return false;

    const int mmW = static_cast<int>(mm.right - mm.left);
    const int mmH = static_cast<int>(mm.bottom - mm.top);
    const int tx = std::clamp((mx - static_cast<int>(mm.left)) * map_.width() / mmW,
                              0, map_.width() - 1);
    const int ty = std::clamp((my - static_cast<int>(mm.top)) * map_.height() / mmH,
                              0, map_.height() - 1);
    const RECT view = MapViewRect();
    const int viewW = static_cast<int>(view.right - view.left);
    const int viewH = static_cast<int>(view.bottom - view.top);
    scrollX_ = std::clamp(tx * pxPerTile_ - viewW / 2, 0,
                          std::max(0, image_.width - viewW));
    scrollY_ = std::clamp(ty * pxPerTile_ - viewH / 2, 0,
                          std::max(0, image_.height - viewH));
    UpdateScrollBars();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void MainWindow::DrawMinimap(HDC dc) const {
    const RECT mm = MinimapRect();
    if (mm.right <= mm.left) return;
    const int mx = mm.left;
    const int my = mm.top;
    const int mw = mm.right - mm.left;
    const int mh = mm.bottom - mm.top;

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
        HBRUSH b = ::CreateSolidBrush(CycleColor(UnitColorFor(u.type, showAllColors_), allowColorCycle_));
        ::FillRect(dc, &r, b);
        ::DeleteObject(b);
    }

    // 当前视口范围
    if (image_.width > 0 && image_.height > 0) {
        const RECT view = MapViewRect();
        const int viewW = static_cast<int>(view.right - view.left);
        const int viewH = static_cast<int>(view.bottom - view.top);
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
    // 默认不显示调试 HUD（与原版一致）；任一调试选项开启后才绘制
    if (!debugMode_ && !showCompareData_ && !showDebugImageSize_ && !showImageSizeAlways_) {
        return;
    }
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
    if (showDebugImageSize_ || showImageSizeAlways_) {
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

    const RECT view = MapViewRect();
    const int viewW = static_cast<int>(view.right - view.left);
    const int viewH = static_cast<int>(view.bottom - view.top);

    if (backBuffer_) {
        const HDC mem = ::CreateCompatibleDC(dc);
        if (mem) {
            const HGDIOBJ old = ::SelectObject(mem, backBuffer_);
            const int w = std::min<int>(viewW, image_.width - scrollX_);
            const int h = std::min<int>(viewH, image_.height - scrollY_);
            if (w > 0 && h > 0) {
                ::BitBlt(dc, view.left, view.top, w, h, mem, scrollX_, scrollY_, SRCCOPY);
            }
            ::SelectObject(mem, old);
            ::DeleteDC(mem);
        }
    }

    if (viewW > 0 && viewH > 0) {
        const int saved = ::SaveDC(dc);
        ::IntersectClipRect(dc, view.left, view.top, view.right, view.bottom);

        if (showZergGround_) DrawCreep(dc, view);
        if (showDoodads_) DrawDoodads(dc, view);
        if (showSpUnits_) DrawSprites(dc, view);
        if (showFog_) DrawFog(dc, view);
        DrawGrid(dc, view);
        if (showUnits_) DrawUnits(dc, view);
        if (showDeathFx_) DrawDeathMarks(dc, view);
        if (showOverlapMarkers_) DrawOverlapMarkers(dc, view);
        if (showUnitConflict_) DrawUnitConflictSizes(dc, view);
        if (showLocations_) DrawLocations(dc, view);
        if (locationDrawing_) DrawLocationPreview(dc, view);
        DrawSelection(dc, view);

        // 光标所在格高亮
        if (cursorTx_ >= 0) {
            const int sx = cursorTx_ * pxPerTile_ - scrollX_;
            const int sy = cursorTy_ * pxPerTile_ - scrollY_;
            RECT r = {sx, sy, sx + pxPerTile_, sy + pxPerTile_};
            ::DrawFocusRect(dc, &r);
        }

        DrawHud(dc);
        ::RestoreDC(dc, saved);
    }

    DrawSidebar(dc);
    DrawOutputPanel(dc, client);
}

// 左侧栏：上=微缩地图，下=场景树（场景树是 TreeView 子窗口，这里只画背景/分隔线）
void MainWindow::DrawSidebar(HDC dc) const {
    const RECT side = SideRect();
    if (side.right <= side.left || side.bottom <= side.top) return;

    HBRUSH back = ::CreateSolidBrush(RGB(30, 30, 30));
    ::FillRect(dc, &side, back);
    ::DeleteObject(back);

    HPEN border = ::CreatePen(PS_SOLID, 1, RGB(96, 96, 96));
    const HGDIOBJ oldPen = ::SelectObject(dc, border);
    ::MoveToEx(dc, side.right - 1, side.top, nullptr);
    ::LineTo(dc, side.right - 1, side.bottom);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(border);

    DrawMinimap(dc);
}

// ── 场景树（右侧栏下方）──
// lParam 编码：高 4 位 = 种类（1 单位 / 2 SP单位 / 3 装饰物 / 4 地点 / 5 触发），低 20 位 = 下标
namespace {
constexpr int kSceneKindShift = 20;
constexpr int kSceneKindMask = 0xF;
constexpr int kSceneIndexMask = 0xFFFFF;
constexpr int kSceneMaxChildren = 200;

LPARAM SceneItemData(int kind, int index) {
    return (static_cast<LPARAM>(kind) << kSceneKindShift) |
           (static_cast<LPARAM>(index) & kSceneIndexMask);
}

HTREEITEM InsertSceneItem(HWND tree, HTREEITEM parent, const wchar_t* text, LPARAM data) {
    TVINSERTSTRUCTW ins = {};
    ins.hParent = parent ? parent : TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = const_cast<wchar_t*>(text);
    ins.item.lParam = data;
    return TreeView_InsertItem(tree, &ins);
}
}  // namespace

void MainWindow::CreateSceneTree() {
    if (sceneTree_ || !hwnd_) return;
    sceneTree_ = ::CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
            TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
        0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSceneTreeId)), hInstance_, nullptr);
    LayoutSceneTree();
    RefreshSceneTree();
}

void MainWindow::LayoutSceneTree() {
    if (!sceneTree_) return;
    const RECT r = SceneTreeRect();
    const int w = std::max(0L, r.right - r.left);
    const int h = std::max(0L, r.bottom - r.top);
    ::SetWindowPos(sceneTree_, nullptr, r.left, r.top, w, h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::ShowWindow(sceneTree_, (w > 0 && h > 0) ? SW_SHOW : SW_HIDE);
}

void MainWindow::RefreshSceneTree() {
    if (!sceneTree_) return;
    ::SendMessageW(sceneTree_, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(sceneTree_);

    if (!map_.IsOpen()) {
        InsertSceneItem(sceneTree_, nullptr, L"（未打开地图）", 0);
    } else {
        wchar_t rootText[220];
        ::wsprintfW(rootText, L"%s  %dx%d  ERA %u",
                    fileName_.empty() ? L"未命名" : fileName_.c_str(), map_.width(),
                    map_.height(), static_cast<unsigned>(map_.era()));
        InsertSceneItem(sceneTree_, nullptr, rootText, 0);

        auto addGroup = [&](const wchar_t* name, int count, int kind,
                            const std::function<void(int, std::wstring&)>& describe) {
            wchar_t head[128];
            ::wsprintfW(head, L"%s (%d)", name, count);
            HTREEITEM group = InsertSceneItem(sceneTree_, nullptr, head, 0);
            const int shown = std::min(count, kSceneMaxChildren);
            for (int i = 0; i < shown; i++) {
                std::wstring text;
                describe(i, text);
                InsertSceneItem(sceneTree_, group, text.c_str(), SceneItemData(kind, i));
            }
            if (count > shown) {
                wchar_t more[64];
                ::wsprintfW(more, L"… 其余 %d 条", count - shown);
                InsertSceneItem(sceneTree_, group, more, 0);
            }
            return group;
        };

        addGroup(L"单位", static_cast<int>(map_.units().size()), 1,
                 [this](int i, std::wstring& out) {
                     const UnitRecord& u = map_.units()[static_cast<std::size_t>(i)];
                     wchar_t buf[128];
                     ::wsprintfW(buf, L"#%u 类型 %u @ 格 %d,%d  玩家 %u",
                                 static_cast<unsigned>(u.serial), static_cast<unsigned>(u.type),
                                 static_cast<int>(u.x) / kTilePixels,
                                 static_cast<int>(u.y) / kTilePixels,
                                 static_cast<unsigned>(u.owner));
                     out = buf;
                 });
        addGroup(L"SP单位", static_cast<int>(map_.sprites().Count()), 2,
                 [this](int i, std::wstring& out) {
                     const SpriteRecord& s = map_.sprites().Items()[static_cast<std::size_t>(i)];
                     wchar_t buf[128];
                     ::wsprintfW(buf, L"类型 %u @ 格 %d,%d  玩家 %u",
                                 static_cast<unsigned>(s.type), static_cast<int>(s.x) / kTilePixels,
                                 static_cast<int>(s.y) / kTilePixels,
                                 static_cast<unsigned>(s.owner));
                     out = buf;
                 });
        addGroup(L"装饰物", static_cast<int>(map_.doodads().Count()), 3,
                 [this](int i, std::wstring& out) {
                     const Doodad& d = map_.doodads().Items()[static_cast<std::size_t>(i)];
                     wchar_t buf[128];
                     ::wsprintfW(buf, L"类型 %u @ 格 %d,%d  玩家 %u",
                                 static_cast<unsigned>(d.type), static_cast<int>(d.x) / kTilePixels,
                                 static_cast<int>(d.y) / kTilePixels,
                                 static_cast<unsigned>(d.owner));
                     out = buf;
                 });
        addGroup(L"地点", static_cast<int>(map_.locations().Count()), 4,
                 [this](int i, std::wstring& out) {
                     const Location& l = map_.locations().Items()[static_cast<std::size_t>(i)];
                     std::wstring name;
                     if (l.stringId < map_.strings().Count()) {
                         name = Utf8ToWide(map_.strings().At(l.stringId));
                     }
                     if (name.empty()) name = L"（未命名）";
                     if (name.size() > 24) name = name.substr(0, 21) + L"…";
                     wchar_t buf[160];
                     ::wsprintfW(buf, L"#%d  %s  (%u,%u)-(%u,%u)", i, name.c_str(),
                                 static_cast<unsigned>(l.left), static_cast<unsigned>(l.top),
                                 static_cast<unsigned>(l.right), static_cast<unsigned>(l.bottom));
                     out = buf;
                 });
        addGroup(L"触发", static_cast<int>(map_.triggers().BlockCount()), 5,
                 [this](int i, std::wstring& out) {
                     const Trigger& t = map_.triggers().Items()[static_cast<std::size_t>(i)];
                     wchar_t buf[128];
                     ::wsprintfW(buf, L"触发 %d  [%d 条件 / %d 行动]%s", i, t.ConditionCount(),
                                 t.ActionCount(), (t.executionFlags & 0x8) ? L"  已禁用" : L"");
                     out = buf;
                 });

        // 玩家：仅显示占用情况，双击无跳转
        HTREEITEM players = InsertSceneItem(sceneTree_, nullptr, L"玩家", 0);
        for (int p = 0; p < 8; p++) {
            const bool active = map_.players().player(p).controller < 6;
            wchar_t buf[80];
            ::wsprintfW(buf, L"玩家 %d  %s", p + 1, active ? L"启用" : L"未使用");
            InsertSceneItem(sceneTree_, players, buf, 0);
        }
    }

    ::SendMessageW(sceneTree_, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(sceneTree_, nullptr, TRUE);
    LayoutSceneTree();
}

void MainWindow::OnSceneTreeDblClk() {
    if (!sceneTree_ || !map_.IsOpen()) return;
    TVITEMW item = {};
    item.mask = TVIF_PARAM;
    item.hItem = TreeView_GetSelection(sceneTree_);
    if (!item.hItem || !TreeView_GetItem(sceneTree_, &item)) return;
    const LPARAM data = item.lParam;
    if (data == 0) return;
    const int kind = static_cast<int>((data >> kSceneKindShift) & kSceneKindMask);
    const int index = static_cast<int>(data & kSceneIndexMask);

    auto centerOnTile = [this](int tx, int ty) {
        const RECT view = MapViewRect();
        const int viewW = static_cast<int>(view.right - view.left);
        const int viewH = static_cast<int>(view.bottom - view.top);
        scrollX_ = std::clamp(tx * pxPerTile_ - viewW / 2, 0,
                              std::max(0, image_.width - viewW));
        scrollY_ = std::clamp(ty * pxPerTile_ - viewH / 2, 0,
                              std::max(0, image_.height - viewH));
        UpdateScrollBars();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    };

    switch (kind) {
        case 1: {
            const auto& items = map_.units();
            if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return;
            const UnitRecord& u = items[static_cast<std::size_t>(index)];
            centerOnTile(static_cast<int>(u.x) / kTilePixels, static_cast<int>(u.y) / kTilePixels);
            break;
        }
        case 2: {
            const auto& items = map_.sprites().Items();
            if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return;
            const SpriteRecord& s = items[static_cast<std::size_t>(index)];
            centerOnTile(static_cast<int>(s.x) / kTilePixels, static_cast<int>(s.y) / kTilePixels);
            break;
        }
        case 3: {
            const auto& items = map_.doodads().Items();
            if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return;
            const Doodad& d = items[static_cast<std::size_t>(index)];
            centerOnTile(static_cast<int>(d.x) / kTilePixels, static_cast<int>(d.y) / kTilePixels);
            break;
        }
        case 4:
            if (index >= 0 && static_cast<std::size_t>(index) < map_.locations().Count()) {
                EditLocation(index);
            }
            break;
        case 5: {
            if (index < 0 || static_cast<std::size_t>(index) >= map_.triggers().BlockCount()) return;
            BeginEdit();
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_DLG_181, hwnd_);
            SetDialogDocument(nullptr);
            CommitEdit(L"编辑触发");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        default:
            break;
    }
}

void MainWindow::BeepWarning() const {
    if (soundEnabled_) ::MessageBeep(MB_ICONWARNING);
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
    RefreshSceneTree();
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
        BeepWarning();
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
                ::MessageBoxW(hwnd_, L"该地图文件已不存在，已从最近列表移除。", kWindowTitle,
                              MB_ICONWARNING);
                AppSettings::Instance().RemoveRecentMap(path);
                AppSettings::Instance().Save();
                break;
            }
            if (MaybeSave()) OpenMap(path);
            break;
        }
        case kMpqFileManager: {
            if (!map_.IsOpen()) {
                BeepWarning();
                break;
            }
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_DLG_167, hwnd_);
            SetDialogDocument(nullptr);
            if (map_.IsDirty()) {
                UpdateTitle();
                ::InvalidateRect(hwnd_, nullptr, FALSE);
            }
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
                if (RemoveUnitsAtTileWithFx(cursorTx_, cursorTy_) > 0) {
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
            if (!map_.IsOpen()) {
                BeepWarning();
                break;
            }
            BeginEdit();
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_DLG_172, hwnd_);
            SetDialogDocument(nullptr);
            CommitEdit(L"编辑单位属性");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
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
                BeepWarning();
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
                BeepWarning();
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
        case kDoodadAnywhere:
        case kClipboardDoodads:
            TogglePlacementRule(id);
            break;
        case kOptionsEditorSettings:
            ShowRecoveredDialog(IDD_DLG_155, hwnd_);
            break;
        case kAdvancedCheckUpdate:
            ShowRecoveredDialog(IDD_DLG_174, hwnd_);
            break;
        case kAutoCheckUpdate: {
            const bool enabled = !AppSettings::Instance().autoCheckUpdate();
            AppSettings::Instance().SetAutoCheckUpdate(enabled);
            AppSettings::Instance().Save();
            Output(2, enabled ? L"自动检查更新：已开启（当前无更新源）"
                              : L"自动检查更新：已关闭");
            SyncMenuChecks();
            break;
        }
        case kAdvancedBackup:
            ShowRecoveredDialog(IDD_DLG_173, hwnd_);
            break;
        case kAdvancedDefaultEditor:
            SetDefaultMapEditor();
            break;
        case kAdvancedShortcut:
            CreateStartMenuShortcut();
            break;
        case kEnableSound:
            soundEnabled_ = !soundEnabled_;
            AppSettings::Instance().SetSoundEnabled(soundEnabled_);
            AppSettings::Instance().Save();
            SyncMenuChecks();
            if (soundEnabled_) ::MessageBeep(MB_ICONASTERISK);
            break;
        // 选项 → 输出
        case kOutputShow:
            outputVisible_ = !outputVisible_;
            SyncMenuChecks();
            LayoutChrome();
            LayoutSceneTree();
            UpdateScrollBars();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kOutputFont8:
        case kOutputFont10:
        case kOutputFont12:
        case kOutputFont14:
        case kOutputFont16: {
            static const int kPoints[] = {8, 10, 12, 14, 16};
            outputFontPt_ = kPoints[id - kOutputFont8];
            SyncMenuChecks();
            LayoutChrome();
            LayoutSceneTree();
            UpdateScrollBars();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kOutputVerbOff:
        case kOutputVerbSimple:
        case kOutputVerbNormal:
        case kOutputVerbHigh:
        case kOutputVerbDebug: {
            static const int kLevels[] = {0, 1, 2, 3, 4};
            static const int kIds[] = {kOutputVerbOff, kOutputVerbSimple, kOutputVerbNormal,
                                       kOutputVerbHigh, kOutputVerbDebug};
            int level = 2;
            for (int i = 0; i < 5; i++) {
                if (kIds[i] == id) level = kLevels[i];
            }
            outputVerbosity_ = level;
            SyncMenuChecks();
            LayoutChrome();
            LayoutSceneTree();
            UpdateScrollBars();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kOutputClear:
            outputLines_.clear();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kWindowNew:
            NewWindow();
            break;
        case kWindowMinimize:
            MinimizeWindows();
            break;
        case kWindowTileHorizontal:
            ArrangeWindows(true);
            break;
        case kWindowTileVertical:
            ArrangeWindows(false);
            break;
        case kWindowClose:
            ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case kWindowTerrain:
            InvalidateRect(hwnd_, nullptr, FALSE);   // 地形视图即主视图，重绘即可
            break;
        case kWindowDoodads:
            ShowDoodadPalette();
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
                BeepWarning();
                break;
            }
            const bool cursorInside = cursorTx_ >= selX0_ && cursorTx_ <= selX1_ &&
                                      cursorTy_ >= selY0_ && cursorTy_ <= selY1_;
            const int tx = cursorInside ? cursorTx_ : SelectionCenterX();
            const int ty = cursorInside ? cursorTy_ : SelectionCenterY();
            if (OverlapSelectedUnitsAt(tx, ty) == 0) BeepWarning();
            break;
        }
        case kDeleteInvalidUnits:
            if (DeleteInvalidUnits() == 0) BeepWarning();
            break;
        case kFixDoodadErrors: {
            if (!map_.IsOpen()) {
                BeepWarning();
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
                BeepWarning();
            }
            break;
        }
        case kEditConvertTerrain: {
            if (!map_.IsOpen()) {
                BeepWarning();
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
                BeepWarning();
                break;
            }
            BeginEdit();
            const int removed = map_.mutableDoodads().RemoveInTileRect(x0, y0, x1, y1);
            if (removed > 0) {
                map_.MarkDirty();
                CommitEdit(L"转换地形");
            } else {
                editOpen_ = false;
                BeepWarning();
            }
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kRandomizeMinerals: {
            if (!map_.IsOpen()) {
                BeepWarning();
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
                BeepWarning();
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
        case kLocDrawNeat:
            SetLocationDrawMode(0);
            break;
        case kLocDrawGrid:
            SetLocationDrawMode(1);
            break;
        case kLocDrawAnywhere:
            SetLocationDrawMode(2);
            break;
        case kSpAlignGrid:
            spAlignGrid_ = !spAlignGrid_;
            SyncMenuChecks();
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
        case kDebugImageSizeAlways:
            showImageSizeAlways_ = !showImageSizeAlways_;
            SyncMenuChecks();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kDebugClearDoodads:
        case kDebugClearUnits:
        case kDebugClearDuplicates: {
            // 本工程不做增量缓存；清缓存等价于丢弃整图位图并重建
            const wchar_t* what = id == kDebugClearDoodads ? L"装饰物"
                                 : id == kDebugClearUnits ? L"单位" : L"重复";
            ReleaseBackBuffer();
            RebuildView(pxPerTile_);
            Output(3, std::wstring(L"已清除") + what + L"缓存并重建视图");
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
        case kAutoEdgeCliffs:
            AutoPlaceCliffs();
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
        case kDispFogExplored:
        case kLayerFogToggle:
        case kDispLocations:
            ToggleDisplay(id);
            break;
        case kLocRename: {
            if (!map_.IsOpen()) {
                BeepWarning();
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
                BeepWarning();
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
        case kTriggerTextView:
            if (!map_.IsOpen()) {
                BeepWarning();
                break;
            }
            BeginEdit();
            SetDialogDocument(&map_);
            ShowRecoveredDialog(IDD_TRIG_TEXT, hwnd_);
            SetDialogDocument(nullptr);
            CommitEdit(L"编辑触发文本");
            UpdateTitle();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case kIscriptGrpDebug:
            ShowRecoveredDialog(IDD_ISCRIPT, hwnd_);
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
        case kHelpHomepage:
            OpenHelpUrl(L"http://www.stormcoast-fortress.net/");
            break;
        case kHelpOnlineHelp:
            OpenHelpUrl(L"http://www.stormcoast-fortress.net/");
            break;
        case kHelpForum:
            OpenHelpUrl(L"http://www.stormcoast-fortress.net/");
            break;
        case kHelpReportBug:
            // 原版指向作者邮箱；本工程改为项目仓库的 issue 页
            OpenHelpUrl(L"https://github.com/coder-murphy/SCMapEditorPlus/issues");
            break;
        case kHelpDonate:
            ::MessageBoxW(hwnd_, L"本工程为免费开源工具，不接受捐款。", kWindowTitle,
                          MB_ICONINFORMATION);
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
        LiveWindows().push_back(self);
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (msg == WM_NCDESTROY) {
        auto* self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            auto& live = LiveWindows();
            live.erase(std::remove(live.begin(), live.end(), self), live.end());
            const bool last = live.empty();
            const bool owned = self->selfOwned_;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            const LRESULT result = ::DefWindowProcW(hwnd, msg, wParam, lParam);
            if (last) ::PostQuitMessage(0);
            if (owned) delete self;
            return result;
        }
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
        case DoodadPalette::kMsgParamsChanged:
            doodadType_ = doodadPalette_.type();
            doodadOwner_ = doodadPalette_.owner();
            doodadDisabled_ = doodadPalette_.disabled();
            return 0;
        case DoodadPalette::kMsgAllowAnywhere:
            placeDoodadsAnywhere_ = wParam != 0;
            SyncMenuChecks();
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
            if (wParam == 2) {
                const ULONGLONG now = ::GetTickCount64();
                const std::size_t before = deathMarks_.size();
                deathMarks_.erase(
                    std::remove_if(deathMarks_.begin(), deathMarks_.end(),
                                   [now](const DeathMark& m) { return now - m.tick > 500; }),
                    deathMarks_.end());
                const bool animating = allowColorCycle_ && showUnits_ && map_.IsOpen();
                if (animating || !deathMarks_.empty() || deathMarks_.size() != before) {
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return 0;
            }
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
            if (locationDrawing_) {
                FinishLocationDraw();
                UpdateTitle();
                return 0;
            }
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
            // 仅在左上地图视口内滚动才缩放；右栏/日志区滚轮不改变地图
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ::ScreenToClient(hwnd_, &pt);
            if (!PointInMapView(pt.x, pt.y)) return 0;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int next = NextZoom(pxPerTile_, delta > 0 ? 1 : -1);
            if (next != pxPerTile_ && map_.IsOpen()) RebuildView(next);
            return 0;
        }
        case WM_HSCROLL: {
            if (!hScroll_) return 0;
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            ::GetScrollInfo(hScroll_, SB_CTL, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINELEFT: pos -= 32; break;
                case SB_LINERIGHT: pos += 32; break;
                case SB_PAGELEFT: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGERIGHT: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            const int maxPos = std::max(0, si.nMax - static_cast<int>(si.nPage) + 1);
            scrollX_ = std::clamp(pos, 0, maxPos);
            si.fMask = SIF_POS;
            si.nPos = scrollX_;
            ::SetScrollInfo(hScroll_, SB_CTL, &si, TRUE);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_VSCROLL: {
            if (!vScroll_) return 0;
            SCROLLINFO si = {sizeof(si)};
            si.fMask = SIF_ALL;
            ::GetScrollInfo(vScroll_, SB_CTL, &si);
            int pos = si.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= 32; break;
                case SB_LINEDOWN: pos += 32; break;
                case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                default: break;
            }
            const int maxPos = std::max(0, si.nMax - static_cast<int>(si.nPage) + 1);
            scrollY_ = std::clamp(pos, 0, maxPos);
            si.fMask = SIF_POS;
            si.nPos = scrollY_;
            ::SetScrollInfo(vScroll_, SB_CTL, &si, TRUE);
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE:
            LayoutChrome();
            LayoutSceneTree();
            UpdateScrollBars();
            UpdateStatusBar();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_NOTIFY: {
            const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
            if (hdr && hdr->hwndFrom == sceneTree_ && hdr->code == NM_DBLCLK) {
                OnSceneTreeDblClk();
                return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            if (MaybeSave()) ::DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY: {
            // 保存视图状态（缩放/方格/编辑层/显示选项）；无头回归时跳过。
            // 同进程多窗口时只在最后一个窗口关闭时写回，避免互相覆盖。
            if (AppSettings::Instance().persistViewOnExit() &&
                LiveWindows().size() <= 1) {
                ViewState& view = AppSettings::Instance().mutableView();
                view.zoomPx = pxPerTile_;
                view.gridEnabled = gridEnabled_;
                view.gridCellX = gridCellX_;
                view.gridCellY = gridCellY_;
                view.staticGridEnabled = staticGridEnabled_;
                view.staticGridCellX = staticGridCellX_;
                view.staticGridCellY = staticGridCellY_;
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
                AppSettings::Instance().SetSoundEnabled(soundEnabled_);
                AppSettings::Instance().Save();
            }
            ReleaseBackBuffer();
            return 0;
        }
        default:
            return ::DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

}  // namespace scmd
