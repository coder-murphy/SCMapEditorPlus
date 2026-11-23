#include "app/app.h"

#include <shellapi.h>
#include <cwchar>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <queue>
#include <string>
#include <unordered_set>

#include "res/resource.h"
#include "app/app_settings.h"
#include "app/backups.h"
#include "ui/brush_palette.h"
#include "ui/dialog_registry.h"
#include "ui/dialogs.h"

#include "archive/mpq_archive.h"
#include "map/edit_history.h"
#include "map/map_document.h"
#include "map/map_switches.h"
#include "terrain/terrain_renderer.h"
#include "terrain/edge_placer.h"
#include "terrain/tileset_export.h"
#include "chk/chk_section.h"

#include <commctrl.h>

#include "common/log.h"

namespace scmd {

namespace {

// 调试开关：SCMDPlus.exe --dialog=155 直接打开该对话框（用于自动化验证）
int ParseDialogArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    int id = 0;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--dialog=", 9) == 0) {
                id = static_cast<int>(std::wcstol(argv[i] + 9, nullptr, 10));
            }
        }
        ::LocalFree(argv);
    }
    return id;
}

// 调试开关：SCMDPlus.exe --mpq=<地图路径> 只做容器读取自检
std::wstring ParseMpqArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--mpq=", 6) == 0) path = argv[i] + 6;
        }
        ::LocalFree(argv);
    }
    return path;
}

// 调试开关：SCMDPlus.exe --mpq=<档案> --mpq-extract=<条目> --extract-to=<输出>
// 仅用于逆向取数（如 arr\units.dat），不写 settings、不建窗口。
std::wstring ParseMpqExtractArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring entry;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--mpq-extract=", 14) == 0) entry = argv[i] + 14;
        }
        ::LocalFree(argv);
    }
    return entry;
}

std::wstring ParseExtractToArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--extract-to=", 13) == 0) path = argv[i] + 13;
        }
        ::LocalFree(argv);
    }
    return path;
}

// 调试开关：SCMDPlus.exe --open="<地图>" 启动后直接载入地图（窗口保持打开）
std::wstring ParseOpenArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--open=", 7) == 0) path = argv[i] + 7;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --settings-dir=<目录>：覆盖设置文件目录（默认 %APPDATA%\SCMDPlus，测试用）
std::wstring ParseSettingsDirArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring dir;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--settings-dir=", 15) == 0) dir = argv[i] + 15;
        }
        ::LocalFree(argv);
    }
    return dir;
}

// --recent-test：最近地图 / 设置持久化回归（配合 --settings-dir）
bool HasRecentTestArg() {
    int argc = 0;
    bool want = false;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], L"--recent-test") == 0) want = true;
        }
        ::LocalFree(argv);
    }
    return want;
}

// --backup-test：自动备份 / 恢复 / 清理回归（配合 --settings-dir）
bool HasBackupTestArg() {
    int argc = 0;
    bool want = false;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], L"--backup-test") == 0) want = true;
        }
        ::LocalFree(argv);
    }
    return want;
}

// 是否无头回归/工具模式（不保存交互视图状态）
bool IsHeadlessRun() {
    int argc = 0;
    bool headless = false;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            const wchar_t* a = argv[i];
            if (std::wcsstr(a, L"-test") != nullptr ||
                std::wcsncmp(a, L"--save-to=", 10) == 0 ||
                std::wcsncmp(a, L"--dump-render=", 14) == 0 ||
                std::wcsncmp(a, L"--dialog=", 9) == 0 ||
                std::wcsncmp(a, L"--extract-ts=", 13) == 0 ||
                std::wcsncmp(a, L"--mpq-extract=", 14) == 0 ||
                std::wcsncmp(a, L"--mpq=", 6) == 0) {
                headless = true;
            }
        }
        ::LocalFree(argv);
    }
    return headless;
}

// 通用开关参数（无值，如 --view-test）
bool HasSwitchArg(const wchar_t* name) {
    int argc = 0;
    bool want = false;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], name) == 0) want = true;
        }
        ::LocalFree(argv);
    }
    return want;
}

// --view-test：校验启动时从 settings.ini 恢复视图状态（--settings-dir 预置）
bool ItemChecked(HWND hwnd, int id);
void WriteDiag(const std::wstring& text);

int RunViewTest(HWND hwnd, const MainWindow& win) {
    const bool unitsLayer = ItemChecked(hwnd, 0x803D) && !ItemChecked(hwnd, 0x803A);
    const bool gridOff = ItemChecked(hwnd, 0x80D6);
    const bool unitsHidden = !ItemChecked(hwnd, 0x8018);
    const bool zoom16 = win.pxPerTile() == 16;

    wchar_t msg[256];
    ::wsprintfW(msg, L"view=1\nlayer=%d\ngrid=%d\nunitsHidden=%d\nzoom=%d\n",
                unitsLayer ? 1 : 0, gridOff ? 1 : 0, unitsHidden ? 1 : 0, zoom16 ? 1 : 0);
    WriteDiag(msg);
    return (unitsLayer && gridOff && unitsHidden && zoom16) ? 0 : 2;
}

// --ts=<目录>：地形集四件套所在目录
std::wstring ParseTsArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring dir;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--ts=", 5) == 0) dir = argv[i] + 5;
        }
        ::LocalFree(argv);
    }
    return dir;
}

// --game-dir=<目录>：星际安装目录（--extract-ts 使用）
std::wstring ParseGameDirArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring dir;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--game-dir=", 11) == 0) dir = argv[i] + 11;
        }
        ::LocalFree(argv);
    }
    return dir;
}

// --extract-ts=<输出目录>：从游戏 MPQ 导出 8 套地形集四件套
std::wstring ParseExtractTsArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring dir;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--extract-ts=", 13) == 0) dir = argv[i] + 13;
        }
        ::LocalFree(argv);
    }
    return dir;
}

// --dump-render=<bmp>：把当前地图渲染成 24 位 BMP（自动化比对用）
std::wstring ParseDumpArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--dump-render=", 14) == 0) path = argv[i] + 14;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --save-to=<输出路径>：配合 --open 打开后立刻另存（用于回归验证）
std::wstring ParseSaveToArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--save-to=", 10) == 0) path = argv[i] + 10;
        }
        ::LocalFree(argv);
    }
    return path;
}

// 前置声明（WriteDiag 定义在下方）
void WriteDiag(const std::wstring& text);

// --file-test=<输出scx>：文件菜单闭环回归（新建 -> 绘制 -> 保存 -> 重开 -> 关闭）
std::wstring ParseFileTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--file-test=", 12) == 0) path = argv[i] + 12;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --mpq-compress-test=<输出mpq>：压缩写出与读回闭环
std::wstring ParseMpqCompressTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--mpq-compress-test=", 20) == 0) {
                path = argv[i] + 20;
            }
        }
        ::LocalFree(argv);
    }
    return path;
}

// --edit-test=<输出scx>：无头编辑回归（刷地形 + 放/删单位 + 可行走校验）
std::wstring ParseEditTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--edit-test=", 12) == 0) path = argv[i] + 12;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --unit-param-test=<输出scx>：单位放置参数回归
std::wstring ParseUnitParamTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--unit-param-test=", 18) == 0) path = argv[i] + 18;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --clip-test=<输出scx>：框选/剪贴板/重叠/非法单位回归
std::wstring ParseClipTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--clip-test=", 12) == 0) path = argv[i] + 12;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --brush-pattern-test=<输出scx>：多格图案刷回归
std::wstring ParseBrushPatternTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--brush-pattern-test=", 21) == 0) path = argv[i] + 21;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --player-test=<输出scx>：玩家/势力模型回归
std::wstring ParsePlayerTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--player-test=", 14) == 0) path = argv[i] + 14;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --restriction-test=<输出scx>：升级/科技限制回归
std::wstring ParseRestrictionTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--restriction-test=", 19) == 0) path = argv[i] + 19;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --location-test=<输出scx>：Location 171 → MRGN 写回回归
std::wstring ParseLocationTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--location-test=", 16) == 0) path = argv[i] + 16;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --decor-test=<输出scx>：DD2 装饰物 / MASK 战争迷雾回归
std::wstring ParseDecorTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--decor-test=", 13) == 0) path = argv[i] + 13;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --sprite-test=<输出scx>：THG2 SP 单位回归
std::wstring ParseSpriteTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--sprite-test=", 14) == 0) path = argv[i] + 14;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --radar-test=<输出scx>：小雷达（Map Revealer）/ 迷雾开关回归
std::wstring ParseRadarTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--radar-test=", 13) == 0) path = argv[i] + 13;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --scenario-text-test=<输出scx>：声音 / 开关名 / 任务简报回归
std::wstring ParseScenarioTextTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--scenario-text-test=", 21) == 0) path = argv[i] + 21;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --map-tools-test=<输出scx>：转换地形 / 矿物外形随机回归
std::wstring ParseMapToolsTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--map-tools-test=", 17) == 0) path = argv[i] + 17;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --tileset-render-test=<输出目录>：8 套地形集逐套渲染回归
std::wstring ParseTilesetRenderTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--tileset-render-test=", 22) == 0) path = argv[i] + 22;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --brush-test：地形刷调色板回归（配合 --open / --ts）
bool HasBrushTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    bool want = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], L"--brush-test") == 0) want = true;
        }
        ::LocalFree(argv);
    }
    return want;
}

// 在给定格附近找一个可行走格；失败返回 false
bool FindWalkable(const MapDocument& doc, int startX, int startY, int& tx, int& ty) {
    for (int radius = 0; radius < 64; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                const int x = startX + dx;
                const int y = startY + dy;
                if (doc.IsWalkableTile(x, y)) { tx = x; ty = y; return true; }
            }
        }
    }
    return false;
}

bool FindBlocked(const MapDocument& doc, int startX, int startY, int& tx, int& ty) {
    for (int radius = 0; radius < 64; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                const int x = startX + dx;
                const int y = startY + dy;
                if (x < 0 || y < 0 || x >= doc.width() || y >= doc.height()) continue;
                if (!doc.IsWalkableTile(x, y)) { tx = x; ty = y; return true; }
            }
        }
    }
    return false;
}

int RunEditTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, ParseTsArg())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    const int baseUnits = static_cast<int>(doc.unitCount());
    const int cx = doc.width() / 2;
    const int cy = doc.height() / 2;

    // 1) 找一个可行走格与一个不可行走格
    int wx = -1, wy = -1, bx = -1, by = -1;
    const bool haveWalk = FindWalkable(doc, cx, cy, wx, wy);
    const bool haveBlocked = FindBlocked(doc, cx, cy, bx, by);

    // 2) 地形刷：把 (wx,wy) 及右侧 2 格刷成同值（取该格原值，保证可渲染）
    const u16 brush = doc.TileAt(wx, wy);
    const bool painted = doc.PaintRect(wx, wy, wx + 2, wy, brush);

    // 3) 放 3 个单位（矿物/气矿/出生点各一），再删掉 1 个
    int placed = 0;
    if (doc.PlaceUnit(wx, wy, 176, 11, 1500)) placed++;          // 矿物
    int gx = -1, gy = -1;
    if (FindWalkable(doc, wx + 4, wy + 4, gx, gy) && doc.PlaceUnit(gx, gy, 188, 11, 5000)) placed++;
    int sx = -1, sy = -1;
    if (FindWalkable(doc, wx - 4, wy - 4, sx, sy) && doc.PlaceUnit(sx, sy, 214, 0, 0)) placed++;

    // 4) 不可行走格必须拒绝
    const bool rejected = haveBlocked ? !doc.PlaceUnit(bx, by, 176, 11, 1500) : false;

    // 5) 删除 (wx,wy) 上的单位
    const int removed = doc.RemoveUnitsAtTile(wx, wy);

    const bool saved = doc.Save(outPath);

    // 6) 重新打开校验
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    int repaintMatches = 0;
    if (reopened) {
        for (int x = wx; x <= wx + 2; x++) {
            if (check.TileAt(x, wy) == brush) repaintMatches++;
        }
    }
    const int expectUnits = baseUnits + placed - removed;
    const bool countOk = reopened && static_cast<int>(check.unitCount()) == expectUnits;

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"open=OK\nbase=%d\nplaced=%d\nremoved=%d\nafter=%d\nexpected=%d\n"
        L"painted=%d\nrepaint=%d/3\nblockedRejected=%d\nsaved=%d\nreopened=%d\ncountOk=%d\n",
        baseUnits, placed, removed, static_cast<int>(doc.unitCount()), expectUnits,
        painted ? 1 : 0, repaintMatches, rejected ? 1 : 0, saved ? 1 : 0,
        reopened ? 1 : 0, countOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = painted && placed == 3 && rejected && removed == 1 && saved &&
                    reopened && repaintMatches == 3 && countOk;
    return ok ? 0 : 2;
}

// --unit-param-test=<输出scx>：单位放置参数回归
int RunUnitParamTest(MainWindow& win, const std::wstring& outPath) {
    MapDocument& doc = win.mutableMap();
    // 不加载地形集：所有格视为可行走，专注验证放置参数写回
    if (!doc.CreateNew(32, 32, 0, std::wstring())) {
        WriteDiag(L"create=FAIL\n");
        return 2;
    }

    win.SetUnitPlacement(214, 3, 0);              // 出生点，玩家 4
    const bool placedStart = win.PlaceUnitAtTile(5, 5);
    win.SetUnitPlacement(188, 11, 5000);          // 气矿，中立，资源 5000
    const bool placedGeyser = win.PlaceUnitAtTile(6, 6);

    // 附属单位自动连接：CC(106) 在 12,12，附属槽为 (16,13)-(17,14)
    win.SetUnitPlacement(106, 1, 0);
    const bool placedCC = win.PlaceUnitAtTile(12, 12);
    win.SetUnitPlacement(113, 1, 0);              // Factory 与 CC 重叠，应被拒绝
    const bool rejectedOverlap = !win.PlaceUnitAtTile(12, 12);
    win.SetUnitPlacement(107, 1, 0);              // Comsat Station
    const bool placedAddon = win.PlaceUnitAtTile(17, 14);

    // Control Tower(115) 应吸附到 Starport(114) 的 (15,21)-(16,22) 槽
    win.SetUnitPlacement(114, 1, 0);
    const bool placedStarport = win.PlaceUnitAtTile(12, 20);
    win.SetUnitPlacement(115, 1, 0);
    const bool placedTower = win.PlaceUnitAtTile(16, 22);

    // 地面重叠：默认拒绝同格，开启“允许地面单位重叠”后接受
    win.SetUnitPlacement(0, 1, 0);                // Marine
    const bool placedMarine = win.PlaceUnitAtTile(25, 5);
    const bool stackRejected = !win.PlaceUnitAtTile(25, 5);
    ::SendMessageW(win.hwnd(), WM_COMMAND, MAKEWPARAM(0x8015, 0), 0);
    const bool stackAllowed = win.PlaceUnitAtTile(25, 5);
    ::SendMessageW(win.hwnd(), WM_COMMAND, MAKEWPARAM(0x8015, 0), 0);

    // 空中重叠：默认拒绝空中同格；地面单位可与空中单位同格
    win.SetUnitPlacement(8, 1, 0);                // Wraith
    const bool placedWraith = win.PlaceUnitAtTile(25, 10);
    const bool airStackRejected = !win.PlaceUnitAtTile(25, 10);
    win.SetUnitPlacement(0, 1, 0);                // Marine
    const bool groundUnderAir = win.PlaceUnitAtTile(25, 10);

    // 矿物与基地距离限制：开启后远处矿物拒绝、近处允许
    ::SendMessageW(win.hwnd(), WM_COMMAND, MAKEWPARAM(0x80BF, 0), 0);
    win.SetUnitPlacement(176, 0, 0);
    const bool rejectedFar = !win.PlaceUnitAtTile(30, 30);
    const bool placedNearMineral = win.PlaceUnitAtTile(10, 5);
    ::SendMessageW(win.hwnd(), WM_COMMAND, MAKEWPARAM(0x80BF, 0), 0);

    const bool rejected = !win.PlaceUnitAtTile(-1, 0);
    const bool saved = doc.Save(outPath);

    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    bool startOk = false;
    bool geyserOk = false;
    bool addonOk = false;
    bool towerOk = false;
    bool mineralOk = false;
    if (reopened) {
        for (const UnitRecord& u : check.units()) {
            if (u.type == 214 && u.owner == 3) startOk = true;
            if (u.type == 188 && u.owner == 11 && u.resource == 5000) geyserOk = true;
            if (u.type == 107 && u.x == 16 * 32 && u.y == 13 * 32 + 16) addonOk = true;
            if (u.type == 115 && u.x == 15 * 32 && u.y == 21 * 32 + 16) towerOk = true;
            if (u.type == 176 && u.x == 10 * 32 && u.y == 5 * 32 + 16) mineralOk = true;
        }
    }

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"create=OK\nplaced=%d/%d\nrejected=%d\nsaved=%d\nreopened=%d\nunits=%u\n"
        L"startOk=%d\ngeyserOk=%d\ncc=%d\naddon=%d\nfarRejected=%d\nnearMineral=%d\n"
        L"addonOk=%d\ntower=%d/%d\ntowerOk=%d\nmineralOk=%d\noverlapRejected=%d\n"
        L"stack=%d/%d/%d\nair=%d/%d/%d\n",
        placedStart ? 1 : 0, placedGeyser ? 1 : 0, rejected ? 1 : 0, saved ? 1 : 0,
        reopened ? 1 : 0, static_cast<unsigned>(check.unitCount()),
        startOk ? 1 : 0, geyserOk ? 1 : 0, placedCC ? 1 : 0, placedAddon ? 1 : 0,
        rejectedFar ? 1 : 0, placedNearMineral ? 1 : 0, addonOk ? 1 : 0,
        placedStarport ? 1 : 0, placedTower ? 1 : 0, towerOk ? 1 : 0, mineralOk ? 1 : 0,
        rejectedOverlap ? 1 : 0, placedMarine ? 1 : 0, stackRejected ? 1 : 0,
        stackAllowed ? 1 : 0, placedWraith ? 1 : 0, airStackRejected ? 1 : 0,
        groundUnderAir ? 1 : 0);
    WriteDiag(msg);

    const bool ok = placedStart && placedGeyser && placedCC && placedAddon && placedStarport &&
                    placedTower && rejectedFar && placedNearMineral && rejected && saved &&
                    reopened && check.unitCount() == 11 && startOk && geyserOk && addonOk &&
                    towerOk && mineralOk && rejectedOverlap && placedMarine && stackRejected &&
                    stackAllowed && placedWraith && airStackRejected && groundUnderAir;
    return ok ? 0 : 2;
}

// --clip-test=<输出scx>：框选/剪贴板/重叠/非法单位回归
int RunClipTest(MainWindow& win, const std::wstring& outPath) {
    MapDocument& doc = win.mutableMap();
    if (!doc.CreateNew(16, 16, 0, std::wstring())) {
        WriteDiag(L"create=FAIL\n");
        return 2;
    }

    const u16 sample = 0x0040;
    doc.PaintTile(2, 2, sample);
    doc.PlaceUnit(2, 2, 214, 1, 0, true);
    doc.MarkDirty();

    win.SelectRect(2, 2, 3, 3);
    win.CopySelection(false);

    // 跨地图：关闭后新建 20x20，再粘贴
    doc.Close();
    win.ClearSelection();
    if (!doc.CreateNew(20, 20, 0, std::wstring())) {
        WriteDiag(L"recreate=FAIL\n");
        return 2;
    }
    const bool pasted = win.PasteClipboardAt(10, 10);
    const bool tileOk = doc.TileAt(10, 10) == sample;
    const bool unitOk = doc.UnitCountAtTile(10, 10) == 1;

    // 剪切目标格
    win.SelectRect(10, 10, 10, 10);
    win.CopySelection(true);
    const bool cutTileOk = doc.TileAt(10, 10) == 0;
    const bool cutUnitOk = doc.UnitCountAtTile(10, 10) == 0;

    // 再粘贴到 5,5
    const bool pasted2 = win.PasteClipboardAt(5, 5);
    const bool tile2Ok = doc.TileAt(5, 5) == sample;
    const bool unit2Ok = doc.UnitCountAtTile(5, 5) == 1;

    // 剪贴板包含装饰物（0x811C）
    doc.mutableDoodads().MutableItems().push_back(
        Doodad{1, static_cast<u16>(5 * 32), static_cast<u16>(5 * 32 + 16), 0, 0});
    win.SelectRect(5, 5, 5, 5);
    win.CopySelection(false);                         // 默认不含装饰物
    win.PasteClipboardAt(9, 9);
    const bool doodadPasteNo = doc.doodads().Count() == 1;
    doc.RemoveUnitsAtTile(9, 9);

    win.OnCommand(0x811C);                            // 打开“包括装饰物”
    win.SelectRect(5, 5, 5, 5);
    win.CopySelection(false);
    win.PasteClipboardAt(9, 9);
    bool doodadAt99 = false;
    for (const Doodad& d : doc.doodads().Items()) {
        if (d.x == 9 * 32 && d.y == 9 * 32 + 16) doodadAt99 = true;
    }
    const bool doodadPaste = doc.doodads().Count() == 2 && doodadAt99;
    doc.RemoveUnitsAtTile(9, 9);

    win.SelectRect(9, 9, 9, 9);
    win.CopySelection(true);                          // 剪切时同步删除装饰物
    const bool doodadCut = doc.doodads().Count() == 1;

    // 重叠所选单位
    doc.PlaceUnit(6, 6, 188, 11, 5000, true);
    win.SelectRect(5, 5, 6, 6);
    const int merged = win.OverlapSelectedUnitsAt(5, 5);
    const bool overlapOk = merged == 2 && doc.UnitCountAtTile(5, 5) == 2;

    // 删除非法单位：越界 + 重复 serial
    auto& units = doc.mutableUnits();
    UnitRecord outOfBounds = units.front();
    outOfBounds.serial = 12345;
    outOfBounds.x = static_cast<u16>(20 * 32);
    outOfBounds.y = 0;
    units.push_back(outOfBounds);
    const UnitRecord duplicate = units.front();
    units.push_back(duplicate);
    const int removed = win.DeleteInvalidUnits();
    const bool invalidOk = removed == 2 && doc.unitCount() == 2;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const bool reopenOk = reopened && check.unitCount() == 2 && check.TileAt(5, 5) == sample;

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"paste=%d\ntile=%d\nunit=%d\ncutTile=%d\ncutUnit=%d\npaste2=%d\ntile2=%d\nunit2=%d\n"
        L"doodadNo=%d\ndoodadPaste=%d\ndoodadCut=%d\n"
        L"overlap=%d\nremoved=%d\ninvalidOk=%d\nsaved=%d\nreopened=%d\nreopenOk=%d\n",
        pasted ? 1 : 0, tileOk ? 1 : 0, unitOk ? 1 : 0, cutTileOk ? 1 : 0, cutUnitOk ? 1 : 0,
        pasted2 ? 1 : 0, tile2Ok ? 1 : 0, unit2Ok ? 1 : 0, doodadPasteNo ? 1 : 0,
        doodadPaste ? 1 : 0, doodadCut ? 1 : 0, overlapOk ? 1 : 0, removed,
        invalidOk ? 1 : 0, saved ? 1 : 0, reopened ? 1 : 0, reopenOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = pasted && tileOk && unitOk && cutTileOk && cutUnitOk && pasted2 &&
                    tile2Ok && unit2Ok && doodadPasteNo && doodadPaste && doodadCut &&
                    overlapOk && invalidOk && saved && reopenOk;
    return ok ? 0 : 2;
}

// --brush-pattern-test=<输出scx>：多格图案刷绘制/撤销回归
int RunBrushPatternTest(MainWindow& win, const std::wstring& outPath) {
    MapDocument& doc = win.mutableMap();
    if (!doc.CreateNew(16, 16, 0, std::wstring())) {
        WriteDiag(L"create=FAIL\n");
        return 2;
    }

    TerrainBrush brush;
    brush.SetFromTiles(2, 2, {0x0040, 0x0041, 0x0042, 0x0043}, L"2x2");
    win.SetBrushPattern(brush);

    const bool painted = win.PaintBrushAt(5, 5);
    const bool tileOk = doc.TileAt(4, 4) == 0x0040 && doc.TileAt(5, 4) == 0x0041 &&
                        doc.TileAt(4, 5) == 0x0042 && doc.TileAt(5, 5) == 0x0043;

    const bool undoOk = win.Undo() && doc.TileAt(4, 4) == 0 && doc.TileAt(5, 5) == 0;
    const bool redoOk = win.Redo() && doc.TileAt(4, 4) == 0x0040 && doc.TileAt(5, 5) == 0x0043;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const bool reopenOk = reopened && check.TileAt(4, 4) == 0x0040 &&
                          check.TileAt(5, 4) == 0x0041 &&
                          check.TileAt(4, 5) == 0x0042 &&
                          check.TileAt(5, 5) == 0x0043;

    wchar_t msg[256];
    ::wsprintfW(msg,
                L"paint=%d\ntiles=%d\nundo=%d\nredo=%d\nsaved=%d\nreopened=%d\nreopenOk=%d\n",
                painted ? 1 : 0, tileOk ? 1 : 0, undoOk ? 1 : 0, redoOk ? 1 : 0,
                saved ? 1 : 0, reopened ? 1 : 0, reopenOk ? 1 : 0);
    WriteDiag(msg);
    return (painted && tileOk && undoOk && redoOk && saved && reopenOk) ? 0 : 2;
}

// --brush-test：地形刷调色板回归（枚举 / 去重 / 选中 / 窗口交互 / 绘制）
int RunBrushTest(const std::wstring& mapPath, const std::wstring& tsDir) {
    MapDocument doc;
    if (!doc.Open(mapPath, tsDir) || !doc.hasTileset()) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    const std::vector<BrushPalette::Entry> entries = BrushPalette::BuildEntries(doc.tileset());
    bool unique = !entries.empty();
    std::unordered_set<unsigned> seen;
    for (const BrushPalette::Entry& e : entries) {
        const unsigned key = (static_cast<unsigned>(e.group) << 16) | e.mega;
        if (!seen.insert(key).second) {
            unique = false;
            break;
        }
        if (doc.tileset().MegatileOf(e.value) != e.mega) {
            unique = false;
            break;
        }
    }

    BrushPalette palette;
    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    bool window = palette.Create(instance, nullptr);
    bool click = false;
    if (window) {
        palette.Show(instance, nullptr, &doc.tileset(),
                     entries.empty() ? 0 : entries.front().value);
        ::UpdateWindow(palette.hwnd());
        if (entries.size() >= 2) {
            ::SendMessageW(palette.hwnd(), WM_KEYDOWN, VK_RIGHT, 0);
            click = palette.selected() == entries[1].value;
            // 再点第一格，验证鼠标命中（y=64 位于顶部筛选栏下方的瓦片网格）
            ::SendMessageW(palette.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(24, 64));
            click = click && palette.selected() == entries.front().value;
        } else {
            click = true;
        }
        palette.Close();
        window = !palette.IsOpen();
    }

    bool painted = false;
    bool readback = false;
    if (!entries.empty()) {
        const u16 value = entries.front().value;
        painted = doc.PaintTile(0, 0, value);
        readback = doc.TileAt(0, 0) == value;
    }

    // 过滤：可走 / 阻挡 / 类型 / 组区间
    bool filterOk = false;
    if (!entries.empty()) {
        const Tileset& ts = doc.tileset();
        const int sampleType = ts.Type(entries.front().group);
        const int sampleGroup = entries.front().group;

        BrushPalette::Filter walkFilter;
        walkFilter.walkable = 1;
        const auto walkable = BrushPalette::BuildEntries(ts, walkFilter);
        bool walkOk = true;
        for (const auto& e : walkable) {
            if (!ts.IsWalkable(e.group)) walkOk = false;
        }

        BrushPalette::Filter blockFilter;
        blockFilter.walkable = 0;
        const auto blocked = BrushPalette::BuildEntries(ts, blockFilter);
        bool blockOk = true;
        for (const auto& e : blocked) {
            if (ts.IsWalkable(e.group)) blockOk = false;
        }

        BrushPalette::Filter typeFilter;
        typeFilter.type = sampleType;
        const auto typed = BrushPalette::BuildEntries(ts, typeFilter);
        bool typeOk = !typed.empty();
        for (const auto& e : typed) {
            if (ts.Type(e.group) != sampleType) typeOk = false;
        }

        BrushPalette::Filter groupFilter;
        groupFilter.groupMin = sampleGroup;
        groupFilter.groupMax = sampleGroup;
        const auto ranged = BrushPalette::BuildEntries(ts, groupFilter);
        bool groupOk = !ranged.empty();
        for (const auto& e : ranged) {
            if (e.group != sampleGroup) groupOk = false;
        }
        filterOk = walkOk && blockOk && typeOk && groupOk;
    }

    // .scb 文件往返
    bool fileOk = false;
    {
        std::vector<TerrainBrush> brushes;
        TerrainBrush brush;
        brush.SetFromTiles(2, 2, {0x0040, 0x0041, 0x0042, 0x0043}, L"测试刷子");
        brushes.push_back(brush);

        wchar_t path[MAX_PATH] = L"";
        ::GetTempPathW(MAX_PATH, path);
        ::lstrcatW(path, L"scmd_brush_test.scb");
        const bool savedFile = SaveBrushes(path, brushes);
        std::vector<TerrainBrush> loaded;
        const bool loadedFile = LoadBrushes(path, loaded);
        fileOk = savedFile && loadedFile && loaded.size() == 1 &&
                 loaded[0].name == L"测试刷子" && loaded[0].width == 2 &&
                 loaded[0].height == 2 && loaded[0].tiles == brush.tiles;
        ::DeleteFileW(path);
    }

    wchar_t msg[384];
    ::wsprintfW(msg, L"entries=%u unique=%d window=%d click=%d paint=%d readback=%d filters=%d file=%d\n",
                static_cast<unsigned>(entries.size()), unique ? 1 : 0, window ? 1 : 0,
                click ? 1 : 0, painted ? 1 : 0, readback ? 1 : 0,
                filterOk ? 1 : 0, fileOk ? 1 : 0);
    WriteDiag(msg);
    return (unique && window && click && painted && readback && filterOk && fileOk) ? 0 : 2;
}

int RunFileTest(const std::wstring& outPath) {
    MapDocument doc;
    const bool created = doc.CreateNew(32, 32, 0, ParseTsArg());
    const bool dirtyAfterNew = doc.IsDirty();
    const bool unnamed = doc.path().empty();

    const u16 brush = 0x0002;
    doc.PaintRect(1, 1, 4, 4, brush);
    const bool saved = doc.Save(outPath);
    const bool cleanAfterSave = !doc.IsDirty();
    const bool pathAfterSave = doc.path() == outPath;

    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    int painted = 0;
    if (reopened) {
        for (int y = 1; y <= 4; y++) {
            for (int x = 1; x <= 4; x++) {
                if (check.TileAt(x, y) == brush) painted++;
            }
        }
    }
    const bool sizeOk = reopened && check.width() == 32 && check.height() == 32;

    doc.Close();
    const bool closed = !doc.IsOpen() && doc.path().empty();

    MapDocument fresh;
    const bool freshOk = fresh.CreateNew(64, 64, 0, std::wstring()) && fresh.IsOpen() &&
                         fresh.path().empty();

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"created=%d\ndirtyNew=%d\nunnamed=%d\nsaved=%d\ncleanAfterSave=%d\n"
        L"pathSaved=%d\nreopened=%d\nsizeOk=%d\npainted=%d/16\nclosed=%d\nfreshOk=%d\n",
        created ? 1 : 0, dirtyAfterNew ? 1 : 0, unnamed ? 1 : 0, saved ? 1 : 0,
        cleanAfterSave ? 1 : 0, pathAfterSave ? 1 : 0, reopened ? 1 : 0, sizeOk ? 1 : 0,
        painted, closed ? 1 : 0, freshOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = created && dirtyAfterNew && unnamed && saved && cleanAfterSave &&
                    pathAfterSave && reopened && sizeOk && painted == 16 && closed && freshOk;
    return ok ? 0 : 2;
}

// --mpq-compress-test=<输出mpq>：压缩写出与读回闭环（含不可压缩条目的回退）
int RunMpqCompressTest(const std::wstring& outPath) {
    // 高度可压缩：同一行文本重复
    Buffer text;
    const char* pattern = "staredit\\scenario.chk triggers locations strings ";
    for (int i = 0; i < 4000; i++) {
        for (const char* p = pattern; *p; ++p) text.push_back(static_cast<u8>(*p));
    }
    // 不可压缩：伪随机字节
    Buffer noise(4096);
    unsigned seed = 0x12345678u;
    for (u8& b : noise) {
        seed = seed * 1664525u + 1013904223u;
        b = static_cast<u8>(seed >> 24);
    }

    std::vector<MpqFileEntry> files;
    files.push_back({"text.bin", text});
    files.push_back({"noise.bin", noise});
    if (!MpqArchive::WriteArchive(outPath, files)) {
        WriteDiag(L"compress=FAIL write\n");
        return 2;
    }

    std::ifstream in(outPath.c_str(), std::ios::binary | std::ios::ate);
    const std::size_t archiveSize = in ? static_cast<std::size_t>(in.tellg()) : 0;
    const bool smaller = archiveSize > 0 && archiveSize < text.size() / 2;

    MpqArchive archive;
    Buffer textBack;
    Buffer noiseBack;
    const bool opened = archive.Open(outPath);
    const bool textOk = opened && archive.ReadFile("text.bin", textBack) && textBack == text;
    const bool noiseOk = opened && archive.ReadFile("noise.bin", noiseBack) && noiseBack == noise;

    wchar_t msg[256];
    ::wsprintfW(msg, L"compress=1\nwritten=%u\ntext=%u\nsmaller=%d\ntextOk=%d\nnoiseOk=%d\n",
                static_cast<unsigned>(archiveSize), static_cast<unsigned>(text.size()),
                smaller ? 1 : 0, textOk ? 1 : 0, noiseOk ? 1 : 0);
    WriteDiag(msg);
    return (smaller && textOk && noiseOk) ? 0 : 2;
}

// --edge-test：崖壁自动铺设回归（合成 Space 甲板 + 虚空，验证 1..3 圈与不侵占甲板）
int RunEdgeTest(const std::wstring& tsDir) {
    if (tsDir.empty()) {
        WriteDiag(L"edge=FAIL no-ts\n");
        return 2;
    }
    MapDocument doc;
    if (!doc.CreateNew(32, 32, 1, tsDir) || !doc.hasTileset()) {
        WriteDiag(L"edge=FAIL create\n");
        return 2;
    }
    const Tileset& ts = doc.tileset();
    u16 deck = 0;
    u16 voidTile = 0;
    bool haveDeck = false;
    bool haveVoid = false;
    for (int g = 0; g < ts.GroupCount(); g++) {
        const u16 group = static_cast<u16>(g);
        const u16 type = ts.Type(group);
        if (!haveDeck && ts.IsWalkable(group) && type == 3) {
            deck = static_cast<u16>(g << 4);
            haveDeck = true;
        }
        if (!haveVoid && !ts.IsWalkable(group) && type == 2) {
            voidTile = static_cast<u16>(g << 4);
            haveVoid = true;
        }
    }
    if (!haveDeck || !haveVoid) {
        WriteDiag(L"edge=FAIL groups\n");
        return 2;
    }
    doc.PaintRect(0, 0, doc.width() - 1, doc.height() - 1, voidTile);
    doc.PaintRect(8, 8, 23, 23, deck);

    const int w = doc.width();
    const int h = doc.height();
    const std::size_t count = static_cast<std::size_t>(w) * h;
    const Buffer before = doc.tiles();
    Buffer walkable(count, 0);
    for (std::size_t i = 0; i < count; i++) {
        const u16 v = static_cast<u16>(before[i * 2] | (before[i * 2 + 1] << 8));
        walkable[i] = ts.IsWalkable(static_cast<u16>((v & 0x7FF0) >> 4)) ? 1 : 0;
    }

    Buffer stamped = before;
    const int changed = EdgePlacer().StampOutward(walkable, w, h, ts, stamped);
    Buffer repeat = stamped;
    const int changedAgain = EdgePlacer().StampOutward(walkable, w, h, ts, repeat);

    std::vector<int> dist(count, -1);
    std::queue<int> queue;
    for (std::size_t i = 0; i < count; i++) {
        if (walkable[i]) {
            dist[i] = 0;
            queue.push(static_cast<int>(i));
        }
    }
    const int nx[4] = {0, 1, 0, -1};
    const int ny[4] = {-1, 0, 1, 0};
    while (!queue.empty()) {
        const int idx = queue.front();
        queue.pop();
        if (dist[static_cast<std::size_t>(idx)] >= 3) continue;
        const int x = idx % w;
        const int y = idx / w;
        for (int k = 0; k < 4; k++) {
            const int px = x + nx[k];
            const int py = y + ny[k];
            if (px < 0 || py < 0 || px >= w || py >= h) continue;
            const std::size_t p = static_cast<std::size_t>(py) * w + px;
            if (dist[p] >= 0) continue;
            dist[p] = dist[static_cast<std::size_t>(idx)] + 1;
            queue.push(static_cast<int>(p));
        }
    }

    bool deckKept = true;
    bool cliffOk = true;
    bool ring1 = false;
    bool ring2 = false;
    bool ring3 = false;
    for (std::size_t i = 0; i < count; i++) {
        const u16 a = static_cast<u16>(before[i * 2] | (before[i * 2 + 1] << 8));
        const u16 b = static_cast<u16>(stamped[i * 2] | (stamped[i * 2 + 1] << 8));
        if (walkable[i]) {
            if (a != b) deckKept = false;
            continue;
        }
        if (a == b) continue;
        const u16 group = static_cast<u16>((b & 0x7FF0) >> 4);
        const u16 type = ts.Type(group);
        if (ts.IsWalkable(group) || (type != 10 && type != 20)) cliffOk = false;
        if (dist[i] == 1) ring1 = true;
        else if (dist[i] == 2) ring2 = true;
        else if (dist[i] == 3) ring3 = true;
        else cliffOk = false;   // 只允许压印 1..3 圈
    }

    wchar_t msg[256];
    ::wsprintfW(msg,
                L"edge=1\nstamped=%d\nidempotent=%d\ndeckKept=%d\ncliffOk=%d\nrings=%d%d%d\n",
                changed, changedAgain == 0 ? 1 : 0, deckKept ? 1 : 0, cliffOk ? 1 : 0,
                ring1 ? 1 : 0, ring2 ? 1 : 0, ring3 ? 1 : 0);
    WriteDiag(msg);
    return (changed > 0 && changedAgain == 0 && deckKept && cliffOk && ring1 && ring2 && ring3)
               ? 0
               : 2;
}

// --trig-test=<输出scx>：触发语义回归（字段解析 + 条件/行动编辑 + 玩家开关 + 无损保存）
std::wstring ParseTrigTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--trig-test=", 12) == 0) path = argv[i] + 12;
        }
        ::LocalFree(argv);
    }
    return path;
}

int RunTrigTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, ParseTsArg())) { WriteDiag(L"open=FAIL\n"); return 2; }
    if (doc.triggers().BlockCount() == 0) { WriteDiag(L"no-triggers\n"); return 2; }

    // 1) 未修改时逐字节重建必须与原始段一致
    const Buffer* orig = doc.chk().Find("TRIG");
    const Buffer rebuilt = doc.triggers().Serialize();
    const bool lossless = orig && orig->size() == rebuilt.size() &&
                          std::memcmp(orig->data(), rebuilt.data(), orig->size()) == 0;

    const std::size_t blocksBefore = doc.triggers().BlockCount();
    const Trigger before = doc.triggers().Items()[0];
    const int condBefore = before.ConditionCount();
    const int actBefore = before.ActionCount();

    // 2) 编辑条件/行动/执行标志/玩家开关
    auto& items = doc.mutableTriggers().MutableItems();
    Trigger& t = items[0];
    if (t.ConditionCount() == 0) {
        t.conditions[0].type = 22;      // Always
        t.conditions[0].comparison = 0;
    }
    if (t.ActionCount() == 0) {
        t.actions[0].type = 1;          // Victory
    }
    const u32 condAmount = t.conditions[0].amount + 3;
    const u32 actAmount = (t.actions[0].amount + 5) & 0xFF;
    t.conditions[0].amount = condAmount;
    t.actions[0].amount = static_cast<u8>(actAmount);
    t.executionFlags ^= 0x00000001u;
    t.effectivePlayers[0] = 1;
    const u32 expectedFlags = t.executionFlags;

    // 3) 复制一条触发到末尾（push_back 后 t 可能失效，先取出期望值）
    Trigger copy = t;
    copy.effectivePlayers[0] = 0;
    items.push_back(copy);

    // 4) 序列化后重新解析，字段必须保持
    TriggerSection check;
    const Buffer edited = doc.triggers().Serialize();
    check.Parse(edited);
    const bool parseOk = check.BlockCount() == blocksBefore + 1;
    const Trigger& ct = check.Items()[0];
    const bool condOk = ct.conditions[0].amount == condAmount && ct.conditions[0].type != 0;
    const bool actOk = ct.actions[0].amount == static_cast<u8>(actAmount) && ct.actions[0].type != 0;
    const bool flagOk = ct.executionFlags == expectedFlags;
    const bool playerOk = ct.effectivePlayers[0] == 1;

    // 5) 保存并重开
    const bool saved = doc.Save(outPath);
    MapDocument reopened;
    const bool reopenOk = reopened.Open(outPath, std::wstring());
    bool reopenFields = false;
    bool reopenLossless = false;
    if (reopenOk) {
        const Trigger& rt = reopened.triggers().Items()[0];
        reopenFields = reopened.triggers().BlockCount() == blocksBefore + 1 &&
                       rt.conditions[0].amount == condAmount &&
                       rt.actions[0].amount == static_cast<u8>(actAmount) &&
                       rt.executionFlags == expectedFlags &&
                       rt.effectivePlayers[0] == 1;
        const Buffer* savedTrig = reopened.chk().Find("TRIG");
        const Buffer again = reopened.triggers().Serialize();
        reopenLossless = savedTrig && savedTrig->size() == again.size() &&
                         std::memcmp(savedTrig->data(), again.data(), again.size()) == 0;
    }

    wchar_t msg[640];
    ::wsprintfW(msg,
        L"blocks=%u->%u\ncond=%d act=%d\nlossless=%d\nparse=%d\ncondOk=%d\nactOk=%d\n"
        L"flagOk=%d\nplayerOk=%d\nsaved=%d\nreopened=%d\nreopenFields=%d\nreopenLossless=%d\n",
        static_cast<unsigned>(blocksBefore), static_cast<unsigned>(check.BlockCount()),
        condBefore, actBefore, lossless ? 1 : 0, parseOk ? 1 : 0, condOk ? 1 : 0,
        actOk ? 1 : 0, flagOk ? 1 : 0, playerOk ? 1 : 0, saved ? 1 : 0,
        reopenOk ? 1 : 0, reopenFields ? 1 : 0, reopenLossless ? 1 : 0);
    WriteDiag(msg);

    const bool ok = lossless && parseOk && condOk && actOk && flagOk && playerOk && saved &&
                    reopenOk && reopenFields && reopenLossless;
    return ok ? 0 : 2;
}

// --extra-test=<输出scx>：自定义 MPQ 文件（BIN 等）保存后必须原样保留
std::wstring ParseExtraTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--extra-test=", 13) == 0) path = argv[i] + 13;
        }
        ::LocalFree(argv);
    }
    return path;
}

int RunExtraFileTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MpqArchive src;
    Buffer chk;
    if (!src.Open(mapPath) || !src.ReadScenarioChk(chk)) {
        WriteDiag(L"seed=FAIL\n");
        return 2;
    }
    Buffer probe(256);
    for (std::size_t i = 0; i < probe.size(); i++) probe[i] = static_cast<u8>((i * 37 + 11) & 0xFF);

    const std::wstring seed = outPath + L".seed.scx";
    std::vector<MpqFileEntry> files;
    const std::string list = "staredit\\scenario.chk\r\ncustom\\probe.bin\r\n";
    files.push_back({"(listfile)", Buffer(list.begin(), list.end())});
    files.push_back({"staredit\\scenario.chk", chk});
    files.push_back({"custom\\probe.bin", probe});
    if (!MpqArchive::WriteArchive(seed, files)) {
        wchar_t err[512];
        ::wsprintfW(err, L"seedwrite=FAIL path=%s err=%u\n", seed.c_str(), ::GetLastError());
        WriteDiag(err);
        return 2;
    }

    MapDocument doc;
    const bool opened = doc.Open(seed);
    const std::size_t extras = doc.ExtraFileCount();

    // 附加文件管理 API：新增 / 读取 / 替换 / 拒绝内部名 / 删除
    Buffer probe2(64);
    for (std::size_t i = 0; i < probe2.size(); i++) probe2[i] = static_cast<u8>((i * 53 + 7) & 0xFF);
    Buffer readAdded;
    const bool added = opened && doc.SetExtraFile("custom\\added.bin", probe2);
    const bool addRead = doc.ReadExtraFile("custom\\added.bin", readAdded) &&
                         readAdded.size() == probe2.size() &&
                         std::memcmp(readAdded.data(), probe2.data(), probe2.size()) == 0;
    probe2[0] = static_cast<u8>(probe2[0] ^ 0xFF);
    const bool replaced = doc.SetExtraFile("custom\\added.bin", probe2);
    const bool replaceRead = doc.ReadExtraFile("custom\\added.bin", readAdded) &&
                             readAdded.size() == probe2.size() && readAdded[0] == probe2[0];
    const bool rejected = !doc.SetExtraFile("(listfile)", Buffer(1, 0)) &&
                          !doc.SetExtraFile("staredit\\scenario.chk", Buffer(1, 0));
    const bool removed = doc.RemoveExtraFile("custom\\added.bin") &&
                         !doc.ReadExtraFile("custom\\added.bin", readAdded);
    const bool extrasStill = doc.ExtraFileCount() == extras;

    const bool saved = opened && doc.Save(outPath);

    Buffer back;
    MpqArchive check;
    const bool readBack = check.Open(outPath) && check.ReadFile("custom\\probe.bin", back);
    const bool same = readBack && back.size() == probe.size() &&
                      std::memcmp(back.data(), probe.data(), probe.size()) == 0;
    ::DeleteFileW(seed.c_str());

    wchar_t msg[320];
    ::wsprintfW(msg, L"opened=%d extras=%u saved=%d readBack=%d same=%d bytes=%u "
                     L"add=%d/%d replace=%d/%d reject=%d remove=%d count=%d\n",
                opened ? 1 : 0, static_cast<unsigned>(extras), saved ? 1 : 0,
                readBack ? 1 : 0, same ? 1 : 0, static_cast<unsigned>(back.size()),
                added ? 1 : 0, addRead ? 1 : 0, replaced ? 1 : 0, replaceRead ? 1 : 0,
                rejected ? 1 : 0, removed ? 1 : 0, extrasStill ? 1 : 0);
    WriteDiag(msg);
    return (opened && extras == 1 && saved && same && added && addRead && replaced &&
            replaceRead && rejected && removed && extrasStill)
               ? 0
               : 2;
}

// --menu-cmd-test：检验编辑层/方格/显示选项菜单命令与勾选回显
HMENU FindMenuWithItem(HMENU menu, int id) {
    if (!menu) return nullptr;
    const int count = ::GetMenuItemCount(menu);
    for (int i = 0; i < count; i++) {
        if (::GetMenuItemID(menu, i) == static_cast<UINT>(id)) return menu;
    }
    for (int i = 0; i < count; i++) {
        if (HMENU sub = ::GetSubMenu(menu, i)) {
            if (HMENU found = FindMenuWithItem(sub, id)) return found;
        }
    }
    return nullptr;
}

bool ItemChecked(HWND hwnd, int id) {
    HMENU menu = FindMenuWithItem(::GetMenu(hwnd), id);
    if (!menu) return false;
    const UINT state = ::GetMenuState(menu, static_cast<UINT>(id), MF_BYCOMMAND);
    return state != 0xFFFFFFFFu && (state & MF_CHECKED) != 0;
}

int RunMenuCommandTest(HWND hwnd, MainWindow& win) {
    auto cmd = [hwnd](int id) {
        ::SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
    };

    const bool layerInit = ItemChecked(hwnd, 0x803A);

    cmd(0x803D);   // 编辑层 → 单位
    const bool layerUnits = ItemChecked(hwnd, 0x803D) && !ItemChecked(hwnd, 0x803A);

    cmd(0x803C);   // 编辑层 → 地点
    const bool layerLoc = ItemChecked(hwnd, 0x803C) && ItemChecked(hwnd, 0x8115);

    cmd(0x803A);   // 回到地形
    const bool layerBack = ItemChecked(hwnd, 0x803A) && !ItemChecked(hwnd, 0x803C);

    cmd(0x80A5);   // 方格 → 较大 (64)
    const bool gridLarge = ItemChecked(hwnd, 0x80A5) && !ItemChecked(hwnd, 0x80D6);
    cmd(0x80D6);   // 方格 → 禁止
    const bool gridOff = ItemChecked(hwnd, 0x80D6) && !ItemChecked(hwnd, 0x80A5);
    cmd(0x800D);   // 方格 → 普通
    const bool gridBack = ItemChecked(hwnd, 0x800D) && !ItemChecked(hwnd, 0x80D6);

    cmd(0x8039);   // 方格颜色 → 红色
    const bool gridRed = ItemChecked(hwnd, 0x8039) && !ItemChecked(hwnd, 0x8036);

    cmd(0x8018);   // 显示选项 → 单位 关
    const bool unitsOff = !ItemChecked(hwnd, 0x8018);
    cmd(0x8018);   // 再开
    const bool unitsOn = ItemChecked(hwnd, 0x8018);

    cmd(0x8113);   // 显示战争迷雾
    const bool fogOn = ItemChecked(hwnd, 0x8113);

    // 小雷达显隐 + 编辑层子菜单里的战争迷雾别名
    cmd(0x80B4);
    const bool minimapOff = ItemChecked(hwnd, 0x80B4);
    cmd(0x80B4);
    const bool minimapOn = !ItemChecked(hwnd, 0x80B4);
    cmd(0x80CE);
    const bool fogAlias = !ItemChecked(hwnd, 0x80CE) && !ItemChecked(hwnd, 0x8113);

    cmd(0x8054);   // 显示选项 → 所有颜色
    const bool allColorsOn = ItemChecked(hwnd, 0x8054);
    cmd(0x8054);

    // 显示选项 → 颜色循环 / 死亡效果
    cmd(0x80D1);
    const bool colorCycleOn = ItemChecked(hwnd, 0x80D1);
    cmd(0x80D1);
    const bool colorCycleOff = !ItemChecked(hwnd, 0x80D1);
    cmd(0x80F6);
    const bool deathFxOn = ItemChecked(hwnd, 0x80F6);
    cmd(0x80F6);
    // 特殊状态（0x80B3）默认勾选：关→开
    cmd(0x80B3);
    const bool specialOff = !ItemChecked(hwnd, 0x80B3);
    cmd(0x80B3);
    const bool specialOn = ItemChecked(hwnd, 0x80B3);
    const bool effectsOk = colorCycleOn && colorCycleOff && deathFxOn && specialOff && specialOn;

    // 调试菜单（本轮启用）
    cmd(0x80DE);   // 允许调试
    const bool debugOn = ItemChecked(hwnd, 0x80DE);
    cmd(0x80FC);   // 普通模式
    const bool debugData1 = ItemChecked(hwnd, 0x80FC) && !ItemChecked(hwnd, 0x80FE);
    cmd(0x80FE);   // 最终模式
    const bool debugData2 = ItemChecked(hwnd, 0x80FE) && !ItemChecked(hwnd, 0x80FC);
    cmd(0x80E1);   // 重叠标记
    const bool overlapOn = ItemChecked(hwnd, 0x80E1);
    cmd(0x80E1);
    cmd(0x810A);   // 图像大小
    const bool imageSizeOn = ItemChecked(hwnd, 0x810A);
    cmd(0x810A);
    cmd(0x810B);   // 图像大小（禁用模式）
    const bool imageSizeAlwaysOn = ItemChecked(hwnd, 0x810B);
    cmd(0x810B);
    cmd(0x80E3);   // 清除装饰物缓存
    cmd(0x80E4);   // 清除单位缓存
    cmd(0x80E5);   // 清除重复缓存
    const bool cacheOk = imageSizeAlwaysOn && ItemChecked(hwnd, 0x810A) == false;
    cmd(0x80E0);   // 对比数据
    const bool compareOn = ItemChecked(hwnd, 0x80E0);
    cmd(0x80E0);
    cmd(0x811D);   // 单位冲突大小
    const bool conflictOn = ItemChecked(hwnd, 0x811D);
    cmd(0x811D);
    cmd(0x8072);   // 启用声音
    const bool soundOff = !ItemChecked(hwnd, 0x8072);
    cmd(0x8072);
    const bool soundOn = ItemChecked(hwnd, 0x8072);

    // 选项 → 输出：显示开关 / 字体大小 / 提示详细度
    cmd(0x80F3);
    const bool outputOff = !ItemChecked(hwnd, 0x80F3);
    cmd(0x80F3);
    cmd(0x805A);   // 8 号字体
    const bool font8 = ItemChecked(hwnd, 0x805A) && !ItemChecked(hwnd, 0x805C);
    cmd(0x805C);   // 还原 12 号
    cmd(0x8063);   // 高级模式
    const bool verbHigh = ItemChecked(hwnd, 0x8063) && !ItemChecked(hwnd, 0x8062);
    cmd(0x8062);   // 还原正常模式
    cmd(0x8068);   // 清除当前信息
    const bool outputOk = outputOff && ItemChecked(hwnd, 0x80F3) && font8 && verbHigh;

    cmd(0x80DE);   // 关闭调试

    // 地图显示比率（需要已打开地图；--menu-cmd-test 可配合 --open）
    bool zoomOk = true;
    bool minimapNav = true;
    if (win.map().IsOpen()) {
        cmd(0x80DD);   // 66% → 21 px/瓦片
        zoomOk = ItemChecked(hwnd, 0x80DD) && !ItemChecked(hwnd, 0x80D8) &&
                 win.pxPerTile() == 21;
        cmd(0x80D8);   // 100% → 32 px/瓦片
        zoomOk = zoomOk && ItemChecked(hwnd, 0x80D8) && win.pxPerTile() == 32;
        cmd(0x80DA);   // 25% → 8 px/瓦片
        zoomOk = zoomOk && ItemChecked(hwnd, 0x80DA) && win.pxPerTile() == 8;

        // 小地图点击定位：点击右下角应滚动到地图右下
        RECT client = {};
        ::GetClientRect(hwnd, &client);
        minimapNav = win.ClickMinimapAt(client.right - 20, client.bottom - 20) &&
                     win.scrollY() > 0;
    }

    // 窗口排列（单窗口也应安全；不测试新建/关闭）
    cmd(0x80EF);   // 水平排列
    cmd(0x80F0);   // 垂直排列
    const bool windowOk = ::IsWindow(hwnd) != 0;

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"layerInit=%d\nlayerUnits=%d\nlayerLoc=%d\nlayerBack=%d\n"
        L"gridLarge=%d\ngridOff=%d\ngridBack=%d\ngridRed=%d\n"
        L"unitsOff=%d\nunitsOn=%d\nfogOn=%d\nzoom=%d\nminimapOff=%d\nminimapOn=%d\n"
        L"fogAlias=%d\nallColors=%d\neffects=%d\ndebug=%d\ndebugData=%d\noverlap=%d\nimageSize=%d\n"
        L"compare=%d\nconflict=%d\nminiNav=%d\nsound=%d\noutput=%d\ncache=%d\nwindow=%d\n",
        layerInit ? 1 : 0, layerUnits ? 1 : 0, layerLoc ? 1 : 0, layerBack ? 1 : 0,
        gridLarge ? 1 : 0, gridOff ? 1 : 0, gridBack ? 1 : 0, gridRed ? 1 : 0,
        unitsOff ? 1 : 0, unitsOn ? 1 : 0, fogOn ? 1 : 0, zoomOk ? 1 : 0,
        minimapOff ? 1 : 0, minimapOn ? 1 : 0, fogAlias ? 1 : 0, allColorsOn ? 1 : 0,
        effectsOk ? 1 : 0,
        debugOn ? 1 : 0, (debugData1 && debugData2) ? 1 : 0, overlapOn ? 1 : 0,
        imageSizeOn ? 1 : 0, compareOn ? 1 : 0, conflictOn ? 1 : 0, minimapNav ? 1 : 0,
        (soundOff && soundOn) ? 1 : 0, outputOk ? 1 : 0, cacheOk ? 1 : 0, windowOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = layerInit && layerUnits && layerLoc && layerBack && gridLarge && gridOff &&
                    gridBack && gridRed && unitsOff && unitsOn && fogOn && zoomOk &&
                    minimapOff && minimapOn && fogAlias && allColorsOn && effectsOk && debugOn &&
                    debugData1 && debugData2 && overlapOn && imageSizeOn && compareOn &&
                    conflictOn && minimapNav && soundOff && soundOn && outputOk && cacheOk && windowOk;
    return ok ? 0 : 2;
}

// --resize-test=<输出scx>：重设地图大小回归（DIM/MTXM 平移、单位越界丢弃）
std::wstring ParseResizeTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--resize-test=", 14) == 0) path = argv[i] + 14;
        }
        ::LocalFree(argv);
    }
    return path;
}

int RunResizeTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, ParseTsArg())) { WriteDiag(L"open=FAIL\n"); return 2; }

    const int oldW = doc.width();
    const int oldH = doc.height();
    const std::size_t oldUnits = doc.unitCount();

    // 找一个非零瓦片，验证平移映射
    int fx = -1, fy = -1;
    u16 expected = 0;
    for (int y = 0; y < oldH && fx < 0; y++) {
        for (int x = 0; x < oldW; x++) {
            if (doc.TileAt(x, y) != 0) { fx = x; fy = y; expected = doc.TileAt(x, y); break; }
        }
    }

    const int newW = 96, newH = 96;
    const bool resized = doc.Resize(newW, newH, 1, 1, false);
    const bool dimOk = doc.width() == newW && doc.height() == newH;
    const bool tileMoveOk = fx >= 0 && fx + 1 < newW && fy + 1 < newH &&
                            doc.TileAt(fx + 1, fy + 1) == expected;
    const bool borderOk = doc.TileAt(0, 0) == 0;
    const bool unitsInBounds = [&doc] {
        for (const auto& u : doc.units()) {
            if (u.x >= doc.width() * 32 || u.y >= doc.height() * 32) return false;
        }
        return true;
    }();
    const bool dirty = doc.IsDirty();

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const bool reopenDim = reopened && check.width() == newW && check.height() == newH;
    const bool reopenTile = reopened && fx >= 0 && check.TileAt(fx + 1, fy + 1) == expected;

    // 平滑扩边：新增列/行沿用最近的原始瓦片
    MapDocument smoothDoc;
    bool smoothOk = false;
    if (smoothDoc.Open(mapPath, ParseTsArg())) {
        const u16 edgeTile = smoothDoc.TileAt(oldW - 1, oldH / 2);
        const u16 innerTile = smoothDoc.TileAt(oldW / 2, oldH / 2);
        const bool grew = smoothDoc.Resize(oldW + 8, oldH + 8, 0, 0, true);
        smoothOk = grew && smoothDoc.TileAt(oldW + 4, oldH / 2) == edgeTile &&
                   smoothDoc.TileAt(oldW / 2, oldH / 2) == innerTile;
    }

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"old=%dx%d\nnew=%dx%d\nresized=%d\ndimOk=%d\ntileMove=%d\nborderZero=%d\n"
        L"unitsInBounds=%d\nunits=%u->%u\ndirty=%d\nsaved=%d\nreopened=%d\nreopenDim=%d\n"
        L"reopenTile=%d\nsmooth=%d\n",
        oldW, oldH, newW, newH, resized ? 1 : 0, dimOk ? 1 : 0, tileMoveOk ? 1 : 0,
        borderOk ? 1 : 0, unitsInBounds ? 1 : 0, static_cast<unsigned>(oldUnits),
        static_cast<unsigned>(doc.unitCount()), dirty ? 1 : 0,
        saved ? 1 : 0, reopened ? 1 : 0, reopenDim ? 1 : 0, reopenTile ? 1 : 0,
        smoothOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = resized && dimOk && tileMoveOk && borderOk && unitsInBounds && dirty &&
                    saved && reopened && reopenDim && reopenTile && smoothOk;
    return ok ? 0 : 2;
}

// --undo-test：无头撤销/重做回归
std::wstring ParseUndoTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring v;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--undo-test", 11) == 0) v = L"1";
        }
        ::LocalFree(argv);
    }
    return v;
}

unsigned long long HashDocument(const MapDocument& doc) {
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&h](u8 b) { h ^= b; h *= 1099511628211ull; };
    for (u8 b : doc.tiles()) mix(b);
    for (const auto& u : doc.units()) {
        mix(static_cast<u8>(u.serial & 0xFF));
        mix(static_cast<u8>((u.serial >> 8) & 0xFF));
        mix(static_cast<u8>(u.x & 0xFF));
        mix(static_cast<u8>(u.x >> 8));
        mix(static_cast<u8>(u.y & 0xFF));
        mix(static_cast<u8>(u.y >> 8));
        mix(static_cast<u8>(u.type & 0xFF));
        mix(static_cast<u8>(u.type >> 8));
        mix(u.owner);
    }
    return h;
}

EditCommand MakeCommand(MapDocument& doc, Buffer tilesBefore, std::vector<UnitRecord> unitsBefore,
                         const wchar_t* label) {
    EditCommand cmd;
    cmd.label = label;
    cmd.tilesBefore = std::move(tilesBefore);
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = std::move(unitsBefore);
    cmd.unitsAfter = doc.units();
    return cmd;
}

int RunUndoTest(const std::wstring& mapPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, ParseTsArg())) { WriteDiag(L"open=FAIL\n"); return 2; }

    EditHistory history;
    const unsigned long long h0 = HashDocument(doc);

    int wx = -1, wy = -1;
    if (!FindWalkable(doc, doc.width() / 2, doc.height() / 2, wx, wy)) {
        WriteDiag(L"no-walkable\n");
        return 2;
    }

    // 1) 地形：刷 3 格 -> 撤销 -> 重做
    Buffer t0 = doc.tiles();
    std::vector<UnitRecord> u0 = doc.units();
    const u16 original = doc.TileAt(wx, wy);
    const u16 target = static_cast<u16>(original ^ 0x0001);   // 保证值不同
    doc.PaintRect(wx, wy, wx + 2, wy, target);
    const unsigned long long h1 = HashDocument(doc);
    history.Push(MakeCommand(doc, t0, u0, L"绘制地形"));

    // 局部重绘原语：只刷新 3 格区域后的整图必须与全量渲染逐字节一致
    bool regionOk = true;
    if (doc.hasTileset()) {
        TerrainRenderer renderer;
        renderer.SetTileset(&doc.tileset());
        TerrainImage full;
        TerrainImage partial;
        TerrainImage reference;
        if (!renderer.Render(doc.tiles(), doc.width(), doc.height(), 8, full)) {
            regionOk = false;
        } else {
            partial = full;
            if (!renderer.RenderRegion(doc.tiles(), doc.width(), doc.height(), 8, wx, wy, wx + 2, wy,
                                       partial)) {
                regionOk = false;
            } else if (!renderer.Render(doc.tiles(), doc.width(), doc.height(), 8, reference) ||
                       reference.pixels != partial.pixels) {
                regionOk = false;
            }
        }
    }

    history.Undo(doc);
    const unsigned long long hUndo = HashDocument(doc);
    history.Redo(doc);
    const unsigned long long hRedo = HashDocument(doc);

    const bool tileEditOk = (h1 != h0);
    const bool tileUndoOk = (hUndo == h0);
    const bool tileRedoOk = (hRedo == h1);

    // 2) 单位：放 2 个 -> 撤销 -> 重做
    Buffer t2 = doc.tiles();
    std::vector<UnitRecord> u2 = doc.units();
    int placed = 0;
    if (doc.PlaceUnit(wx, wy, 176, 11, 1500)) placed++;
    int gx = -1, gy = -1;
    if (FindWalkable(doc, wx + 5, wy + 5, gx, gy) && doc.PlaceUnit(gx, gy, 188, 11, 5000)) placed++;
    const std::size_t afterPlace = doc.units().size();
    history.Push(MakeCommand(doc, t2, u2, L"放置单位"));

    history.Undo(doc);
    const std::size_t afterUndo = doc.units().size();
    history.Redo(doc);
    const std::size_t afterRedo = doc.units().size();

    const bool placeOk = placed == 2 && afterPlace == u2.size() + 2;
    const bool placeUndoOk = afterUndo == u2.size();
    const bool placeRedoOk = afterRedo == afterPlace;

    // 3) 删除 1 个单位 -> 撤销应完整还原（含 serial）
    Buffer t3 = doc.tiles();
    std::vector<UnitRecord> u3 = doc.units();
    const int removed = doc.RemoveUnitsAtTile(gx, gy);
    history.Push(MakeCommand(doc, t3, u3, L"删除单位"));
    history.Undo(doc);
    const bool removeUndoOk = removed == 1 && doc.units().size() == u3.size() &&
                              doc.units().back().serial == u3.back().serial;

    // 4) 历史条数与重做栈清空行为
    history.Redo(doc);
    history.Push(MakeCommand(doc, doc.tiles(), doc.units(), L"空操作"));
    const bool redoCleared = !history.CanRedo();

    // 5) 元数据（TRIG/MRGN/STR）撤销/重做
    const Buffer mt0 = doc.triggers().Serialize();
    const Buffer ml0 = doc.locations().Serialize();
    const Buffer ms0 = doc.strings().Serialize();

    doc.mutableStrings().Set(1, "meta-undo");
    Location metaLoc;
    metaLoc.left = 1;
    metaLoc.top = 2;
    metaLoc.right = 10;
    metaLoc.bottom = 11;
    doc.mutableLocations().MutableItems().push_back(metaLoc);
    Trigger metaTrigger;
    metaTrigger.executionFlags = 1;
    doc.mutableTriggers().MutableItems().push_back(metaTrigger);

    const Buffer mt1 = doc.triggers().Serialize();
    const Buffer ml1 = doc.locations().Serialize();
    const Buffer ms1 = doc.strings().Serialize();

    EditCommand meta;
    meta.label = L"元数据";
    meta.tilesBefore = doc.tiles();
    meta.tilesAfter = doc.tiles();
    meta.unitsBefore = doc.units();
    meta.unitsAfter = doc.units();
    meta.hasMeta = true;
    meta.triggersBefore = mt0;
    meta.triggersAfter = mt1;
    meta.locationsBefore = ml0;
    meta.locationsAfter = ml1;
    meta.stringsBefore = ms0;
    meta.stringsAfter = ms1;
    history.Push(std::move(meta));

    history.Undo(doc);
    const bool metaUndoOk = doc.triggers().Serialize() == mt0 &&
                            doc.locations().Serialize() == ml0 &&
                            doc.strings().Serialize() == ms0;
    history.Redo(doc);
    const bool metaRedoOk = doc.triggers().Serialize() == mt1 &&
                            doc.locations().Serialize() == ml1 &&
                            doc.strings().Serialize() == ms1;

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"tileEdit=%d\ntileUndo=%d\ntileRedo=%d\nplaceOk=%d\nplaceUndo=%d\nplaceRedo=%d\n"
        L"removeUndo=%d\nundoCount=%u\nredoCleared=%d\nregionOk=%d\nmetaUndo=%d\nmetaRedo=%d\n",
        tileEditOk ? 1 : 0, tileUndoOk ? 1 : 0, tileRedoOk ? 1 : 0,
        placeOk ? 1 : 0, placeUndoOk ? 1 : 0, placeRedoOk ? 1 : 0,
        removeUndoOk ? 1 : 0, static_cast<unsigned>(history.UndoCount()),
        redoCleared ? 1 : 0, regionOk ? 1 : 0, metaUndoOk ? 1 : 0, metaRedoOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = tileEditOk && tileUndoOk && tileRedoOk && placeOk && placeUndoOk &&
                    placeRedoOk && removeUndoOk && regionOk && metaUndoOk && metaRedoOk;
    return ok ? 0 : 2;
}

// --player-test=<输出scx>：OWNR/SIDE/FORC/COLR 解析、修改、撤销、保存回归
int RunPlayerTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer upgr0 = sectionOf(doc, "UPGR");
    const Buffer dim0 = sectionOf(doc, "DIM ");
    const Buffer mtxm0 = sectionOf(doc, "MTXM");

    const bool parsed = doc.players().hasOwnr() && doc.players().hasSide() &&
                        doc.players().hasForc() && doc.players().hasColr();
    const Buffer before = doc.players().Snapshot();

    doc.mutablePlayers().SetController(0, static_cast<u8>(PlayerController::kHumanOpen));
    doc.mutablePlayers().SetRace(0, static_cast<u8>(PlayerRace::kProtoss));
    doc.mutablePlayers().SetColor(0, 5);
    doc.mutablePlayers().SetForce(0, 2);
    doc.mutablePlayers().SetForceNameId(1, 9);
    doc.mutablePlayers().SetForceFlags(1, 3);
    doc.mutablePlayers().SetController(11, static_cast<u8>(PlayerController::kNeutral));
    const Buffer after = doc.players().Snapshot();

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"玩家/势力";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = doc.units();
    cmd.unitsAfter = doc.units();
    cmd.hasPlayers = true;
    cmd.playersBefore = before;
    cmd.playersAfter = after;
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool undoOk = doc.players().Snapshot() == before;
    history.Redo(doc);
    const bool redoOk = doc.players().Snapshot() == after;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const PlayerSlot& p0 = check.players().player(0);
    const PlayerSlot& p11 = check.players().player(11);
    const ForceSettings& f1 = check.players().force(1);
    const bool fieldsOk = reopened && p0.controller == 6 && p0.race == 2 && p0.color == 5 &&
                          p0.force == 2 && p11.controller == 7 && f1.nameStringId == 9 &&
                          f1.flags == 3;
    const bool preserved = reopened && sectionOf(check, "UPGR") == upgr0 &&
                           sectionOf(check, "DIM ") == dim0 &&
                           sectionOf(check, "MTXM") == mtxm0;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nparsed=%d\nundo=%d\nredo=%d\nsaved=%d\nreopened=%d\n"
                L"fields=%d\npreserved=%d\n",
                parsed ? 1 : 0, undoOk ? 1 : 0, redoOk ? 1 : 0, saved ? 1 : 0,
                reopened ? 1 : 0, fieldsOk ? 1 : 0, preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = parsed && undoOk && redoOk && saved && reopened && fieldsOk && preserved;
    return ok ? 0 : 2;
}

// --restriction-test=<输出scx>：UPGR/PTEC/PUPx/PTEx 解析、修改、撤销、保存回归
int RunRestrictionTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer ownr0 = sectionOf(doc, "OWNR");
    const Buffer dim0 = sectionOf(doc, "DIM ");
    const Buffer mtxm0 = sectionOf(doc, "MTXM");
    const bool parsed = doc.restrictions().upgr.present;
    const Buffer before = doc.restrictions().Snapshot();

    UpgradeTechRestrictions& restrictions = doc.mutableRestrictions();
    restrictions.upgr.SetGlobalPrimary(0, 3);
    restrictions.upgr.SetGlobalSecondary(0, 1);
    restrictions.upgr.SetPrimary(0, 0, 2);
    restrictions.upgr.SetSecondary(0, 0, 1);
    restrictions.upgr.SetUseDefaults(0, 0, 0);
    restrictions.ptec.SetGlobalPrimary(1, 1);
    restrictions.ptec.SetPrimary(2, 1, 1);
    restrictions.pupx.SetGlobalPrimary(0, 3);
    restrictions.ptex.SetPrimary(0, 0, 1);
    const Buffer after = restrictions.Snapshot();

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"升级/科技限制";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = doc.units();
    cmd.unitsAfter = doc.units();
    cmd.hasRestrictions = true;
    cmd.restrictionsBefore = before;
    cmd.restrictionsAfter = after;
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool undoOk = doc.restrictions().Snapshot() == before;
    history.Redo(doc);
    const bool redoOk = doc.restrictions().Snapshot() == after;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const UpgradeTechRestrictions& r = check.restrictions();
    const bool fieldsOk = reopened &&
        r.upgr.present && r.upgr.GlobalPrimary(0) == 3 && r.upgr.GlobalSecondary(0) == 1 &&
        r.upgr.Primary(0, 0) == 2 && r.upgr.Secondary(0, 0) == 1 &&
        r.upgr.UseDefaults(0, 0) == 0 &&
        r.ptec.present && r.ptec.GlobalPrimary(1) == 1 && r.ptec.Primary(2, 1) == 1 &&
        r.pupx.present && r.pupx.GlobalPrimary(0) == 3 &&
        r.ptex.present && r.ptex.Primary(0, 0) == 1;
    const bool sizesOk = reopened &&
        sectionOf(check, "UPGR").size() == 1748 &&
        sectionOf(check, "PTEC").size() == 912 &&
        sectionOf(check, "PUPx").size() == 2318 &&
        sectionOf(check, "PTEx").size() == 1672;
    const bool preserved = reopened && sectionOf(check, "OWNR") == ownr0 &&
                           sectionOf(check, "DIM ") == dim0 &&
                           sectionOf(check, "MTXM") == mtxm0;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nparsed=%d\nundo=%d\nredo=%d\nsaved=%d\nreopened=%d\n"
                L"fields=%d\nsizes=%d\npreserved=%d\n",
                parsed ? 1 : 0, undoOk ? 1 : 0, redoOk ? 1 : 0, saved ? 1 : 0,
                reopened ? 1 : 0, fieldsOk ? 1 : 0, sizesOk ? 1 : 0, preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = parsed && undoOk && redoOk && saved && reopened && fieldsOk &&
                    sizesOk && preserved;
    return ok ? 0 : 2;
}

// --location-test=<输出scx>：Location 171 → MRGN 写回回归
// 修改一条地点的矩形/高程标志/名称，验证撤销重做、保存重开与其它记录不变。
int RunLocationTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer upgr0 = sectionOf(doc, "UPGR");
    const Buffer dim0 = sectionOf(doc, "DIM ");
    const Buffer mtxm0 = sectionOf(doc, "MTXM");

    const bool parsed = doc.locations().Count() > 0;
    if (!parsed) {
        WriteDiag(L"no-locations\n");
        return 2;
    }
    const std::size_t locCount = doc.locations().Count();
    const Buffer locBefore = doc.locations().Serialize();
    const Buffer strBefore = doc.strings().Serialize();
    const Buffer trigBefore = doc.triggers().Serialize();

    Location& loc0 = doc.mutableLocations().MutableItems()[0];
    const Location original = loc0;
    loc0.left = 2;
    loc0.top = 3;
    loc0.right = 9;
    loc0.bottom = 11;
    loc0.flags = static_cast<u16>((original.flags & ~0x3Fu) | 0x04u);   // 高地
    int sid = original.stringId;
    if (sid <= 0) sid = static_cast<int>(std::max<std::size_t>(1, doc.strings().Count()));
    doc.mutableStrings().Set(static_cast<std::size_t>(sid), "SCMD-LOC-TEST");
    loc0.stringId = static_cast<u16>(sid);
    doc.MarkDirty();

    const Buffer locAfter = doc.locations().Serialize();
    const Buffer strAfter = doc.strings().Serialize();

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"编辑地点";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = doc.units();
    cmd.unitsAfter = doc.units();
    cmd.hasMeta = true;
    cmd.triggersBefore = trigBefore;
    cmd.triggersAfter = trigBefore;
    cmd.locationsBefore = locBefore;
    cmd.locationsAfter = locAfter;
    cmd.stringsBefore = strBefore;
    cmd.stringsAfter = strAfter;
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool undoOk = doc.locations().Serialize() == locBefore &&
                        doc.strings().Serialize() == strBefore;
    history.Redo(doc);
    const bool redoOk = doc.locations().Serialize() == locAfter &&
                        doc.strings().Serialize() == strAfter;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const Location& c0 = check.locations().Items()[0];
    const bool fieldsOk = reopened && check.locations().Count() == locCount && c0.left == 2 &&
                          c0.top == 3 && c0.right == 9 && c0.bottom == 11 &&
                          (c0.flags & 0x3Fu) == 0x04u && c0.stringId == sid &&
                          check.strings().At(static_cast<std::size_t>(sid)) == "SCMD-LOC-TEST";
    const bool reservedOk = reopened && std::memcmp(c0.reserved, original.reserved, 8) == 0;

    bool othersOk = reopened && check.locations().Count() == locCount;
    if (othersOk) {
        const Buffer after = check.locations().Serialize();
        othersOk = after.size() == locBefore.size();
        if (othersOk) {
            for (std::size_t i = 1; i < locCount; i++) {
                if (std::memcmp(after.data() + i * 20, locBefore.data() + i * 20, 20) != 0) {
                    othersOk = false;
                    break;
                }
            }
        }
    }
    const bool preserved = reopened && sectionOf(check, "UPGR") == upgr0 &&
                           sectionOf(check, "DIM ") == dim0 &&
                           sectionOf(check, "MTXM") == mtxm0;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nparsed=%d\nundo=%d\nredo=%d\nsaved=%d\nreopened=%d\n"
                L"fields=%d\nreserved=%d\nothers=%d\npreserved=%d\n",
                parsed ? 1 : 0, undoOk ? 1 : 0, redoOk ? 1 : 0, saved ? 1 : 0,
                reopened ? 1 : 0, fieldsOk ? 1 : 0, reservedOk ? 1 : 0, othersOk ? 1 : 0,
                preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = parsed && undoOk && redoOk && saved && reopened && fieldsOk && reservedOk &&
                    othersOk && preserved;
    return ok ? 0 : 2;
}

// --decor-test=<输出scx>：DD2 装饰物 / MASK 战争迷雾解析、修改、撤销、保存回归
int RunDecorTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer dd2Orig = sectionOf(doc, "DD2 ");
    const Buffer maskOrig = sectionOf(doc, "MASK");
    const Buffer thg2Orig = sectionOf(doc, "THG2");
    const Buffer upgrOrig = sectionOf(doc, "UPGR");
    const Buffer dimOrig = sectionOf(doc, "DIM ");
    const Buffer mtxmOrig = sectionOf(doc, "MTXM");

    const bool parsed = doc.doodads().present() && doc.doodads().Count() > 0 &&
                        doc.fog().present() &&
                        doc.fog().Serialize().size() ==
                            static_cast<std::size_t>(doc.width()) * doc.height();
    const bool roundtrip = doc.doodads().Serialize() == dd2Orig &&
                           doc.fog().Serialize() == maskOrig;

    // 整图填充语义：0x00 已探索 / 0xFF 未探索
    bool fillOk = true;
    {
        FogMask fill;
        fill.SetAll(0x00, doc.width(), doc.height());
        for (int ty = 0; ty < doc.height() && fillOk; ty += 7) {
            for (int tx = 0; tx < doc.width(); tx += 7) {
                if (fill.IsFogged(tx, ty)) { fillOk = false; break; }
            }
        }
        fill.SetAll(0xFF, doc.width(), doc.height());
        for (int ty = 0; ty < doc.height() && fillOk; ty += 7) {
            for (int tx = 0; tx < doc.width(); tx += 7) {
                if (!fill.IsFogged(tx, ty)) { fillOk = false; break; }
            }
        }
    }

    const Buffer dd2Before = doc.doodads().Serialize();
    const Buffer fogBefore = doc.fog().Serialize();
    const Doodad dd0Orig = doc.doodads().Items()[0];
    doc.mutableDoodads().MutableItems()[0].x = static_cast<u16>(dd0Orig.x + 32);
    const bool fogA = doc.mutableFog().SetFogged(2, 2, !doc.fog().IsFogged(2, 2));
    const bool fogB = doc.mutableFog().SetFogged(3, 2, doc.fog().IsFogged(2, 2));
    const bool fogTarget = doc.fog().IsFogged(2, 2);
    doc.MarkDirty();

    const Buffer dd2After = doc.doodads().Serialize();
    const Buffer fogAfter = doc.fog().Serialize();

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"装饰物/战争迷雾";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = doc.units();
    cmd.unitsAfter = doc.units();
    cmd.hasDecor = true;
    cmd.doodadsBefore = dd2Before;
    cmd.doodadsAfter = dd2After;
    cmd.fogBefore = fogBefore;
    cmd.fogAfter = fogAfter;
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool undoOk = doc.doodads().Serialize() == dd2Before &&
                        doc.fog().Serialize() == fogBefore;
    history.Redo(doc);
    const bool redoOk = doc.doodads().Serialize() == dd2After &&
                        doc.fog().Serialize() == fogAfter;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const bool doodadsOk = reopened && check.doodads().Count() > 0 &&
                           check.doodads().Items()[0].x == dd0Orig.x + 32;
    const bool fogOk = reopened && check.fog().IsFogged(2, 2) == fogTarget &&
                       check.fog().IsFogged(3, 2) == fogTarget;

    bool othersOk = reopened && check.doodads().Count() == doc.doodads().Count();
    if (othersOk) {
        const Buffer after = check.doodads().Serialize();
        othersOk = after.size() == dd2Before.size();
        if (othersOk) {
            for (std::size_t i = 1; i < check.doodads().Count(); i++) {
                if (std::memcmp(after.data() + i * 8, dd2Before.data() + i * 8, 8) != 0) {
                    othersOk = false;
                    break;
                }
            }
            // 迷雾除 (2,2)/(3,2) 外应与原始一致
            const Buffer& fog = check.fog().Serialize();
            if (fog.size() != fogBefore.size()) {
                othersOk = false;
            } else {
                for (std::size_t i = 0; i < fog.size() && othersOk; i++) {
                    const int tx = static_cast<int>(i % check.width());
                    const int ty = static_cast<int>(i / check.width());
                    if ((tx == 2 || tx == 3) && ty == 2) continue;
                    if (fog[i] != fogBefore[i]) othersOk = false;
                }
            }
        }
    }
    const bool preserved = reopened && sectionOf(check, "THG2") == thg2Orig &&
                           sectionOf(check, "UPGR") == upgrOrig &&
                           sectionOf(check, "DIM ") == dimOrig &&
                           sectionOf(check, "MTXM") == mtxmOrig;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nparsed=%d\nroundtrip=%d\nfill=%d\nundo=%d\nredo=%d\n"
                L"saved=%d\nreopened=%d\ndoodads=%d\nfog=%d\nothers=%d\npreserved=%d\n",
                parsed ? 1 : 0, roundtrip ? 1 : 0, fillOk ? 1 : 0, undoOk ? 1 : 0,
                redoOk ? 1 : 0, saved ? 1 : 0, reopened ? 1 : 0, doodadsOk ? 1 : 0,
                (fogA || fogB) && fogOk ? 1 : 0, othersOk ? 1 : 0, preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = parsed && roundtrip && fillOk && undoOk && redoOk && saved && reopened &&
                    doodadsOk && fogOk && othersOk && preserved;
    return ok ? 0 : 2;
}

// --sprite-test=<输出scx>：THG2 SP 单位解析、写入、保存重开回归
int RunSpriteTest(const std::wstring& mapPath, const std::wstring& outPath) {
    // 独立构造 3 条记录，校验 10 字节布局与 bit12 语义
    Buffer raw(30, 0);
    auto put16 = [&raw](std::size_t off, u16 v) {
        raw[off] = static_cast<u8>(v & 0xFF);
        raw[off + 1] = static_cast<u8>(v >> 8);
    };
    put16(0, 100);
    put16(2, 4 * 32);
    put16(4, 4 * 32);
    raw[6] = 0;
    raw[7] = 0;
    put16(8, 0x1000);
    put16(10, 101);
    put16(12, 6 * 32);
    put16(14, 4 * 32);
    raw[16] = 1;
    put16(18, 0x0000);
    put16(20, 102);
    put16(22, 8 * 32);
    put16(24, 5 * 32);
    raw[26] = 2;
    put16(28, 0x1000);

    SpriteTable table;
    const bool roundtrip = table.Parse(raw) && table.Serialize() == raw &&
                           table.Count() == 3 && table.Items()[0].DrawAsSprite() &&
                           !table.Items()[1].DrawAsSprite() &&
                           table.Items()[2].DrawAsSprite();

    // 长度异常：不解析，序列化原样返回
    Buffer bad(7, 0xAA);
    SpriteTable badTable;
    const bool malformed = !badTable.Parse(bad) && badTable.Serialize() == bad;

    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }
    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer dd2Orig = sectionOf(doc, "DD2 ");
    const Buffer maskOrig = sectionOf(doc, "MASK");
    const Buffer upgrOrig = sectionOf(doc, "UPGR");
    const Buffer dimOrig = sectionOf(doc, "DIM ");
    const Buffer mtxmOrig = sectionOf(doc, "MTXM");

    auto& items = doc.mutableSprites().MutableItems();
    items.push_back({100, 4 * 32, 4 * 32, 0, 0, 0x1000});
    items.push_back({101, 6 * 32, 4 * 32, 1, 0, 0x0000});
    items.push_back({102, 8 * 32, 5 * 32, 2, 0, 0x1000});
    doc.MarkDirty();

    // 模拟 164：玩家 / 未用字节 / Flag 位（bit0 + 普通SP + Flipped + 禁用）
    const Buffer spritesBefore = doc.sprites().Serialize();
    const Buffer doodadsNow = doc.doodads().Serialize();
    const Buffer fogNow = doc.fog().Serialize();
    SpriteRecord& s0 = doc.mutableSprites().MutableItems()[0];
    s0.owner = 2;
    s0.unused = 0x7F;
    s0.flags = static_cast<u16>(0x1000u | 0x2000u | 0x8000u | 0x0001u);
    const Buffer spritesAfter = doc.sprites().Serialize();

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"SP单位属性";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = doc.units();
    cmd.unitsAfter = doc.units();
    cmd.hasDecor = true;
    cmd.doodadsBefore = doodadsNow;
    cmd.doodadsAfter = doodadsNow;
    cmd.fogBefore = fogNow;
    cmd.fogAfter = fogNow;
    cmd.spritesBefore = spritesBefore;
    cmd.spritesAfter = spritesAfter;
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool undoOk = doc.sprites().Serialize() == spritesBefore;
    history.Redo(doc);
    const bool redoOk = doc.sprites().Serialize() == spritesAfter;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const bool fieldsOk = reopened && check.sprites().Count() == 3 &&
                          check.sprites().Items()[0].type == 100 &&
                          check.sprites().Items()[0].x == 4 * 32 &&
                          check.sprites().Items()[0].owner == 2 &&
                          check.sprites().Items()[0].unused == 0x7F &&
                          check.sprites().Items()[0].flags == 0xB001u &&
                          check.sprites().Items()[1].owner == 1 &&
                          check.sprites().Items()[2].DrawAsSprite() &&
                          sectionOf(check, "THG2").size() == 30;
    const bool preserved = reopened && sectionOf(check, "DD2 ") == dd2Orig &&
                           sectionOf(check, "MASK") == maskOrig &&
                           sectionOf(check, "UPGR") == upgrOrig &&
                           sectionOf(check, "DIM ") == dimOrig &&
                           sectionOf(check, "MTXM") == mtxmOrig;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nroundtrip=%d\nmalformed=%d\nundo=%d\nredo=%d\nsaved=%d\n"
                L"reopened=%d\nfields=%d\npreserved=%d\n",
                roundtrip ? 1 : 0, malformed ? 1 : 0, undoOk ? 1 : 0, redoOk ? 1 : 0,
                saved ? 1 : 0, reopened ? 1 : 0, fieldsOk ? 1 : 0, preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = roundtrip && malformed && undoOk && redoOk && saved && reopened &&
                    fieldsOk && preserved;
    return ok ? 0 : 2;
}

// --radar-test=<输出scx>：小雷达（Map Revealer）与迷雾开关回归
int RunRadarTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }

    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer dd2Orig = sectionOf(doc, "DD2 ");
    const Buffer thg2Orig = sectionOf(doc, "THG2");
    const Buffer upgrOrig = sectionOf(doc, "UPGR");
    const Buffer dimOrig = sectionOf(doc, "DIM ");
    const Buffer mtxmOrig = sectionOf(doc, "MTXM");

    auto gridCount = [](int w, int h) {
        int n = 0;
        for (int y = kMapRevealerGridStep; y < h; y += kMapRevealerGridStep) {
            for (int x = kMapRevealerGridStep; x < w; x += kMapRevealerGridStep) n++;
        }
        return n;
    };
    auto countRevealers = [&doc](int player) {
        int n = 0;
        for (const auto& u : doc.units()) {
            if (u.type != kMapRevealerUnitType) continue;
            if (player < 0 ? u.owner < 8 : u.owner == player) n++;
        }
        return n;
    };

    const int perPlayer = gridCount(doc.width(), doc.height());
    const std::size_t baseUnits = doc.units().size();
    const std::vector<UnitRecord> unitsBefore = doc.units();

    const int placed0 = PlaceMapRevealers(doc, 0);
    const bool placeOk = perPlayer > 0 && placed0 == perPlayer && countRevealers(0) == perPlayer;

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"放置小雷达";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = unitsBefore;
    cmd.unitsAfter = doc.units();
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool radarUndo = doc.units().size() == baseUnits && countRevealers(0) == 0;
    history.Redo(doc);
    const bool radarRedo = doc.units().size() == baseUnits + perPlayer &&
                           countRevealers(0) == perPlayer;

    const int placedAll = PlaceMapRevealers(doc, -1);
    const bool placeAllOk = placedAll == 7 * perPlayer && countRevealers(-1) == 8 * perPlayer;
    const int removed0 = RemoveMapRevealers(doc, 0);
    const int removedAll = RemoveMapRevealers(doc, -1);
    const bool removeOk = removed0 == perPlayer && removedAll == 7 * perPlayer &&
                          countRevealers(-1) == 0 && doc.units().size() == baseUnits;

    // 迷雾：按玩家清/置位，其它玩家的位保持不变
    const Buffer fogOrig = doc.fog().Serialize();
    bool fogOk = fogOrig.size() == static_cast<std::size_t>(doc.width()) * doc.height();
    const Buffer doodadsNow = doc.doodads().Serialize();
    SetFogForPlayers(doc, 0, false);
    const Buffer fogAfter = doc.fog().Serialize();
    if (fogAfter.size() == fogOrig.size()) {
        for (std::size_t i = 0; i < fogAfter.size(); i++) {
            if ((fogAfter[i] & 0x01u) != 0) fogOk = false;
            if ((fogAfter[i] & 0xFEu) != (fogOrig[i] & 0xFEu)) fogOk = false;
        }
    } else {
        fogOk = false;
    }

    EditCommand fcmd;
    fcmd.label = L"禁止迷雾";
    fcmd.tilesBefore = doc.tiles();
    fcmd.tilesAfter = doc.tiles();
    fcmd.unitsBefore = doc.units();
    fcmd.unitsAfter = doc.units();
    fcmd.hasDecor = true;
    fcmd.doodadsBefore = doodadsNow;
    fcmd.doodadsAfter = doodadsNow;
    fcmd.fogBefore = fogOrig;
    fcmd.fogAfter = fogAfter;
    history.Push(std::move(fcmd));
    history.Undo(doc);
    const bool fogUndo = doc.fog().Serialize() == fogOrig;
    history.Redo(doc);
    const bool fogRedo = doc.fog().Serialize() == fogAfter;

    SetFogForPlayers(doc, 1, true);
    const Buffer fogP1 = doc.fog().Serialize();
    bool p1Ok = fogP1.size() == fogAfter.size();
    for (std::size_t i = 0; i < fogP1.size() && p1Ok; i++) {
        if ((fogP1[i] & 0x02u) == 0) p1Ok = false;
    }
    SetFogForPlayers(doc, -1, false);
    const Buffer fogNone = doc.fog().Serialize();
    bool noneOk = fogNone.size() == fogOrig.size();
    for (std::size_t i = 0; i < fogNone.size() && noneOk; i++) {
        if (fogNone[i] != 0x00) noneOk = false;
    }
    SetFogForPlayers(doc, -1, true);
    const Buffer fogFull = doc.fog().Serialize();
    bool fullOk = fogFull.size() == fogOrig.size();
    for (std::size_t i = 0; i < fogFull.size() && fullOk; i++) {
        if (fogFull[i] != 0xFF) fullOk = false;
    }
    fogOk = fogOk && p1Ok && noneOk && fullOk;

    // 最终落盘状态：Player 2 的小雷达 + 禁止 Player 2 迷雾（其余位保持 1）
    PlaceMapRevealers(doc, 1);
    SetFogForPlayers(doc, 1, false);
    const bool saved = doc.Save(outPath);

    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    int checkRevealers = 0;
    for (const auto& u : check.units()) {
        if (u.type == kMapRevealerUnitType && u.owner == 1) checkRevealers++;
    }
    const Buffer mask = check.fog().Serialize();
    bool maskOk = mask.size() == static_cast<std::size_t>(check.width()) * check.height();
    for (std::size_t i = 0; i < mask.size() && maskOk; i++) {
        if (mask[i] != 0xFD) maskOk = false;   // 仅 bit1 被清除
    }
    const bool fieldsOk = reopened && checkRevealers == perPlayer && maskOk &&
                          check.units().size() == baseUnits + perPlayer;
    const bool preserved = reopened && sectionOf(check, "DD2 ") == dd2Orig &&
                           sectionOf(check, "THG2") == thg2Orig &&
                           sectionOf(check, "UPGR") == upgrOrig &&
                           sectionOf(check, "DIM ") == dimOrig &&
                           sectionOf(check, "MTXM") == mtxmOrig;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nplace=%d\nremove=%d\nfog=%d\nundo=%d\nredo=%d\n"
                L"saved=%d\nreopened=%d\nfields=%d\npreserved=%d\n",
                placeOk && placeAllOk ? 1 : 0, removeOk ? 1 : 0, fogOk ? 1 : 0,
                radarUndo && fogUndo ? 1 : 0, radarRedo && fogRedo ? 1 : 0,
                saved ? 1 : 0, reopened ? 1 : 0, fieldsOk ? 1 : 0,
                preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = placeOk && placeAllOk && removeOk && fogOk && radarUndo && fogUndo &&
                    radarRedo && fogRedo && saved && reopened && fieldsOk && preserved;
    return ok ? 0 : 2;
}

// --scenario-text-test=<输出scx>：WAV 声音 / SWNM 开关名 / MBRF 任务简报回归
int RunScenarioTextTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }
    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer wavBefore = sectionOf(doc, "WAV ");
    const Buffer swnmBefore = sectionOf(doc, "SWNM");
    const Buffer mbrfBefore = sectionOf(doc, "MBRF");
    const Buffer strBefore = sectionOf(doc, "STR ");
    const Buffer trigBefore = sectionOf(doc, "TRIG");
    const Buffer mrgnBefore = sectionOf(doc, "MRGN");
    const Buffer dd2Orig = sectionOf(doc, "DD2 ");
    const Buffer thg2Orig = sectionOf(doc, "THG2");
    const Buffer upgrOrig = sectionOf(doc, "UPGR");
    const Buffer dimOrig = sectionOf(doc, "DIM ");
    const Buffer mtxmOrig = sectionOf(doc, "MTXM");

    // 声音：槽 5 指向新建的 STR
    const int sidSound = static_cast<int>(std::max<std::size_t>(1, doc.strings().Count()));
    doc.mutableStrings().Set(static_cast<std::size_t>(sidSound), "sound\\test.wav");
    doc.mutableSounds().Set(5, static_cast<u32>(sidSound));
    const bool soundOk = doc.sounds().Get(5) == static_cast<u32>(sidSound);

    // 开关名：开关 4（下标 3）
    const int sidSwitch = static_cast<int>(std::max<std::size_t>(1, doc.strings().Count()));
    doc.mutableStrings().Set(static_cast<std::size_t>(sidSwitch), "Switch 3 Name");
    doc.mutableSwitchNames().Set(3, static_cast<u32>(sidSwitch));
    const bool switchOk = doc.switchNames().Get(3) == static_cast<u32>(sidSwitch);

    // 任务简报：编辑第一条文本/目标/传输行动；没有则补一条
    auto& blocks = doc.mutableBriefing().MutableItems();
    if (blocks.empty()) {
        Trigger t;
        t.conditions[0].type = 13;      // 任务简报
        t.actions[0].type = 3;          // 文本消息
        t.actions[0].player = 0;
        blocks.push_back(t);
    }
    int bb = -1, aa = -1;
    for (int b = 0; b < static_cast<int>(blocks.size()) && aa < 0; b++) {
        for (int a = 0; a < static_cast<int>(TriggerSection::kActionCount); a++) {
            const u8 type = blocks[static_cast<std::size_t>(b)].actions[a].type;
            if (type == 0) break;
            if (type == 3 || type == 4 || type == 8) {
                bb = b;
                aa = a;
                break;
            }
        }
    }
    if (aa < 0) {
        bb = 0;
        for (int a = 0; a < static_cast<int>(TriggerSection::kActionCount); a++) {
            if (blocks[0].actions[a].type == 0) {
                blocks[0].actions[a].type = 3;
                blocks[0].actions[a].player = 0;
                aa = a;
                break;
            }
        }
    }
    const int sidBrief = static_cast<int>(std::max<std::size_t>(1, doc.strings().Count()));
    doc.mutableStrings().Set(static_cast<std::size_t>(sidBrief), "Briefing Text");
    if (bb >= 0 && aa >= 0) {
        blocks[static_cast<std::size_t>(bb)].actions[aa].stringId = static_cast<u32>(sidBrief);
    }
    const bool briefingOk = bb >= 0 && aa >= 0;
    doc.MarkDirty();

    const Buffer wavAfter = doc.sounds().Serialize();
    const Buffer swnmAfter = doc.switchNames().Serialize();
    const Buffer mbrfAfter = doc.briefing().Serialize();
    const Buffer strAfter = doc.strings().Serialize();

    EditHistory history;
    EditCommand cmd;
    cmd.label = L"声音/开关名/简报";
    cmd.tilesBefore = doc.tiles();
    cmd.tilesAfter = doc.tiles();
    cmd.unitsBefore = doc.units();
    cmd.unitsAfter = doc.units();
    cmd.hasMeta = true;
    cmd.triggersBefore = trigBefore;
    cmd.triggersAfter = trigBefore;
    cmd.locationsBefore = mrgnBefore;
    cmd.locationsAfter = mrgnBefore;
    cmd.stringsBefore = strBefore;
    cmd.stringsAfter = strAfter;
    cmd.hasScenarioExtra = true;
    cmd.wavBefore = wavBefore;
    cmd.wavAfter = wavAfter;
    cmd.swnmBefore = swnmBefore;
    cmd.swnmAfter = swnmAfter;
    cmd.mbrfBefore = mbrfBefore;
    cmd.mbrfAfter = mbrfAfter;
    history.Push(std::move(cmd));
    history.Undo(doc);
    const bool undoOk = doc.sounds().Serialize() == wavBefore &&
                        doc.switchNames().Serialize() == swnmBefore &&
                        doc.briefing().Serialize() == mbrfBefore &&
                        doc.strings().Serialize() == strBefore;
    history.Redo(doc);
    const bool redoOk = doc.sounds().Serialize() == wavAfter &&
                        doc.switchNames().Serialize() == swnmAfter &&
                        doc.briefing().Serialize() == mbrfAfter &&
                        doc.strings().Serialize() == strAfter;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const u32 soundSid = check.sounds().Get(5);
    const u32 switchSid = check.switchNames().Get(3);
    bool briefTextOk = false;
    for (const auto& t : check.briefing().Items()) {
        for (int a = 0; a < static_cast<int>(TriggerSection::kActionCount); a++) {
            if (t.actions[a].type == 0) break;
            if ((t.actions[a].type == 3 || t.actions[a].type == 4 || t.actions[a].type == 8) &&
                check.strings().At(t.actions[a].stringId) == "Briefing Text") {
                briefTextOk = true;
            }
        }
    }
    const bool fieldsOk = reopened && soundSid != 0 &&
                          check.strings().At(soundSid) == "sound\\test.wav" &&
                          switchSid != 0 && check.strings().At(switchSid) == "Switch 3 Name" &&
                          briefTextOk && sectionOf(check, "WAV ").size() == 2048 &&
                          sectionOf(check, "SWNM").size() == 1024 &&
                          sectionOf(check, "MBRF").size() % 2400 == 0;
    const bool preserved = reopened && sectionOf(check, "DD2 ") == dd2Orig &&
                           sectionOf(check, "THG2") == thg2Orig &&
                           sectionOf(check, "UPGR") == upgrOrig &&
                           sectionOf(check, "DIM ") == dimOrig &&
                           sectionOf(check, "MTXM") == mtxmOrig;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nsounds=%d\nswitches=%d\nbriefing=%d\nundo=%d\nredo=%d\n"
                L"saved=%d\nreopened=%d\nfields=%d\npreserved=%d\n",
                soundOk ? 1 : 0, switchOk ? 1 : 0, briefingOk ? 1 : 0, undoOk ? 1 : 0,
                redoOk ? 1 : 0, saved ? 1 : 0, reopened ? 1 : 0, fieldsOk ? 1 : 0,
                preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = soundOk && switchOk && briefingOk && undoOk && redoOk && saved &&
                    reopened && fieldsOk && preserved;
    return ok ? 0 : 2;
}

// --map-tools-test=<输出scx>：转换地形（删除 DD2）/ 矿物外形随机回归
int RunMapToolsTest(const std::wstring& mapPath, const std::wstring& outPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, ParseTsArg())) {
        WriteDiag(L"open=FAIL\n");
        return 2;
    }
    auto sectionOf = [](const MapDocument& d, const char* name) -> Buffer {
        const Buffer* data = d.chk().Find(name);
        return data ? *data : Buffer();
    };
    const Buffer maskOrig = sectionOf(doc, "MASK");
    const Buffer thg2Orig = sectionOf(doc, "THG2");
    const Buffer upgrOrig = sectionOf(doc, "UPGR");
    const Buffer dimOrig = sectionOf(doc, "DIM ");
    const Buffer mtxmOrig = sectionOf(doc, "MTXM");

    // 纠正装饰物错误：删除越界记录，并把 enabled 规范为 0/1
    DoodadTable probe;
    Buffer probeBytes(3 * 8, 0);
    auto put16 = [](Buffer& b, std::size_t at, u16 v) {
        b[at] = static_cast<u8>(v & 0xFF);
        b[at + 1] = static_cast<u8>(v >> 8);
    };
    put16(probeBytes, 0, 1);                                        // 合法记录
    put16(probeBytes, 8, 1);                                        // 越界 x
    put16(probeBytes, 8 + 2, static_cast<u16>(doc.width() * kTilePixels));
    put16(probeBytes, 16, 1);                                       // enabled=2 需规范化
    probeBytes[16 + 7] = 2;
    probe.Parse(probeBytes);
    const int fixCount = probe.FixErrors(doc.width(), doc.height());
    bool fixOk = fixCount == 2 && probe.Count() == 2 && probe.Items()[1].enabled == 1;
    for (const Doodad& d : probe.Items()) {
        if (static_cast<int>(d.x) >= doc.width() * kTilePixels ||
            static_cast<int>(d.y) >= doc.height() * kTilePixels) {
            fixOk = false;
        }
    }

    // 转换地形：删除整图 DD2 记录（MTXM/THG2 保留）
    const int doodadsBefore = static_cast<int>(doc.doodads().Count());
    const Buffer doodadsBytesBefore = doc.doodads().Serialize();
    const Buffer fogNow = doc.fog().Serialize();
    const Buffer spritesNow = doc.sprites().Serialize();
    const int removed = doc.mutableDoodads().RemoveInTileRect(0, 0, doc.width() - 1,
                                                              doc.height() - 1);
    const bool convertOk = removed == doodadsBefore && doc.doodads().Count() == 0;
    const Buffer doodadsBytesAfter = doc.doodads().Serialize();

    EditHistory history;
    EditCommand dcmd;
    dcmd.label = L"转换地形";
    dcmd.tilesBefore = doc.tiles();
    dcmd.tilesAfter = doc.tiles();
    dcmd.unitsBefore = doc.units();
    dcmd.unitsAfter = doc.units();
    dcmd.hasDecor = true;
    dcmd.doodadsBefore = doodadsBytesBefore;
    dcmd.doodadsAfter = doodadsBytesAfter;
    dcmd.fogBefore = fogNow;
    dcmd.fogAfter = fogNow;
    dcmd.spritesBefore = spritesNow;
    dcmd.spritesAfter = spritesNow;
    history.Push(std::move(dcmd));
    history.Undo(doc);
    const bool convertUndo = doc.doodads().Count() == doodadsBefore;
    history.Redo(doc);
    const bool convertRedo = doc.doodads().Count() == 0;

    // 矿物外形随机：先统一为 176，再随机到 176..178
    std::size_t mineralCount = 0;
    for (auto& u : doc.mutableUnits()) {
        if (u.type >= 176 && u.type <= 178) {
            u.type = 176;
            mineralCount++;
        }
    }
    const std::vector<UnitRecord> unitsPre = doc.units();
    const int changedMinerals = RandomizeMineralTypes(doc);
    const std::vector<UnitRecord> unitsPost = doc.units();
    bool mineralsOk = mineralCount > 0 && changedMinerals >= 0 &&
                      static_cast<std::size_t>(changedMinerals) <= mineralCount;
    if (unitsPost.size() == unitsPre.size()) {
        for (std::size_t i = 0; i < unitsPost.size(); i++) {
            const UnitRecord& a = unitsPre[i];
            const UnitRecord& b = unitsPost[i];
            if (a.serial != b.serial || a.resource != b.resource || a.x != b.x || a.y != b.y ||
                a.owner != b.owner) {
                mineralsOk = false;
                break;
            }
            if (a.type >= 176 && a.type <= 178) {
                if (b.type < 176 || b.type > 178) mineralsOk = false;
            } else if (a.type != b.type) {
                mineralsOk = false;
            }
        }
    } else {
        mineralsOk = false;
    }

    EditCommand ucmd;
    ucmd.label = L"矿物外形随机";
    ucmd.tilesBefore = doc.tiles();
    ucmd.tilesAfter = doc.tiles();
    ucmd.unitsBefore = unitsPre;
    ucmd.unitsAfter = unitsPost;
    history.Push(std::move(ucmd));
    history.Undo(doc);
    bool mineralUndo = doc.units().size() == unitsPre.size();
    for (std::size_t i = 0; i < doc.units().size() && mineralUndo; i++) {
        if (doc.units()[i].type != unitsPre[i].type) mineralUndo = false;
    }
    history.Redo(doc);
    bool mineralRedo = doc.units().size() == unitsPost.size();
    for (std::size_t i = 0; i < doc.units().size() && mineralRedo; i++) {
        if (doc.units()[i].type != unitsPost[i].type) mineralRedo = false;
    }

    // 保存地图图像（渲染到内存，验证尺寸与颜色）
    bool imageOk = false;
    if (doc.hasTileset()) {
        TerrainRenderer renderer;
        renderer.SetTileset(&doc.tileset());
        TerrainImage image;
        imageOk = renderer.Render(doc.tiles(), doc.width(), doc.height(), 8, image) &&
                  image.width == doc.width() * 8 && image.height == doc.height() * 8 &&
                  !image.pixels.empty();
        if (imageOk) {
            std::unordered_set<unsigned> colors;
            for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
                colors.insert((static_cast<unsigned>(image.pixels[i]) << 16) |
                              (static_cast<unsigned>(image.pixels[i + 1]) << 8) |
                              image.pixels[i + 2]);
            }
            imageOk = colors.size() > 1;
        }
    }

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, ParseTsArg());
    bool mineralSaved = reopened && check.units().size() == unitsPost.size();
    if (mineralSaved) {
        for (std::size_t i = 0; i < check.units().size(); i++) {
            const u16 t = check.units()[i].type;
            if (t >= 176 && t <= 178) continue;
            if (t != unitsPost[i].type) {
                mineralSaved = false;
                break;
            }
        }
    }
    const bool fieldsOk = reopened && check.doodads().Count() == 0 && mineralSaved;
    const bool preserved = reopened && sectionOf(check, "MASK") == maskOrig &&
                           sectionOf(check, "THG2") == thg2Orig &&
                           sectionOf(check, "UPGR") == upgrOrig &&
                           sectionOf(check, "DIM ") == dimOrig &&
                           sectionOf(check, "MTXM") == mtxmOrig;

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"open=OK\nfix=%d\nconvert=%d\nundo=%d\nredo=%d\nminerals=%d\nimage=%d\n"
                L"saved=%d\nreopened=%d\nfields=%d\npreserved=%d\n",
                fixOk ? 1 : 0, convertOk ? 1 : 0, convertUndo ? 1 : 0, convertRedo ? 1 : 0,
                mineralsOk ? 1 : 0, imageOk ? 1 : 0,
                saved ? 1 : 0, reopened ? 1 : 0, fieldsOk ? 1 : 0, preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = fixOk && convertOk && convertUndo && convertRedo && mineralsOk && imageOk &&
                    saved && reopened && fieldsOk && preserved;
    return ok ? 0 : 2;
}

// --recent-test：最近地图 MRU 与设置文件读写回归
int RunRecentTest() {
    AppSettings& settings = AppSettings::Instance();
    if (settings.filePath().empty()) {
        WriteDiag(L"recent=FAIL no-settings-dir\n");
        return 2;
    }

    settings.ClearRecentMaps();
    settings.SetUndoLimit(50);
    const bool saveClear = settings.Save();
    const bool loadClear = settings.Load();
    const bool cleared = saveClear && loadClear && settings.recentMaps().empty();

    for (int i = 0; i < 10; i++) {
        settings.AddRecentMap(L"C:\\maps\\map" + std::to_wstring(i) + L".scx");
    }
    const bool capOk = settings.recentMaps().size() == AppSettings::kMaxRecent &&
                       settings.recentMaps().front() == L"C:\\maps\\map9.scx";

    settings.AddRecentMap(L"C:\\maps\\map5.scx");   // 去重并置顶
    int occurrences = 0;
    for (const std::wstring& path : settings.recentMaps()) {
        if (::_wcsicmp(path.c_str(), L"C:\\maps\\map5.scx") == 0) occurrences++;
    }
    const bool dedupeOk = settings.recentMaps().size() == AppSettings::kMaxRecent &&
                          settings.recentMaps().front() == L"C:\\maps\\map5.scx" &&
                          occurrences == 1;

    settings.SetUndoLimit(75);
    settings.SetDefaultMineral(12);
    settings.SetDefaultGas(3);
    settings.SetGameDir(L"C:\\StarCraft");
    settings.SetMpqFiles({L"C:\\a.mpq", L"D:\\b.mpq"});
    settings.SetBackupEnabled(false);
    settings.SetBackupCount(3);
    settings.SetAutoBackupEnabled(true);
    settings.SetAutoBackupMinutes(7);
    {
        ViewState& view = settings.mutableView();
        view.zoomPx = 16;
        view.gridEnabled = false;
        view.gridCellX = 48;
        view.gridColorId = 0x8039;
        view.gridColorValue = 220;
        view.layer = 4;
        view.showUnits = false;
        view.hideMinimap = true;
        view.showFog = true;
    }
    settings.SetSoundEnabled(false);
    const bool saved = settings.Save();
    settings.ClearRecentMaps();
    settings.SetUndoLimit(50);
    settings.SetDefaultMineral(8);
    settings.SetDefaultGas(1);
    const bool reloaded = settings.Load();
    const bool persisted = reloaded && settings.recentMaps().size() == AppSettings::kMaxRecent &&
                           settings.recentMaps().front() == L"C:\\maps\\map5.scx";
    const bool undoOk = settings.undoLimit() == 75;
    const bool settingsOk = settings.defaultMineral() == 12 && settings.defaultGas() == 3 &&
                            settings.gameDir() == L"C:\\StarCraft" &&
                            settings.mpqFiles().size() == 2 &&
                            settings.mpqFiles()[0] == L"C:\\a.mpq" &&
                            !settings.backupEnabled() && settings.backupCount() == 3 &&
                            settings.autoBackupEnabled() &&
                            settings.autoBackupMinutes() == 7 &&
                            settings.view().zoomPx == 16 && !settings.view().gridEnabled &&
                            settings.view().gridCellX == 48 && settings.view().layer == 4 &&
                            !settings.view().showUnits && settings.view().hideMinimap &&
                            settings.view().showFog && !settings.soundEnabled();

    // 失效最近项移除：去重后再删除首项
    const bool frontBefore = !settings.recentMaps().empty();
    if (frontBefore) settings.RemoveRecentMap(settings.recentMaps().front());
    const bool removeOk = frontBefore && settings.recentMaps().size() == 7 &&
                          settings.recentMaps().front() == L"C:\\maps\\map9.scx";

    wchar_t msg[384];
    ::wsprintfW(msg,
                L"recent=1\nsaveClear=%d\nloadClear=%d\ncleared=%d\ncap=%d\n"
                L"dedupe=%d\nsaved=%d\nreloaded=%d\npersisted=%d\nundo=%d\nsettings=%d\n"
        L"remove=%d\n",
                saveClear ? 1 : 0, loadClear ? 1 : 0,
                cleared ? 1 : 0, capOk ? 1 : 0, dedupeOk ? 1 : 0, saved ? 1 : 0,
                reloaded ? 1 : 0, persisted ? 1 : 0, undoOk ? 1 : 0, settingsOk ? 1 : 0,
                removeOk ? 1 : 0);
    WriteDiag(msg);
    return (cleared && capOk && dedupeOk && saved && reloaded && persisted && undoOk &&
            settingsOk && removeOk)
               ? 0
               : 2;
}

// --backup-test：备份、上限裁剪、恢复与删除
int RunBackupTest(const std::wstring& mapPath, const std::wstring& settingsDir) {
    if (mapPath.empty() || settingsDir.empty()) {
        WriteDiag(L"backup=FAIL missing --open / --settings-dir\n");
        return 2;
    }
    const std::wstring target = settingsDir + L"\\backup_probe.scx";
    if (!::CopyFileW(mapPath.c_str(), target.c_str(), FALSE)) {
        WriteDiag(L"backup=FAIL copy\n");
        return 2;
    }

    auto readAll = [](const std::wstring& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const std::string original = readAll(target);

    ClearAllBackups();
    bool made = true;
    for (int i = 0; i < 4; i++) made = MakeBackup(target, 3) && made;

    const std::vector<BackupGroup> groups = ListBackupGroups();
    const bool groupsOk = made && groups.size() == 1 && groups[0].source == target;
    std::vector<BackupEntry> entries;
    if (!groups.empty()) entries = ListBackups(groups[0].key);
    const bool pruneOk = entries.size() == 3;

    {
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out << "broken";
    }
    const bool restored = !entries.empty() && RestoreBackup(entries.front().file, target) &&
                          readAll(target) == original;

    if (!groups.empty()) DeleteBackupGroup(groups[0].key);
    const bool cleaned = ListBackupGroups().empty();
    ::DeleteFileW(target.c_str());

    wchar_t msg[384];
    ::wsprintfW(msg,
                L"backup=1\nmade=%d\ngroups=%d\nprune=%d\nentries=%u\nrestored=%d\ncleaned=%d\n",
                made ? 1 : 0, groupsOk ? 1 : 0, pruneOk ? 1 : 0,
                static_cast<unsigned>(entries.size()), restored ? 1 : 0, cleaned ? 1 : 0);
    WriteDiag(msg);
    return (made && groupsOk && pruneOk && restored && cleaned) ? 0 : 2;
}

// --tileset-render-test=<输出目录>：8 套地形集逐套创建合成地图，渲染结果与
// 独立解码的参考图逐像素比对；每套导出一张 BMP，哈希必须互不相同。
int RunTilesetRenderTest(const std::wstring& tsDir, const std::wstring& outDir) {
    static const wchar_t* const kPrefix[8] = {
        L"StarDat.mpq_badlands", L"StarDat.mpq_platform", L"StarDat.mpq_install",
        L"StarDat.mpq_ashworld", L"StarDat.mpq_jungle",  L"BrooDat.mpq_desert",
        L"BrooDat.mpq_ice",      L"BrooDat.mpq_twilight",
    };
    if (tsDir.empty() || outDir.empty()) { WriteDiag(L"no-dir\n"); return 2; }
    ::CreateDirectoryW(outDir.c_str(), nullptr);

    auto readFile = [](const std::wstring& path, Buffer& out) -> bool {
        std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
        if (!in) return false;
        const std::streamoff size = in.tellg();
        if (size <= 0) return false;
        in.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(out.data()), size);
        return in.good();
    };
    auto u16At = [](const Buffer& b, std::size_t off) -> u16 {
        return static_cast<u16>(b[off] | (b[off + 1] << 8));
    };

    std::wstring report;
    int passed = 0;
    std::unordered_set<unsigned long long> hashes;

    for (int era = 0; era < 8; era++) {
        Buffer cv5, vx4, vr4, wpe;
        const std::wstring base = tsDir + L"\\" + kPrefix[era];
        const bool files = readFile(base + L".cv5", cv5) && readFile(base + L".vx4", vx4) &&
                           readFile(base + L".vr4", vr4) && readFile(base + L".wpe", wpe) &&
                           wpe.size() >= 256 * 4;

        MapDocument doc;
        const bool created = doc.CreateNew(16, 16, static_cast<u16>(era), tsDir);

        // 合成 MTXM：覆盖地形集的前若干组与全部 16 个索引
        const int groups = static_cast<int>(cv5.size() / 52);
        Buffer& mtxm = doc.mutableTiles();
        const bool patternOk = groups > 0 && mtxm.size() == 16 * 16 * 2;
        for (int y = 0; y < 16 && patternOk; y++) {
            for (int x = 0; x < 16; x++) {
                const int g = ((y * 16 + x) * 37) % groups;
                const u16 v = static_cast<u16>((g << 4) | ((x + y) % 16));
                mtxm[(y * 16 + x) * 2] = static_cast<u8>(v & 0xFF);
                mtxm[(y * 16 + x) * 2 + 1] = static_cast<u8>(v >> 8);
            }
        }

        TerrainRenderer renderer;
        renderer.SetTileset(&doc.tileset());
        TerrainImage image;
        const bool rendered = created && doc.hasTileset() &&
                              renderer.Render(doc.tiles(), doc.width(), doc.height(), 8, image);
        const bool dimsOk = image.width == 128 && image.height == 128;

        // 独立参考解码：直接读 CV5/VX4/VR4/WPE，不经过 Tileset/TerrainRenderer
        bool pixelsOk = false;
        int colors = 0;
        if (files && dimsOk) {
            const int step = 4;   // 8 px/瓦片 -> 每个源像素覆盖 4×4
            const int megaCount = static_cast<int>(vx4.size() / 32);
            const int vrCount = static_cast<int>(vr4.size() / 64);
            bool mismatch = false;
            std::unordered_set<unsigned> seen;
            for (int ty = 0; ty < 16 && !mismatch; ty++) {
                for (int tx = 0; tx < 16 && !mismatch; tx++) {
                    const u16 v = u16At(mtxm, (ty * 16 + tx) * 2);
                    const int group = (v & 0x7FF0) >> 4;
                    const int index = v & 0x0F;
                    const int mega =
                        group < groups ? u16At(cv5, group * 52 + 20 + index * 2) : 0;
                    u8 block[1024] = {};
                    if (mega < megaCount) {
                        for (int k = 0; k < 16; k++) {
                            const u16 ref =
                                u16At(vx4, static_cast<std::size_t>(mega) * 32 + k * 2);
                            const int vrid = ref >> 1;
                            if (vrid >= vrCount) continue;
                            const u8* src = vr4.data() + static_cast<std::size_t>(vrid) * 64;
                            const int bx = (k % 4) * 8;
                            const int by = (k / 4) * 8;
                            const bool hflip = (ref & 1) != 0;
                            for (int yy = 0; yy < 8; yy++) {
                                for (int xx = 0; xx < 8; xx++) {
                                    block[(by + yy) * 32 + bx + xx] =
                                        src[yy * 8 + (hflip ? (7 - xx) : xx)];
                                }
                            }
                        }
                    }
                    for (int py = 0; py < 8 && !mismatch; py++) {
                        for (int px = 0; px < 8; px++) {
                            const u8 idx = block[(py * step) * 32 + px * step];
                            const u8* rgb = wpe.data() + static_cast<std::size_t>(idx) * 4;
                            const std::size_t off =
                                ((static_cast<std::size_t>(ty * 8 + py)) * image.width +
                                 (tx * 8 + px)) * 4;
                            if (image.pixels[off + 0] != rgb[2] ||
                                image.pixels[off + 1] != rgb[1] ||
                                image.pixels[off + 2] != rgb[0] ||
                                image.pixels[off + 3] != 0xFF) {
                                mismatch = true;
                            }
                            seen.insert((static_cast<unsigned>(rgb[0]) << 16) |
                                        (static_cast<unsigned>(rgb[1]) << 8) | rgb[2]);
                        }
                    }
                }
            }
            pixelsOk = !mismatch;
            colors = static_cast<int>(seen.size());
        }

        // 缩放档位：25%..400% 对应 8..128 px/瓦片；非整除档位（33/66/150..）也要正确
        bool zoomOk = false;
        if (files && created && doc.hasTileset()) {
            TerrainImage ref32;
            zoomOk = renderer.Render(doc.tiles(), doc.width(), doc.height(), 32, ref32);
            static const int kZooms[] = {11, 21, 48, 128};
            for (int z : kZooms) {
                if (!zoomOk) break;
                TerrainImage zi;
                if (!renderer.Render(doc.tiles(), doc.width(), doc.height(), z, zi) ||
                    zi.width != doc.width() * z || zi.height != doc.height() * z) {
                    zoomOk = false;
                    break;
                }
                for (int y = 0; y < zi.height && zoomOk; y++) {
                    const int sy = (y * 32) / z;
                    const u8* srcRow = ref32.pixels.data() +
                                       static_cast<std::size_t>(sy) * ref32.width * 4;
                    const u8* dstRow = zi.pixels.data() +
                                       static_cast<std::size_t>(y) * zi.width * 4;
                    for (int x = 0; x < zi.width; x++) {
                        const int sx = (x * 32) / z;
                        if (std::memcmp(dstRow + x * 4, srcRow + sx * 4, 4) != 0) {
                            zoomOk = false;
                            break;
                        }
                    }
                }
            }
        }

        unsigned long long hash = 1469598103934665603ull;
        for (u8 b : image.pixels) {
            hash ^= b;
            hash *= 1099511628211ull;
        }
        const bool unique = hashes.insert(hash).second;
        const std::wstring bmp = outDir + L"\\ts_" + std::to_wstring(era) + L"_" +
                                 kPrefix[era] + L".bmp";
        const bool bmpOk = rendered && TerrainRenderer::WriteBmp24(bmp, image);
        const bool ok = files && created && patternOk && rendered && dimsOk && pixelsOk &&
                        zoomOk && colors > 1 && unique && bmpOk;
        if (ok) passed++;

        wchar_t line[512];
        ::wsprintfW(line,
                    L"era=%d name=%s files=%d created=%d groups=%d megas=%d vr=%d "
                    L"render=%d dims=%d pixels=%d zoom=%d colors=%d unique=%d bmp=%d hash=%016I64x\n",
                    era, doc.eraName(), files ? 1 : 0, created ? 1 : 0, groups,
                    static_cast<int>(vx4.size() / 32), static_cast<int>(vr4.size() / 64),
                    rendered ? 1 : 0, dimsOk ? 1 : 0, pixelsOk ? 1 : 0, zoomOk ? 1 : 0,
                    colors, unique ? 1 : 0, bmpOk ? 1 : 0, hash);
        report += line;
    }

    wchar_t tail[32];
    ::wsprintfW(tail, L"pass=%d/8\n", passed);
    report += tail;
    WriteDiag(report);
    return passed == 8 ? 0 : 2;
}

// --section-test：校验 TRIG/MRGN/STR 的解析与无损重建
int RunSectionTest(const std::wstring& mapPath) {
    MapDocument doc;
    if (!doc.Open(mapPath, std::wstring())) { WriteDiag(L"open=FAIL\n"); return 2; }

    auto compare = [&doc](const char* name, const Buffer& rebuilt) {
        const Buffer* orig = doc.chk().Find(name);
        if (!orig) return -1;                        // 段不存在
        return orig->size() == rebuilt.size() &&
               std::memcmp(orig->data(), rebuilt.data(), orig->size()) == 0 ? 1 : 0;
    };

    const int trig = compare("TRIG", doc.triggers().Serialize());
    const int mrgn = compare("MRGN", doc.locations().Serialize());
    const int str = compare("STR ", doc.strings().Serialize());

    auto sectionSize = [&doc](const char* name) -> int {
        const Buffer* data = doc.chk().Find(name);
        return data ? static_cast<int>(data->size()) : -1;
    };

    int unit101 = 0;
    for (const auto& u : doc.units()) {
        if (u.type == 101) unit101++;   // Map Revealer（小雷达）
    }
    wchar_t msg[768];
    ::wsprintfW(msg,
                L"trigBlocks=%u\nmrgn=%u\nstr=%u\ntrigEq=%d\nmrgnEq=%d\nstrEq=%d\n"
                L"OWNR=%d\nSIDE=%d\nFORC=%d\nCOLR=%d\nUPGR=%d\nTECH=%d\n"
                L"DD2=%d\nTHG2=%d\nMASK=%d\nMASKexpect=%d\nunit101=%d\n"
                L"WAV=%d\nSWNM=%d\nMBRF=%d\n",
                static_cast<unsigned>(doc.triggers().BlockCount()),
                static_cast<unsigned>(doc.locations().Count()),
                static_cast<unsigned>(doc.strings().Count()), trig, mrgn, str,
                sectionSize("OWNR"), sectionSize("SIDE"), sectionSize("FORC"),
                sectionSize("COLR"), sectionSize("UPGR"), sectionSize("TECH"),
                sectionSize("DD2 "), sectionSize("THG2"), sectionSize("MASK"),
                doc.width() * doc.height(), unit101,
                sectionSize("WAV "), sectionSize("SWNM"), sectionSize("MBRF"));

    auto hexOf = [&doc](const char* name, int maxBytes) -> std::wstring {
        const Buffer* data = doc.chk().Find(name);
        if (!data) return L"-";
        const int n = std::min<int>(maxBytes, static_cast<int>(data->size()));
        std::wstring out;
        out.reserve(static_cast<std::size_t>(n) * 3);
        for (int i = 0; i < n; i++) {
            wchar_t part[4];
            ::wsprintfW(part, L"%02X", (*data)[i]);
            out += part;
            if (i + 1 < n) out += L' ';
        }
        return out;
    };
    std::wstring details;
    details += L"OWNRhex=" + hexOf("OWNR", 12) + L"\n";
    details += L"SIDEhex=" + hexOf("SIDE", 12) + L"\n";
    details += L"FORChex=" + hexOf("FORC", 24) + L"\n";
    details += L"COLRhex=" + hexOf("COLR", 8) + L"\n";
    details += L"UPGRhex=" + hexOf("UPGR", 24) + L"\n";
    details += L"TECHhex=" + hexOf("TECH", 24) + L"\n";
    details += L"DD2hex=" + hexOf("DD2 ", 32) + L"\n";
    details += L"MASKhex=" + hexOf("MASK", 32) + L"\n";

    std::wstring pos;
    int shown = 0;
    for (const auto& u : doc.units()) {
        if (u.type != 101 || shown >= 12) continue;
        wchar_t one[32];
        ::wsprintfW(one, L" (%u,%u)", static_cast<unsigned>(u.x), static_cast<unsigned>(u.y));
        pos += one;
        shown++;
    }
    if (!pos.empty()) details += L"unit101pos=" + pos + L"\n";
    if (const Buffer* upgr = doc.chk().Find("UPGR"); upgr && upgr->size() == 1748) {
        wchar_t global[64];
        ::wsprintfW(global, L"UPGRglobal0=%u/%u\n", static_cast<unsigned>((*upgr)[1104]),
                    static_cast<unsigned>((*upgr)[1150]));
        details += global;
    }
    WriteDiag(std::wstring(msg) + details);

    const bool ok = trig != 0 && mrgn != 0 && str != 0;
    return ok ? 0 : 2;
}

// --menu-test：校验从原程序导入的主菜单与加速键资源
int RunMenuTest(HWND hwnd) {
    const HINSTANCE inst = ::GetModuleHandleW(nullptr);
    HMENU menu = ::LoadMenuW(inst, MAKEINTRESOURCEW(IDR_MAINMENU));
    if (!menu) {
        WriteDiag(L"menu=FAIL\n");
        return 2;
    }

    const int topCount = ::GetMenuItemCount(menu);
    std::wstring topLabels;
    for (int i = 0; i < topCount; i++) {
        wchar_t label[128] = L"";
        ::GetMenuStringW(menu, i, label, 127, MF_BYPOSITION);
        if (!topLabels.empty()) topLabels += L"|";
        topLabels += label;
    }

    int items = 0, seps = 0;
    std::function<void(HMENU)> walk = [&](HMENU m) {
        const int n = ::GetMenuItemCount(m);
        for (int i = 0; i < n; i++) {
            MENUITEMINFOW mii = {sizeof(mii)};
            mii.fMask = MIIM_FTYPE | MIIM_SUBMENU;
            if (!::GetMenuItemInfoW(m, static_cast<UINT>(i), TRUE, &mii)) continue;
            if (mii.fType & MFT_SEPARATOR) seps++;
            items++;
            if (mii.hSubMenu) walk(mii.hSubMenu);
        }
    };
    walk(menu);

    HACCEL accel = ::LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDR_ACCEL_MAIN));
    const int accelCount =
        accel ? static_cast<int>(::CopyAcceleratorTableW(accel, nullptr, 0)) : -1;

    // 抽查关键命令 ID 是否与二进制一致
    const int idFileNew = ::GetMenuItemID(::GetSubMenu(menu, 0), 0);
    const int idEditUndo = ::GetMenuItemID(::GetSubMenu(menu, 1), 0);
    const int idHelpAbout = ::GetMenuItemID(::GetSubMenu(menu, 8), 0);

    // 用 Esc（无修饰键）直接走一次 TranslateAccelerator，确认加速键表已装载
    bool escHandled = false;
    if (accel) {
        MSG probe = {};
        probe.hwnd = hwnd;
        probe.message = WM_KEYDOWN;
        probe.wParam = VK_ESCAPE;
        probe.lParam = 1;
        escHandled = ::TranslateAcceleratorW(hwnd, accel, &probe) != 0;
    }

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"menuTop=%d\nmenuItems=%d\nmenuSeps=%d\naccel=%d\n"
                L"idFileNew=0x%x\nidEditUndo=0x%x\nidHelpAbout=0x%x\nesc=%d\nlabels=%s\n",
                topCount, items, seps, accelCount, idFileNew, idEditUndo, idHelpAbout,
                escHandled ? 1 : 0, topLabels.c_str());
    WriteDiag(msg);

    if (accel) ::DestroyAcceleratorTable(accel);
    ::DestroyMenu(menu);
    const bool ok = topCount == 10 && items == 226 && accelCount == 34 && idFileNew == 0x8005 &&
                    idEditUndo == 0x807F && idHelpAbout == 0x0068 && escHandled;
    return ok ? 0 : 2;
}

void WriteDiag(const std::wstring& text) {
    wchar_t path[MAX_PATH] = L"";
    ::GetTempPathW(MAX_PATH, path);
    ::lstrcatW(path, L"scmdplus_mpq.txt");
    const HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(f, text.c_str(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
    ::CloseHandle(f);
}

// 从 MPQ 取出单个条目：--mpq=<档案> --mpq-extract=<条目> --extract-to=<输出>
int RunMpqExtract(const std::wstring& archivePath, const std::wstring& entryWide,
                  const std::wstring& outPath) {
    if (archivePath.empty() || entryWide.empty() || outPath.empty()) {
        WriteDiag(L"extract=FAIL args\n");
        return 2;
    }
    std::string entry;
    entry.reserve(entryWide.size());
    for (wchar_t c : entryWide) entry.push_back(static_cast<char>(c & 0xFF));

    MpqArchive mpq;
    Buffer data;
    if (!mpq.Open(archivePath) || !mpq.ReadFile(entry, data)) {
        WriteDiag(L"extract=FAIL read\n");
        return 2;
    }
    {
        std::wstring diag;
        MpqArchive::EntryInfo info = {};
        if (mpq.DebugEntryInfo(entry, info)) {
            wchar_t infoMsg[200];
            ::wsprintfW(infoMsg, L"flags=0x%08X pos=%u csize=%u size=%u\n", info.flags,
                        info.blockPos, info.compressedSize, info.fileSize);
            diag += infoMsg;
        }
        wchar_t head[96];
        ::wsprintfW(head, L"head=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    data.size() > 0 ? data[0] : 0, data.size() > 1 ? data[1] : 0,
                    data.size() > 2 ? data[2] : 0, data.size() > 3 ? data[3] : 0,
                    data.size() > 4 ? data[4] : 0, data.size() > 5 ? data[5] : 0,
                    data.size() > 6 ? data[6] : 0, data.size() > 7 ? data[7] : 0);
        diag += head;
        wchar_t sizeMsg[64];
        ::wsprintfW(sizeMsg, L"extract=OK size=%u\n", static_cast<unsigned>(data.size()));
        diag += sizeMsg;
        WriteDiag(diag);
    }
    std::ofstream out(outPath.c_str(), std::ios::binary);
    if (!out) {
        WriteDiag(L"extract=FAIL write\n");
        return 2;
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return 0;
}

// 读取地图容器并输出自检信息：条目数、CHK 大小、DIM
int RunMpqSelfTest(const std::wstring& path) {
    // 先做文件级诊断，区分"路径打不开"和"容器解析失败"
    wchar_t pre[192];
    {
        const HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            ::wsprintfW(pre, L"file=FAIL err=%lu\n", ::GetLastError());
            WriteDiag(pre);
            return 2;
        }
        const DWORD fileSize = ::GetFileSize(h, nullptr);
        BYTE sig[4] = {0};
        DWORD got = 0;
        ::ReadFile(h, sig, 4, &got, nullptr);
        ::CloseHandle(h);
        ::wsprintfW(pre, L"file=OK size=%lu sig=%02X%02X%02X%02X\n",
                    fileSize, sig[0], sig[1], sig[2], sig[3]);
    }

    MpqArchive mpq;
    if (!mpq.Open(path)) {
        std::wstring diag(pre);
        diag += L"open=FAIL\n";
        WriteDiag(diag);
        return 2;
    }

    Buffer chk;
    if (!mpq.ReadScenarioChk(chk)) {
        wchar_t msg[192];
        ::wsprintfW(msg, L"open=OK\nentries=%d\nchk=FAIL\nstage=%s\n",
                    mpq.EntryCount(), mpq.LastError());
        WriteDiag(msg);
        return 2;
    }

    int w = 0, h = 0;
    std::size_t off = 0;
    while (off + 8 <= chk.size()) {
        const char* name = reinterpret_cast<const char*>(chk.data() + off);
        const u32 len = static_cast<u32>(chk[off + 4]) | (static_cast<u32>(chk[off + 5]) << 8) |
                        (static_cast<u32>(chk[off + 6]) << 16) | (static_cast<u32>(chk[off + 7]) << 24);
        if (len > 0x800000u || off + 8 + len > chk.size()) break;
        if (std::memcmp(name, "DIM ", 4) == 0 && len >= 4) {
            w = chk[off + 8] | (chk[off + 9] << 8);
            h = chk[off + 10] | (chk[off + 11] << 8);
            break;
        }
        off += 8 + len;
    }

    wchar_t msg[256];
    ::wsprintfW(msg, L"open=OK\nentries=%d\nchk=%u\ndim=%dx%d\n",
                mpq.EntryCount(), static_cast<unsigned>(chk.size()), w, h);
    WriteDiag(msg);
    return 0;
}

int RunExtractTilesets(const std::wstring& gameDir, const std::wstring& outDir) {
    if (gameDir.empty() || outDir.empty()) {
        WriteDiag(L"ts=FAIL missing --game-dir / --extract-ts\n");
        return 2;
    }

    const TilesetExportResult result = ExtractTilesets(gameDir, outDir);
    wchar_t summary[256];
    ::wsprintfW(summary, L"ts=%s files=%d failed=%d bytes=%u dir=%s\n",
                (result.failed == 0 && result.files == 32) ? L"OK" : L"FAIL",
                result.files, result.failed, static_cast<unsigned>(result.bytes), outDir.c_str());
    WriteDiag(std::wstring(summary) + result.log);
    return (result.failed == 0 && result.files == 32) ? 0 : 2;
}

}  // namespace

App& App::Instance() {
    static App instance;
    return instance;
}

int App::Run(HINSTANCE hInstance) {
    // 无窗口工具：从游戏 MPQ 导出地形集
    if (const std::wstring tsOut = ParseExtractTsArg(); !tsOut.empty()) {
        return RunExtractTilesets(ParseGameDirArg(), tsOut);
    }

    // 本地设置：--settings-dir 可覆盖（测试），默认 %APPDATA%\SCMDPlus
    if (const std::wstring settingsDir = ParseSettingsDirArg(); !settingsDir.empty()) {
        AppSettings::Instance().SetDirectory(settingsDir);
    }
    AppSettings::Instance().Load();
    AppSettings::Instance().SetPersistViewOnExit(!IsHeadlessRun());

    // 最近地图 / 设置持久化回归（无窗口）
    if (HasRecentTestArg()) return RunRecentTest();

    // 自动备份回归（无窗口）
    if (HasBackupTestArg()) {
        return RunBackupTest(ParseOpenArg(), ParseSettingsDirArg());
    }

    ::InitCommonControls();
    // 187 号对话框含 RichEdit20A 控件，必须先让 riched20 注册窗口类
    if (!::LoadLibraryW(L"riched20.dll")) {
        Log(L"[app] riched20.dll 加载失败，富文本控件不可用");
    }
    if (!mainWindow_.Create(hInstance)) {
        ::MessageBoxW(nullptr, L"创建主窗口失败。", L"SCMD Plus 3.0", MB_ICONERROR);
        return 1;
    }
    Log(L"[app] started");

    // 地形集目录：--ts 优先；否则用构建时复制到 exe 旁的 ts，再回退工程根目录的 ts
    std::wstring tsDir = ParseTsArg();
    if (tsDir.empty()) {
        wchar_t exePath[MAX_PATH] = L"";
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = std::wcsrchr(exePath, L'\\');
        if (slash) *slash = L'\0';
        const std::wstring exeDir(exePath);
        tsDir = exeDir + L"\\ts";
        if (::GetFileAttributesW(tsDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            // exe 在 bin\<Platform>\<Config> 下，工程根目录再往上 3 层
            const std::wstring rootTs = exeDir + L"\\..\\..\\..\\ts";
            wchar_t full[MAX_PATH] = L"";
            if (::GetFullPathNameW(rootTs.c_str(), MAX_PATH, full, nullptr) > 0 &&
                ::GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
                tsDir = full;
            }
        }
    }
    mainWindow_.SetTilesetDir(tsDir);

    // 单位放置参数回归（无头）
    if (const std::wstring unitOut = ParseUnitParamTestArg(); !unitOut.empty()) {
        const int rc = RunUnitParamTest(mainWindow_, unitOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 框选/剪贴板回归（无头）
    if (const std::wstring clipOut = ParseClipTestArg(); !clipOut.empty()) {
        const int rc = RunClipTest(mainWindow_, clipOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 多格图案刷回归（无头）
    if (const std::wstring brushOut = ParseBrushPatternTestArg(); !brushOut.empty()) {
        const int rc = RunBrushPatternTest(mainWindow_, brushOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 菜单/加速键导入自检（无头）
    {
        int argc = 0;
        bool want = false;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (std::wcsncmp(argv[i], L"--menu-test", 11) == 0) want = true;
            }
            ::LocalFree(argv);
        }
        if (want) {
            const int rc = RunMenuTest(mainWindow_.hwnd());
            ::DestroyWindow(mainWindow_.hwnd());
            return rc;
        }
    }

    // 编辑层/方格/显示选项命令回归（无头）
    {
        int argc = 0;
        bool want = false;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (std::wcsncmp(argv[i], L"--menu-cmd-test", 15) == 0) want = true;
            }
            ::LocalFree(argv);
        }
        if (want) {
            const std::wstring mapPath = ParseOpenArg();
            if (!mapPath.empty()) mainWindow_.OpenMap(mapPath);
            const int rc = RunMenuCommandTest(mainWindow_.hwnd(), mainWindow_);
            ::DestroyWindow(mainWindow_.hwnd());
            return rc;
        }
    }

    // 启动视图状态恢复回归
    if (HasSwitchArg(L"--view-test")) {
        const int rc = RunViewTest(mainWindow_.hwnd(), mainWindow_);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 崖壁自动铺设回归（无头）
    if (HasSwitchArg(L"--edge-test")) {
        const int rc = RunEdgeTest(tsDir);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 段重建回归（无头）
    {
        int argc = 0;
        bool want = false;
        LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i) {
                if (std::wcsncmp(argv[i], L"--section-test", 14) == 0) want = true;
            }
            ::LocalFree(argv);
        }
        if (want) {
            const std::wstring mapPath = ParseOpenArg();
            if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
            const int rc = RunSectionTest(mapPath);
            ::DestroyWindow(mainWindow_.hwnd());
            return rc;
        }
    }

    // 玩家/势力模型回归（无头）
    if (const std::wstring playerOut = ParsePlayerTestArg(); !playerOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunPlayerTest(mapPath, playerOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 升级/科技限制回归（无头）
    if (const std::wstring restrictionOut = ParseRestrictionTestArg(); !restrictionOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunRestrictionTest(mapPath, restrictionOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // Location 171 → MRGN 写回回归（无头）
    if (const std::wstring locationOut = ParseLocationTestArg(); !locationOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunLocationTest(mapPath, locationOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 装饰物 / 战争迷雾回归（无头）
    if (const std::wstring decorOut = ParseDecorTestArg(); !decorOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunDecorTest(mapPath, decorOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // SP 单位（THG2）回归（无头）
    if (const std::wstring spriteOut = ParseSpriteTestArg(); !spriteOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunSpriteTest(mapPath, spriteOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 小雷达 / 迷雾开关回归（无头）
    if (const std::wstring radarOut = ParseRadarTestArg(); !radarOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunRadarTest(mapPath, radarOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 声音 / 开关名 / 任务简报回归（无头）
    if (const std::wstring textOut = ParseScenarioTextTestArg(); !textOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunScenarioTextTest(mapPath, textOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 地图片级工具回归（无头）
    if (const std::wstring toolsOut = ParseMapToolsTestArg(); !toolsOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunMapToolsTest(mapPath, toolsOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 8 套地形集逐套渲染回归（无头）
    if (const std::wstring tsRenderOut = ParseTilesetRenderTestArg(); !tsRenderOut.empty()) {
        const int rc = RunTilesetRenderTest(tsDir, tsRenderOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 自定义 MPQ 文件保留回归（无头）
    if (const std::wstring extraOut = ParseExtraTestArg(); !extraOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunExtraFileTest(mapPath, extraOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 触发语义回归（无头）
    if (const std::wstring trigOut = ParseTrigTestArg(); !trigOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunTrigTest(mapPath, trigOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 撤销/重做回归（无头）
    if (!ParseUndoTestArg().empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"no-map\n"); return 2; }
        const int rc = RunUndoTest(mapPath);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 编辑回归（无头）
    if (const std::wstring editOut = ParseEditTestArg(); !editOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"open=FAIL no-map\n"); return 2; }
        const int rc = RunEditTest(mapPath, editOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 地形刷调色板回归（无头）
    if (HasBrushTestArg()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) {
            WriteDiag(L"no-map\n");
            ::DestroyWindow(mainWindow_.hwnd());
            return 2;
        }
        const int rc = RunBrushTest(mapPath, ParseTsArg());
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 重设地图大小回归（无头）
    if (const std::wstring resizeOut = ParseResizeTestArg(); !resizeOut.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty()) { WriteDiag(L"open=FAIL no-map\n"); return 2; }
        const int rc = RunResizeTest(mapPath, resizeOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 文件菜单闭环回归（无头）
    if (const std::wstring fileOut = ParseFileTestArg(); !fileOut.empty()) {
        const int rc = RunFileTest(fileOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // MPQ 压缩写出回归（无头）
    if (const std::wstring compressOut = ParseMpqCompressTestArg(); !compressOut.empty()) {
        const int rc = RunMpqCompressTest(compressOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 另存回归：打开 -> 保存 -> 退出
    if (const std::wstring saveTo = ParseSaveToArg(); !saveTo.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty() || !mainWindow_.OpenMap(mapPath)) {
            ::DestroyWindow(mainWindow_.hwnd());
            return 2;
        }
        const MapDocument& doc = mainWindow_.map();
        MapDocument* writable = const_cast<MapDocument*>(&doc);
        const bool ok = writable->Save(saveTo);
        wchar_t msg[256];
        ::wsprintfW(msg, L"save=%s\nmap=%dx%d\nunits=%u\n", ok ? L"OK" : L"FAIL",
                    doc.width(), doc.height(), static_cast<unsigned>(doc.unitCount()));
        WriteDiag(msg);
        ::DestroyWindow(mainWindow_.hwnd());
        return ok ? 0 : 2;
    }

    // 渲染导出（自动化比对）
    if (const std::wstring dumpPath = ParseDumpArg(); !dumpPath.empty()) {
        const std::wstring mapPath = ParseOpenArg();
        if (mapPath.empty() || !mainWindow_.OpenMap(mapPath)) {
            ::DestroyWindow(mainWindow_.hwnd());
            return 2;
        }
        const MapDocument& doc = mainWindow_.map();
        TerrainRenderer renderer;
        renderer.SetTileset(&doc.tileset());
        TerrainImage image;
        bool ok = renderer.Render(doc.tiles(), doc.width(), doc.height(), 8, image);
        if (ok) ok = TerrainRenderer::WriteBmp24(dumpPath, image);
        wchar_t msg[256];
        ::wsprintfW(msg, L"render=%s\nw=%d h=%d\nmap=%dx%d\n",
                    ok ? L"OK" : L"FAIL", image.width, image.height, doc.width(), doc.height());
        WriteDiag(msg);
        ::DestroyWindow(mainWindow_.hwnd());
        return ok ? 0 : 2;
    }

    // 启动即载入地图（用于自动化验证与日常调试）
    if (const std::wstring openPath = ParseOpenArg(); !openPath.empty()) {
        if (!mainWindow_.OpenMap(openPath)) {
            ::DestroyWindow(mainWindow_.hwnd());
            return 2;
        }
    }

    // 单条目提取（逆向取数）
    if (const std::wstring entry = ParseMpqExtractArg(); !entry.empty()) {
        const int rc = RunMpqExtract(ParseMpqArg(), entry, ParseExtractToArg());
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 容器读取自检
    if (const std::wstring mpqPath = ParseMpqArg(); !mpqPath.empty()) {
        const int rc = RunMpqSelfTest(mpqPath);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 自动化验证入口：打开指定对话框后立即退出
    if (const int dialogId = ParseDialogArg(); dialogId > 0) {
        const bool ok = ShowRecoveredDialog(dialogId, mainWindow_.hwnd());
        if (!ok) {
            wchar_t msg[160];
            ::wsprintfW(msg, L"dialog=%d err=%d", dialogId, LastDialogError());
            wchar_t errPath[MAX_PATH] = L"";
            ::GetTempPathW(MAX_PATH, errPath);
            ::lstrcatW(errPath, L"scmdplus_dlg_err.txt");
            const HANDLE f = ::CreateFileW(errPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                ::WriteFile(f, msg, static_cast<DWORD>(wcslen(msg) * sizeof(wchar_t)), &written, nullptr);
                ::CloseHandle(f);
            }
        }
        ::DestroyWindow(mainWindow_.hwnd());
        return ok ? 0 : 2;
    }

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (mainWindow_.TranslateAccel(msg)) continue;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    Log(L"[app] exit");
    return static_cast<int>(msg.wParam);
}

}  // namespace scmd
