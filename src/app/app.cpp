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
#include "res/control_ids.h"

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include "app/app_settings.h"
#include "app/backups.h"
#include "ui/brush_palette.h"
#include "ui/dialog_registry.h"
#include "ui/dialogs.h"
#include "ui/dlg_common.h"
#include "ui/dlg_procs.h"
#include "common/string_util.h"

#include "archive/mpq_archive.h"
#include "archive/mpq_codec.h"
#include "map/edit_history.h"
#include "map/map_document.h"
#include "map/map_switches.h"
#include "units/iscript.h"
#include "units/stat_txt.h"
#include "units/unit_data.h"
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

// 通用参数取值：--key=value
std::wstring ParseValueArg(const wchar_t* prefix) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring value;
    const std::size_t len = std::wcslen(prefix);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], prefix, len) == 0) value = argv[i] + len;
        }
        ::LocalFree(argv);
    }
    return value;
}

// 调试开关：SCMDPlus.exe --mpq=<地图路径> 只做容器读取自检
std::wstring ParseMpqArg() {    int argc = 0;
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
bool ItemEnabled(HWND hwnd, int id);
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

// --loc-draw-test=<输出scx>：地点描绘模式与 SP 对齐方格回归
std::wstring ParseLocationDrawTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--loc-draw-test=", 16) == 0) path = argv[i] + 16;
        }
        ::LocalFree(argv);
    }
    return path;
}

// --doodad-test=<输出scx>：装饰物窗口放置/删除/任意放置回归
std::wstring ParseDoodadTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--doodad-test=", 14) == 0) path = argv[i] + 14;
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

// --doodad-test=<输出scx>：装饰物窗口放置/删除/任意放置回归
int RunDoodadTest(MainWindow& win, const std::wstring& outPath) {
    MapDocument& doc = win.mutableMap();
    if (!doc.CreateNew(32, 32, 0, win.tilesetDir())) {
        WriteDiag(L"create=FAIL\n");
        return 2;
    }

    int wx = 2, wy = 2;
    if (doc.hasTileset()) {
        // 新建地图默认瓦片组不保证可行走：显式铺两格可行走地形
        for (int g = 0; g < doc.tileset().GroupCount(); g++) {
            if (doc.tileset().IsWalkable(static_cast<u16>(g))) {
                const u16 value = static_cast<u16>(g << 4);
                doc.PaintTile(2, 2, value);
                doc.PaintTile(4, 4, value);
                break;
            }
        }
    }
    if (!FindWalkable(doc, 2, 2, wx, wy)) {
        WriteDiag(L"no-walkable\n");
        return 2;
    }

    // 默认放置：类型/玩家/启用标志与像素坐标
    win.SetDoodadPlacement(5, 3, false);
    win.SetDoodadAnywhere(false);
    const bool placed = win.PlaceDoodadAtTile(wx, wy);
    bool recordOk = false;
    for (const Doodad& d : doc.doodads().Items()) {
        if (d.type == 5 && d.owner == 3 && d.enabled == 0 &&
            d.x == wx * kTilePixels + kTilePixels / 2 &&
            d.y == wy * kTilePixels + kTilePixels / 2) {
            recordOk = true;
        }
    }

    // 禁用标志写入 enabled = 1
    win.SetDoodadPlacement(7, 1, true);
    const bool placedDisabled = win.PlaceDoodadAtTile(4, 4);
    bool disabledOk = false;
    for (const Doodad& d : doc.doodads().Items()) {
        if (d.type == 7 && d.owner == 1 && d.enabled == 1) disabledOk = true;
    }

    // 默认拒绝不可通行地形；0x8010 允许任意放置后接受
    win.SetDoodadPlacement(9, 0, false);
    bool blockedRejected = true;
    bool anywhereAllowed = true;
    if (doc.hasTileset()) {
        u16 blockedValue = 0;
        bool haveBlock = false;
        for (int g = 0; g < doc.tileset().GroupCount(); g++) {
            if (!doc.tileset().IsWalkable(static_cast<u16>(g))) {
                blockedValue = static_cast<u16>(g << 4);
                haveBlock = true;
                break;
            }
        }
        if (haveBlock) {
            doc.PaintTile(8, 8, blockedValue);
            blockedRejected = !win.PlaceDoodadAtTile(8, 8);
            win.SetDoodadAnywhere(true);
            anywhereAllowed = win.PlaceDoodadAtTile(8, 8);
            win.SetDoodadAnywhere(false);
        }
    }

    // 删除 + 撤销/重做
    const int removed = win.RemoveDoodadsAtTile(wx, wy);
    const bool removeOk = removed == 1;
    const std::size_t afterRemove = doc.doodads().Count();
    const bool undone = win.Undo() && doc.doodads().Count() == afterRemove + 1;
    const bool redone = win.Redo() && doc.doodads().Count() == afterRemove;

    const bool saved = doc.Save(outPath);

    // 装饰物窗口能在真实主窗口上创建
    win.OnCommand(0x8004);
    HWND palette = ::FindWindowW(L"SCMDPlusDoodadPalette", nullptr);
    const bool paletteOpen = palette != nullptr && win.doodadPaletteOpen();
    if (palette) ::DestroyWindow(palette);

    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    bool reopenOk = reopened && check.doodads().Count() == doc.doodads().Count();
    if (reopenOk) {
        bool found = false;
        for (const Doodad& d : check.doodads().Items()) {
            if (d.type == 7 && d.owner == 1 && d.enabled == 1) found = true;
        }
        reopenOk = found;
    }

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"placed=%d\nrecord=%d\ndisabled=%d/%d\nblockedRejected=%d\nanywhere=%d\n"
        L"removed=%d\nundo=%d\nredo=%d\nsaved=%d\npalette=%d\nreopened=%d\nreopenOk=%d\n",
        placed ? 1 : 0, recordOk ? 1 : 0, placedDisabled ? 1 : 0, disabledOk ? 1 : 0,
        blockedRejected ? 1 : 0, anywhereAllowed ? 1 : 0, removed, undone ? 1 : 0,
        redone ? 1 : 0, saved ? 1 : 0, paletteOpen ? 1 : 0, reopened ? 1 : 0,
        reopenOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = placed && recordOk && placedDisabled && disabledOk && blockedRejected &&
                    anywhereAllowed && removeOk && undone && redone && saved && paletteOpen &&
                    reopened && reopenOk;
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

    // 默认段：VCOD 1040 B；TRIG 预设 1/2 分别为 3/4 条，0 为不写
    auto sectionSize = [](const MapDocument& d, const char* name) -> int {
        const Buffer* b = d.chk().Find(name);
        return b ? static_cast<int>(b->size()) : -1;
    };
    const int vcodSize = sectionSize(fresh, "VCOD");
    const int trig3Size = sectionSize(fresh, "TRIG");
    const int trig3Blocks = static_cast<int>(fresh.triggers().BlockCount());

    MapDocument noTrig;
    const bool preset0Created = noTrig.CreateNew(32, 32, 0, std::wstring(), 0);
    const bool preset0Ok = preset0Created && sectionSize(noTrig, "VCOD") == 1040 &&
                           sectionSize(noTrig, "TRIG") == -1;

    MapDocument fourTrig;
    const bool preset2Created = fourTrig.CreateNew(32, 32, 0, std::wstring(), 2);
    const int trig4Size = sectionSize(fourTrig, "TRIG");
    const int trig4Blocks = static_cast<int>(fourTrig.triggers().BlockCount());

    wchar_t msg[640];
    ::wsprintfW(msg,
        L"created=%d\ndirtyNew=%d\nunnamed=%d\nsaved=%d\ncleanAfterSave=%d\n"
        L"pathSaved=%d\nreopened=%d\nsizeOk=%d\npainted=%d/16\nclosed=%d\nfreshOk=%d\n"
        L"vcod=%d\ntrig3=%d/%d\npreset0=%d\ntrig4=%d/%d\n",
        created ? 1 : 0, dirtyAfterNew ? 1 : 0, unnamed ? 1 : 0, saved ? 1 : 0,
        cleanAfterSave ? 1 : 0, pathAfterSave ? 1 : 0, reopened ? 1 : 0, sizeOk ? 1 : 0,
        painted, closed ? 1 : 0, freshOk ? 1 : 0,
        vcodSize, trig3Size, trig3Blocks, preset0Ok ? 1 : 0, trig4Size, trig4Blocks);
    WriteDiag(msg);

    const bool ok = created && dirtyAfterNew && unnamed && saved && cleanAfterSave &&
                    pathAfterSave && reopened && sizeOk && painted == 16 && closed && freshOk &&
                    vcodSize == 1040 && trig3Size == 7200 && trig3Blocks == 3 && preset0Ok &&
                    trig4Size == 9600 && trig4Blocks == 4;
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

int RunExtraFileTest(const std::wstring& mapPath, const std::wstring& outPath) {    MpqArchive src;
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

// --iscript-test=<iscript.bin>：解析头表/动画/操作码；可选 --grp=<grp> 校验 GRP 头
std::wstring ParseIscriptTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--iscript-test=", 15) == 0) path = argv[i] + 15;
        }
        ::LocalFree(argv);
    }
    return path;
}

std::wstring ParseGrpArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--grp=", 6) == 0) path = argv[i] + 6;
        }
        ::LocalFree(argv);
    }
    return path;
}

std::wstring ParseGrpTestArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--grp-test=", 11) == 0) path = argv[i] + 11;
        }
        ::LocalFree(argv);
    }
    return path;
}

std::wstring ParsePalArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--pal=", 6) == 0) path = argv[i] + 6;
        }
        ::LocalFree(argv);
    }
    return path;
}

std::wstring ParseGrpBmpArg() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::wstring path;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcsncmp(argv[i], L"--grp-bmp=", 10) == 0) path = argv[i] + 10;
        }
        ::LocalFree(argv);
    }
    return path;
}

int RunIscriptTest(const std::wstring& iscriptPath, const std::wstring& grpPath) {
    IscriptFile file;
    if (!file.Load(iscriptPath)) {
        WriteDiag(L"load=FAIL\n");
        return 2;
    }

    int anims = 0;
    int instructions = 0;
    int emptyAnims = 0;
    int unknownOps = 0;
    int maxInstructions = 0;
    std::vector<std::wstring> unknownList;
    for (const IscriptHeader& header : file.entries()) {
        for (int i = 0; i < static_cast<int>(header.animOffsets.size()); i++) {
            if (header.animOffsets[static_cast<std::size_t>(i)] == 0) continue;
            anims++;
            std::vector<IscriptInstruction> list;
            if (!file.Disassemble(header, i, list)) {
                emptyAnims++;
                continue;
            }
            instructions += static_cast<int>(list.size());
            maxInstructions = std::max(maxInstructions, static_cast<int>(list.size()));
            for (const IscriptInstruction& ins : list) {
                if (IscriptFile::OpcodeName(ins.opcode)[0] == '?') {
                    unknownOps++;
                    if (unknownList.size() < 8) {
                        wchar_t one[64];
                        ::wsprintfW(one, L"%u/%d/%02X", header.id, i, ins.opcode);
                        unknownList.push_back(one);
                    }
                }
            }
        }
    }

    const IscriptHeader* sample = file.Find(198);
    const bool sampleOk = sample && sample->type == 12 && sample->animOffsets.size() == 14;

    // 模拟示例：优先 Death 动画（一般会 end），否则第一个可用动画
    IscriptSimResult sim;
    bool simOk = false;
    if (sample) {
        int simAnim = -1;
        if (sample->animOffsets.size() > 1 && sample->animOffsets[1] != 0) {
            simAnim = 1;
        } else {
            for (int i = 0; i < static_cast<int>(sample->animOffsets.size()); i++) {
                if (sample->animOffsets[static_cast<std::size_t>(i)] != 0) {
                    simAnim = i;
                    break;
                }
            }
        }
        simOk = simAnim >= 0 && file.Simulate(*sample, simAnim, 1000, sim) && sim.ops > 0;
    }

    bool grpOk = true;
    GrpFile grp;
    if (!grpPath.empty()) {
        grpOk = grp.Load(grpPath) && grp.frameCount() > 0 && grp.width() > 0 &&
                grp.height() > 0;
    }

    // 合成 iscript：trgtrangecondjmp 条件跳转（192 条件开关回归）
    bool condSkip = false;
    bool condTake = false;
    {
        Buffer data(29, 0);
        auto put16 = [&data](std::size_t at, u16 v) {
            data[at] = static_cast<u8>(v & 0xFF);
            data[at + 1] = static_cast<u8>(v >> 8);
        };
        auto put32 = [&data](std::size_t at, u32 v) {
            data[at] = static_cast<u8>(v & 0xFF);
            data[at + 1] = static_cast<u8>((v >> 8) & 0xFF);
            data[at + 2] = static_cast<u8>((v >> 16) & 0xFF);
            data[at + 3] = static_cast<u8>((v >> 24) & 0xFF);
        };
        put16(0, 1000);   // id
        put16(2, 8);      // header offset
        put16(4, 0);      // 索引结束
        put16(6, 0);
        std::memcpy(&data[8], "SCPE", 4);
        put32(12, 0);     // type 0 → 2 个动画槽
        put16(16, 20);    // anim0
        put16(18, 0);     // anim1
        data[20] = 0x3A;  // trgtrangecondjmp +8
        put16(21, 8);
        put16(23, 100);   // 射程参数（忽略）
        data[25] = 0x00;  // playfram 5
        put16(26, 5);
        data[28] = 0x16;  // end

        IscriptFile synth;
        const bool parsed = synth.Parse(data);
        const IscriptHeader* header = parsed ? synth.Find(1000) : nullptr;
        IscriptSimContext skipCtx;
        IscriptSimResult skip;
        const bool skipOk = header && synth.Simulate(*header, 0, 100, skip, &skipCtx);
        condSkip = skipOk && skip.frames == 1 && skip.condJumps == 0 && skip.lastFrame == 5;

        IscriptSimContext takeCtx;
        takeCtx.enemyInRange = true;
        IscriptSimResult take;
        const bool takeOk = header && synth.Simulate(*header, 0, 100, take, &takeCtx);
        condTake = takeOk && take.frames == 0 && take.condJumps == 1 && take.ended;
    }

    wchar_t msg[512];
    ::wsprintfW(msg,
                L"load=1\nentries=%u\nsize=%u\nanims=%d\ninstructions=%d\n"
                L"empty=%d\nunknown=%d\nmax=%d\nsample=%d\n"
                L"sim=%d\nsimOps=%d\nsimFrames=%d\nsimWaits=%d\nsimEnded=%d\n"
                L"condSkip=%d\ncondTake=%d\n",
                static_cast<unsigned>(file.entries().size()),
                static_cast<unsigned>(file.size()), anims, instructions, emptyAnims,
                unknownOps, maxInstructions, sampleOk ? 1 : 0,
                simOk ? 1 : 0, sim.ops, sim.frames, sim.waits, sim.ended ? 1 : 0,
                condSkip ? 1 : 0, condTake ? 1 : 0);
    std::wstring text = msg;
    if (!unknownList.empty()) {
        text += L"unknownOps=";
        for (std::size_t i = 0; i < unknownList.size(); i++) {
            if (i) text += L",";
            text += unknownList[i];
        }
        text += L"\n";
    }
    if (!grpPath.empty()) {
        wchar_t grpMsg[192];
        ::wsprintfW(grpMsg, L"grp=%d\nframes=%d\ndims=%dx%d\n", grpOk ? 1 : 0,
                    grp.frameCount(), grp.width(), grp.height());
        text += grpMsg;
    }
    WriteDiag(text);

    const bool ok = file.entries().size() >= 300 && emptyAnims == 0 && unknownOps == 0 &&
                    sampleOk && simOk && grpOk && condSkip && condTake;
    return ok ? 0 : 2;
}

// --grp-test=<grp> [--pal=<pal>] [--grp-bmp=<bmp>]：GRP 帧解码回归
int RunGrpTest(const std::wstring& grpPath, const std::wstring& palPath,
               const std::wstring& bmpPath) {
    GrpFile grp;
    if (!grp.Load(grpPath)) {
        WriteDiag(L"load=FAIL\n");
        return 2;
    }
    Buffer palette;
    bool palOk = true;
    if (!palPath.empty()) palOk = GrpFile::LoadPalette(palPath, palette);
    if (palette.empty()) GrpFile::GrayPalette(palette);

    int decoded = 0;
    long long opaque = 0;
    int maxOpaque = 0;
    int firstW = 0;
    int firstH = 0;
    int firstOpaque = 0;
    int firstFail = -1;
    Buffer firstFrame;
    for (int i = 0; i < grp.frameCount(); i++) {
        Buffer bgra;
        int w = 0;
        int h = 0;
        if (!grp.DecodeFrame(i, palette, bgra, w, h)) {
            if (firstFail < 0) firstFail = i;
            continue;
        }
        decoded++;
        int count = 0;
        for (std::size_t p = 3; p < bgra.size(); p += 4) {
            if (bgra[p]) count++;
        }
        opaque += count;
        maxOpaque = std::max(maxOpaque, count);
        if (i == 0) {
            firstFrame = std::move(bgra);
            firstW = w;
            firstH = h;
            firstOpaque = count;
        }
    }

    bool bmpOk = true;
    if (!bmpPath.empty()) {
        TerrainImage image;
        image.width = firstW;
        image.height = firstH;
        image.pixels = firstFrame;
        bmpOk = !firstFrame.empty() && TerrainRenderer::WriteBmp24(bmpPath, image);
    }

    wchar_t msg[640];
    ::wsprintfW(msg,
                L"grp=1\nframes=%d\ndims=%dx%d\ndecoded=%d/%d\nopaque=%I64d\nmax=%d\n"
                L"firstOpaque=%d\npal=%d\nbmp=%d\n",
                grp.frameCount(), grp.width(), grp.height(), decoded, grp.frameCount(),
                opaque, maxOpaque, firstOpaque, palOk ? 1 : 0, bmpOk ? 1 : 0);
    std::wstring text = msg;
    if (firstFail >= 0 && firstFail < grp.frameCount()) {
        const GrpFrameMeta& f = grp.frames()[static_cast<std::size_t>(firstFail)];
        wchar_t extra[192];
        ::wsprintfW(extra, L"firstFail=%d off=%u rect=%u,%u %ux%u\n", firstFail,
                    static_cast<unsigned>(f.offset), f.x, f.y, f.width, f.height);
        text += extra;
    }
    WriteDiag(text);

    const bool ok = decoded == grp.frameCount() && grp.frameCount() > 0 && maxOpaque > 0 &&
                    palOk && bmpOk;
    return ok ? 0 : 2;
}

// --unit-data-test：units.dat 列式属性表回归
int RunUnitDataTest() {
    UnitDataTable table;
    std::wstring source;
    if (!UnitDataTable::LoadAuto(table, source)) {
        WriteDiag(L"load=FAIL\n");
        return 2;
    }
    const UnitData* marine = table.Get(0);
    const UnitData* goliath = table.Get(3);
    const UnitData* probe = table.Get(64);
    const UnitData* reaver = table.Get(83);
    const UnitData* archon = table.Get(68);
    const bool samples = marine && goliath && probe && reaver && archon;
    const bool ok = samples && table.count() == 228 && marine->hp == 40 &&
                    marine->shields == 0 && goliath->hp == 125 && goliath->armor == 1 &&
                    probe->hp == 20 && probe->shields == 20 && reaver->hp == 100 &&
                    reaver->shields == 80 && archon->hp == 10 && archon->shields == 350 &&
                    marine->width > 0 && marine->height > 0;

    wchar_t msg[640];
    ::wsprintfW(msg,
                L"units=%d\nsource=%ls\n"
                L"marine=%d/%d/%d %dx%d\n"
                L"goliath=%d/%d/%d\n"
                L"probe=%d/%d\n"
                L"reaver=%d/%d\n"
                L"archon=%d/%d\n",
                table.count(), source.c_str(), marine ? marine->hp : -1,
                marine ? marine->shields : -1, marine ? marine->armor : -1,
                marine ? marine->width : 0, marine ? marine->height : 0,
                goliath ? goliath->hp : -1, goliath ? goliath->shields : -1,
                goliath ? goliath->armor : -1, probe ? probe->hp : -1,
                probe ? probe->shields : -1, reaver ? reaver->hp : -1,
                reaver ? reaver->shields : -1, archon ? archon->hp : -1,
                archon ? archon->shields : -1);
    WriteDiag(msg);

    // stat_txt.tbl 抽样：验证 172 单位名字能解析出真实文本
    StatTextTable names;
    std::wstring nameSource;
    const bool namesLoaded = StatTextTable::LoadAuto(names, nameSource);
    const std::wstring& n0 = names.Name(0);
    const std::wstring& n64 = names.Name(64);
    const bool namesOk = namesLoaded && !n0.empty() && !n64.empty();
    wchar_t nameMsg[768];
    ::wsprintfW(nameMsg, L"names=%d\nnameCount=%d\nnameSource=%ls\nname0=%ls\nname64=%ls\n",
                namesOk ? 1 : 0, static_cast<int>(names.Count()),
                namesLoaded ? nameSource.c_str() : L"(none)", n0.c_str(), n64.c_str());
    WriteDiag(nameMsg);
    return (ok && namesOk) ? 0 : 2;
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

bool ItemEnabled(HWND hwnd, int id) {
    HMENU menu = FindMenuWithItem(::GetMenu(hwnd), id);
    if (!menu) return false;
    const UINT state = ::GetMenuState(menu, static_cast<UINT>(id), MF_BYCOMMAND);
    return state != 0xFFFFFFFFu && (state & (MF_GRAYED | MF_DISABLED)) == 0;
}

// 关闭当前进程内除 keep 之外的 SCMDPlus 主窗口（多窗口回归用）
struct CloseOthersCtx {
    HWND keep = nullptr;
    int closed = 0;
};

BOOL CALLBACK CloseOtherMainWindow(HWND w, LPARAM lp) {
    auto* ctx = reinterpret_cast<CloseOthersCtx*>(lp);
    wchar_t cls[64] = L"";
    ::GetClassNameW(w, cls, 64);
    if (std::wcscmp(cls, L"SCMDPlusMainWindow") == 0 && w != ctx->keep) {
        ::SendMessageW(w, WM_CLOSE, 0, 0);
        ctx->closed++;
    }
    return TRUE;
}

int RunMenuCommandTest(HWND hwnd, MainWindow& win) {
    auto cmd = [hwnd](int id) {
        ::SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
    };

    const bool layerInit = ItemChecked(hwnd, 0x803A);

    // 命令状态：有地图时保存/关闭可用；历史为空时撤销/恢复灰显
    const bool enabledInit = ItemEnabled(hwnd, 0x8087) && ItemEnabled(hwnd, 0x8007) &&
                             !ItemEnabled(hwnd, 0x807F) && !ItemEnabled(hwnd, 0x8080);

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
    cmd(0x80F4);   // 战争迷雾 → 显示战争迷雾在探索地区（同义开关）
    const bool fogExploredOn = ItemChecked(hwnd, 0x80F4) && ItemChecked(hwnd, 0x8113) &&
                               ItemChecked(hwnd, 0x80CE);
    cmd(0x80F4);
    const bool fogExplored = fogExploredOn && !ItemChecked(hwnd, 0x80F4) &&
                             !ItemChecked(hwnd, 0x8113) && !ItemChecked(hwnd, 0x80CE);

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
    // Zerg地皮（0x8034）默认勾选：关→开
    cmd(0x8034);
    const bool zergGroundOff = !ItemChecked(hwnd, 0x8034);
    cmd(0x8034);
    const bool zergGroundOn = ItemChecked(hwnd, 0x8034);
    const bool effectsOk = colorCycleOn && colorCycleOff && deathFxOn && specialOff && specialOn &&
                           zergGroundOff && zergGroundOn;

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
    cmd(0x8010);   // 选项 → 装饰物 → 允许任意放置
    const bool doodadAnywhereOn = ItemChecked(hwnd, 0x8010);
    cmd(0x8010);
    const bool doodadAnywhereOff = !ItemChecked(hwnd, 0x8010);
    cmd(0x80EB);   // Locations → 按照方格描绘
    const bool locGrid = ItemChecked(hwnd, 0x80EB) && !ItemChecked(hwnd, 0x8013);
    cmd(0x80EC);   // Locations → 任意描绘
    const bool locAny = ItemChecked(hwnd, 0x80EC) && !ItemChecked(hwnd, 0x80EB);
    cmd(0x8013);   // 还原整齐的描绘
    const bool locNeat = ItemChecked(hwnd, 0x8013) && !ItemChecked(hwnd, 0x80EC);
    cmd(0x80B7);   // SP单位 → 对齐方格
    const bool spAlignOn = ItemChecked(hwnd, 0x80B7);
    cmd(0x80B7);
    const bool spAlignOff = !ItemChecked(hwnd, 0x80B7);
    cmd(0x80A8);   // 自动检查更新
    const bool autoUpdateOn = ItemChecked(hwnd, 0x80A8);
    cmd(0x80A8);
    const bool autoUpdateOff = !ItemChecked(hwnd, 0x80A8);

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

        // 小地图点击定位：点击小地图中心应滚动到地图中部
        const RECT mm = win.minimapRect();
        minimapNav = mm.right > mm.left && mm.bottom > mm.top &&
                     win.ClickMinimapAt((mm.left + mm.right) / 2, (mm.bottom + mm.top) / 2) &&
                     win.scrollY() > 0;
    }

    // 窗口排列（单窗口也应安全）
    cmd(0x80EF);   // 水平排列
    cmd(0x80F0);   // 垂直排列
    // 同进程多窗口：新建后计数 +1，关闭新窗口后恢复
    const int windowsBefore = MainWindow::LiveWindowCount();
    cmd(0x80ED);   // 新建窗口
    const bool windowCreated = MainWindow::LiveWindowCount() == windowsBefore + 1;
    CloseOthersCtx closeCtx;
    closeCtx.keep = hwnd;
    ::EnumWindows(&CloseOtherMainWindow, reinterpret_cast<LPARAM>(&closeCtx));
    const bool windowOk = ::IsWindow(hwnd) != 0 && windowCreated && closeCtx.closed == 1 &&
                          MainWindow::LiveWindowCount() == windowsBefore;

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"layerInit=%d\nenabledInit=%d\nlayerUnits=%d\nlayerLoc=%d\nlayerBack=%d\n"
        L"gridLarge=%d\ngridOff=%d\ngridBack=%d\ngridRed=%d\n"
        L"unitsOff=%d\nunitsOn=%d\nfogOn=%d\nzoom=%d\nminimapOff=%d\nminimapOn=%d\n"
        L"fogAlias=%d\nfogExplored=%d\nallColors=%d\neffects=%d\ndebug=%d\ndebugData=%d\noverlap=%d\nimageSize=%d\n"
        L"compare=%d\nconflict=%d\nminiNav=%d\nsound=%d\ndoodadAny=%d\nlocMode=%d\n"
        L"spAlign=%d\nautoUpdate=%d\noutput=%d\ncache=%d\nwindow=%d\n",
        layerInit ? 1 : 0, enabledInit ? 1 : 0, layerUnits ? 1 : 0, layerLoc ? 1 : 0,
        layerBack ? 1 : 0,
        gridLarge ? 1 : 0, gridOff ? 1 : 0, gridBack ? 1 : 0, gridRed ? 1 : 0,
        unitsOff ? 1 : 0, unitsOn ? 1 : 0, fogOn ? 1 : 0, zoomOk ? 1 : 0,
        minimapOff ? 1 : 0, minimapOn ? 1 : 0, fogAlias ? 1 : 0, fogExplored ? 1 : 0,
        allColorsOn ? 1 : 0,
        effectsOk ? 1 : 0,
        debugOn ? 1 : 0, (debugData1 && debugData2) ? 1 : 0, overlapOn ? 1 : 0,
        imageSizeOn ? 1 : 0, compareOn ? 1 : 0, conflictOn ? 1 : 0, minimapNav ? 1 : 0,
        (soundOff && soundOn) ? 1 : 0, (doodadAnywhereOn && doodadAnywhereOff) ? 1 : 0,
        (locGrid && locAny && locNeat) ? 1 : 0, (spAlignOn && spAlignOff) ? 1 : 0,
        (autoUpdateOn && autoUpdateOff) ? 1 : 0,
        outputOk ? 1 : 0, cacheOk ? 1 : 0, windowOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = layerInit && enabledInit && layerUnits && layerLoc && layerBack && gridLarge && gridOff &&
                    gridBack && gridRed && unitsOff && unitsOn && fogOn && zoomOk &&
                    minimapOff && minimapOn && fogAlias && fogExplored && allColorsOn &&
                    effectsOk && debugOn &&
                    debugData1 && debugData2 && overlapOn && imageSizeOn && compareOn &&
                    conflictOn && minimapNav && soundOff && soundOn && doodadAnywhereOn &&
                    doodadAnywhereOff && locGrid && locAny && locNeat && spAlignOn &&
                    spAlignOff && autoUpdateOn && autoUpdateOff && outputOk && cacheOk &&
                    windowOk;
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

    // 地形集切换：ERA 写入 CHK，地形集按新 ERA 重载
    const u16 targetEra = static_cast<u16>((doc.era() + 1) % 8);
    const bool eraSwitched = doc.SetEra(targetEra) && doc.era() == targetEra;
    const bool eraTileset = ParseTsArg().empty() || doc.hasTileset();

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    const bool reopenDim = reopened && check.width() == newW && check.height() == newH;
    const bool reopenTile = reopened && fx >= 0 && check.TileAt(fx + 1, fy + 1) == expected;
    const bool reopenEra = reopened && check.era() == targetEra;

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
        L"reopenTile=%d\nsmooth=%d\nera=%u->%u\neraSwitched=%d\neraTileset=%d\nreopenEra=%d\n",
        oldW, oldH, newW, newH, resized ? 1 : 0, dimOk ? 1 : 0, tileMoveOk ? 1 : 0,
        borderOk ? 1 : 0, unitsInBounds ? 1 : 0, static_cast<unsigned>(oldUnits),
        static_cast<unsigned>(doc.unitCount()), dirty ? 1 : 0,
        saved ? 1 : 0, reopened ? 1 : 0, reopenDim ? 1 : 0, reopenTile ? 1 : 0,
        smoothOk ? 1 : 0, static_cast<unsigned>(targetEra), static_cast<unsigned>(check.era()),
        eraSwitched ? 1 : 0, eraTileset ? 1 : 0, reopenEra ? 1 : 0);
    WriteDiag(msg);

    const bool ok = resized && dimOk && tileMoveOk && borderOk && unitsInBounds && dirty &&
                    saved && reopened && reopenDim && reopenTile && smoothOk && eraSwitched &&
                    eraTileset && reopenEra;
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
                L"fields=%d\nothers=%d\npreserved=%d\n",
                parsed ? 1 : 0, undoOk ? 1 : 0, redoOk ? 1 : 0, saved ? 1 : 0,
                reopened ? 1 : 0, fieldsOk ? 1 : 0, othersOk ? 1 : 0,
                preserved ? 1 : 0);
    WriteDiag(msg);

    const bool ok = parsed && undoOk && redoOk && saved && reopened && fieldsOk &&
                    othersOk && preserved;
    return ok ? 0 : 2;
}

// --loc-draw-test=<输出scx>：地点描绘模式与 SP 对齐方格回归
int RunLocationDrawTest(MainWindow& win, const std::wstring& outPath) {
    MapDocument& doc = win.mutableMap();
    if (!doc.CreateNew(32, 32, 0, std::wstring())) {
        WriteDiag(L"create=FAIL\n");
        return 2;
    }

    // MRGN 真实布局：u32 像素坐标 + u16 string + u16 flags（20 字节）
    bool formatOk = false;
    {
        Buffer raw(20, 0);
        auto put32 = [&raw](std::size_t at, u32 v) {
            raw[at] = static_cast<u8>(v & 0xFF);
            raw[at + 1] = static_cast<u8>((v >> 8) & 0xFF);
            raw[at + 2] = static_cast<u8>((v >> 16) & 0xFF);
            raw[at + 3] = static_cast<u8>((v >> 24) & 0xFF);
        };
        put32(0, 3744);
        put32(4, 160);
        put32(8, 3872);
        put32(12, 256);
        raw[16] = 9;
        raw[17] = 0;
        raw[18] = 4;
        raw[19] = 0;
        LocationTable table;
        formatOk = table.Parse(raw) && table.Count() == 1 &&
                   table.Items()[0].left == 3744 && table.Items()[0].top == 160 &&
                   table.Items()[0].right == 3872 && table.Items()[0].bottom == 256 &&
                   table.Items()[0].stringId == 9 && table.Items()[0].flags == 4 &&
                   table.Serialize() == raw;
    }

    // 整齐的描绘：对齐 32 px 瓦片 → (0,0)-(128,96)
    win.SetLocationDrawMode(0);
    const int neat = win.AddLocationFromDrag(10, 10, 100, 70);
    bool neatOk = neat == 0 && doc.locations().Count() == 1;
    if (neatOk) {
        const Location& l = doc.locations().Items()[0];
        neatOk = l.left == 0 && l.top == 0 && l.right == 128 && l.bottom == 96;
    }

    // 按照方格描绘：较大方格 64 px → (0,0)-(128,128)
    win.OnCommand(0x80A5);
    win.SetLocationDrawMode(1);
    const int grid = win.AddLocationFromDrag(10, 10, 100, 70);
    bool gridOk = grid == 1;
    if (gridOk) {
        const Location& l = doc.locations().Items()[1];
        gridOk = l.left == 0 && l.top == 0 && l.right == 128 && l.bottom == 128;
    }
    win.OnCommand(0x800D);   // 还原普通方格

    // 任意描绘：像素精度 → (10,10)-(101,71)
    win.SetLocationDrawMode(2);
    const int anyIndex = win.AddLocationFromDrag(10, 10, 100, 70);
    bool anyOk = anyIndex == 2;
    if (anyOk) {
        const Location& l = doc.locations().Items()[2];
        anyOk = l.left == 10 && l.top == 10 && l.right == 101 && l.bottom == 71;
    }
    win.SetLocationDrawMode(0);

    const std::size_t beforeUndo = doc.locations().Count();
    const bool undone = win.Undo() && doc.locations().Count() == beforeUndo - 1;
    const bool redone = win.Redo() && doc.locations().Count() == beforeUndo;

    // SP 单位：自由像素 / 对齐方格（瓦片中心）
    win.SetSpPlacement(150, 2, 0x1000);
    win.SetSpAlignGrid(false);
    const bool spFree = win.PlaceSpriteAtPixels(100, 70);
    win.SetSpAlignGrid(true);
    const bool spAligned = win.PlaceSpriteAtPixels(100, 70);
    bool spFieldsOk = doc.sprites().Count() == 2;
    if (spFieldsOk) {
        const SpriteRecord& a = doc.sprites().Items()[0];
        const SpriteRecord& b = doc.sprites().Items()[1];
        spFieldsOk = a.type == 150 && a.owner == 2 && a.flags == 0x1000 && a.x == 100 &&
                     a.y == 70 && b.x == 112 && b.y == 80;
    }
    const int removed = win.RemoveSpriteAtTile(3, 2);   // (112,80) 所在格
    const bool spRemove = removed == 1 && doc.sprites().Count() == 1;
    const bool spUndo = win.Undo() && doc.sprites().Count() == 2;
    const bool spRedo = win.Redo() && doc.sprites().Count() == 1;

    const bool saved = doc.Save(outPath);
    MapDocument check;
    const bool reopened = check.Open(outPath, std::wstring());
    bool reopenOk = reopened && check.locations().Count() == 3 && check.sprites().Count() == 1;
    if (reopenOk) {
        const Location& l = check.locations().Items()[2];
        reopenOk = l.left == 10 && l.right == 101 && l.bottom == 71 &&
                   check.sprites().Items()[0].type == 150 &&
                   check.sprites().Items()[0].x == 100;
    }

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"format=%d\nneat=%d\ngrid=%d\nany=%d\nundo=%d\nredo=%d\n"
        L"spFree=%d\nspAligned=%d\nspFields=%d\nspRemove=%d\nspUndo=%d\nspRedo=%d\n"
        L"saved=%d\nreopened=%d\nreopenOk=%d\n",
        formatOk ? 1 : 0, neatOk ? 1 : 0, gridOk ? 1 : 0, anyOk ? 1 : 0, undone ? 1 : 0,
        redone ? 1 : 0, spFree ? 1 : 0, spAligned ? 1 : 0, spFieldsOk ? 1 : 0,
        spRemove ? 1 : 0, spUndo ? 1 : 0, spRedo ? 1 : 0, saved ? 1 : 0, reopened ? 1 : 0,
        reopenOk ? 1 : 0);
    WriteDiag(msg);

    const bool ok = formatOk && neatOk && gridOk && anyOk && undone && redone && spFree &&
                    spAligned && spFieldsOk && spRemove && spUndo && spRedo && saved &&
                    reopened && reopenOk;
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
    settings.SetBrushFiles({L"C:\\brushes\\a.scb", L"D:\\brushes\\b.scb"});
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
    settings.SetAutoCheckUpdate(true);
    settings.SetUpdateCdn(L"https://cdn.example.com/scmdplus/");
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
                            settings.brushFiles().size() == 2 &&
                            settings.brushFiles()[0] == L"C:\\brushes\\a.scb" &&
                            !settings.backupEnabled() && settings.backupCount() == 3 &&
                            settings.autoBackupEnabled() &&
                            settings.autoBackupMinutes() == 7 &&
                            settings.view().zoomPx == 16 && !settings.view().gridEnabled &&
                            settings.view().gridCellX == 48 && settings.view().layer == 4 &&
                            !settings.view().showUnits && settings.view().hideMinimap &&
                            settings.view().showFog && !settings.soundEnabled() &&
                            settings.autoCheckUpdate();
    const bool updateCdnOk =
        settings.updateCdn() == L"https://cdn.example.com/scmdplus/";

    // 失效最近项移除：去重后再删除首项
    const bool frontBefore = !settings.recentMaps().empty();
    if (frontBefore) settings.RemoveRecentMap(settings.recentMaps().front());
    const bool removeOk = frontBefore && settings.recentMaps().size() == 7 &&
                          settings.recentMaps().front() == L"C:\\maps\\map9.scx";

    wchar_t msg[384];
    ::wsprintfW(msg,
                L"recent=1\nsaveClear=%d\nloadClear=%d\ncleared=%d\ncap=%d\n"
                L"dedupe=%d\nsaved=%d\nreloaded=%d\npersisted=%d\nundo=%d\nsettings=%d\n"
        L"remove=%d\nautoUpdate=%d\nupdateCdn=%d\n",
                saveClear ? 1 : 0, loadClear ? 1 : 0,
                cleared ? 1 : 0, capOk ? 1 : 0, dedupeOk ? 1 : 0, saved ? 1 : 0,
                reloaded ? 1 : 0, persisted ? 1 : 0, undoOk ? 1 : 0, settingsOk ? 1 : 0,
                removeOk ? 1 : 0, settings.autoCheckUpdate() ? 1 : 0, updateCdnOk ? 1 : 0);
    WriteDiag(msg);
    return (cleared && capOk && dedupeOk && saved && reloaded && persisted && undoOk &&
            settingsOk && removeOk && updateCdnOk)
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
    int creepSets = 0;
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
        // creep 图案：CV5 flags & 0x0040，4×4 槽位（Installation 当前没有该组）
        int creepTiles = 0;
        for (int i = 0; i < 16; i++) {
            u16 v = 0;
            if (doc.tileset().CreepTile(i, v)) creepTiles++;
        }
        const bool creepOk = creepTiles == 0 || creepTiles >= 9;
        if (creepTiles >= 9) creepSets++;

        const bool ok = files && created && patternOk && rendered && dimsOk && pixelsOk &&
                        zoomOk && creepOk && colors > 1 && unique && bmpOk;
        if (ok) passed++;

        wchar_t line[512];
        ::wsprintfW(line,
                    L"era=%d name=%s files=%d created=%d groups=%d megas=%d vr=%d "
                    L"render=%d dims=%d pixels=%d zoom=%d creepGroup=%d creepTiles=%d "
                    L"colors=%d unique=%d bmp=%d hash=%016I64x\n",
                    era, doc.eraName(), files ? 1 : 0, created ? 1 : 0, groups,
                    static_cast<int>(vx4.size() / 32), static_cast<int>(vr4.size() / 64),
                    rendered ? 1 : 0, dimsOk ? 1 : 0, pixelsOk ? 1 : 0, zoomOk ? 1 : 0,
                    doc.tileset().CreepGroup(), creepTiles,
                    colors, unique ? 1 : 0, bmpOk ? 1 : 0, hash);
        report += line;
    }

    wchar_t tail[64];
    ::wsprintfW(tail, L"pass=%d/8 creep=%d/7\n", passed, creepSets);
    report += tail;
    WriteDiag(report);
    return (passed == 8 && creepSets == 7) ? 0 : 2;
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

// 把 UTF-16 文本写到 %TEMP%\<name>
bool WriteTempTextFile(const wchar_t* name, const std::wstring& text) {
    wchar_t path[MAX_PATH] = L"";
    ::GetTempPathW(MAX_PATH, path);
    ::lstrcatW(path, name);
    const HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = text.empty() ||
                    (::WriteFile(f, text.c_str(),
                                 static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written,
                                 nullptr) &&
                     written == text.size() * sizeof(wchar_t));
    ::CloseHandle(f);
    return ok;
}

// 追加一行 UTF-16 文本到 %TEMP%\<name>（崩溃定位打点用，避免覆盖式打点互相冲掉）
void AppendTempTextLine(const wchar_t* name, const wchar_t* line) {
    wchar_t path[MAX_PATH] = L"";
    ::GetTempPathW(MAX_PATH, path);
    ::lstrcatW(path, name);
    const HANDLE f = ::CreateFileW(path, FILE_APPEND_DATA,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(f, line, static_cast<DWORD>(::lstrlenW(line) * sizeof(wchar_t)),
                &written, nullptr);
    ::WriteFile(f, L"\r\n", 4, &written, nullptr);
    ::CloseHandle(f);
}

std::wstring DialogChildClass(HWND h) {
    wchar_t buf[64] = L"";
    ::GetClassNameW(h, buf, 63);
    return buf;
}

std::wstring DialogChildText(HWND h) {
    wchar_t buf[1024] = L"";
    ::GetWindowTextW(h, buf, 1023);
    return buf;
}

std::wstring EscapeSnapshotText(const std::wstring& in) {
    std::wstring out;
    const std::size_t limit = 120;
    for (std::size_t i = 0; i < in.size() && out.size() < limit; i++) {
        const wchar_t ch = in[i];
        if (ch == L'\r') out += L"\\r";
        else if (ch == L'\n') out += L"\\n";
        else if (ch == L'\t') out += L"\\t";
        else if (ch == L'"') out += L"\\\"";
        else out.push_back(ch);
    }
    if (in.size() > limit) out += L"...";
    return out;
}

// 基准快照跨配置/机器可比：把 exe 目录前缀替换为 <exe>
void NormalizeSnapshotText(std::wstring& text, const std::wstring& exeDir) {
    if (exeDir.empty()) return;
    std::size_t pos = 0;
    while ((pos = text.find(exeDir, pos)) != std::wstring::npos) {
        text.replace(pos, exeDir.size(), L"<exe>");
        pos += 5;
    }
}

struct SnapshotContext {
    std::wstring text;
    std::wstring exeDir;
    int controls = 0;
};

// 输出控件的 ID / 类 / 可用状态 / 文本 / 列表项（最多 20 项）
BOOL CALLBACK SnapshotDialogChild(HWND child, LPARAM lp) {
    SnapshotContext* ctx = reinterpret_cast<SnapshotContext*>(lp);
    ctx->controls++;
    const std::wstring cls = DialogChildClass(child);
    ctx->text += L"  #" + std::to_wstring(::GetDlgCtrlID(child)) + L" " + cls;
    ctx->text += ::IsWindowEnabled(child) ? L" enabled" : L" disabled";
    // 无头模式下父窗口未显示，IsWindowVisible 恒为 false；改看控件自身 WS_VISIBLE
    if ((::GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0) ctx->text += L" hidden";

    if (cls == L"Button") {
        const LRESULT check = ::SendMessageW(child, BM_GETCHECK, 0, 0);
        ctx->text += L" check=";
        ctx->text += check == BST_CHECKED ? L"1" : L"0";
    } else if (cls == L"ListBox" || cls == L"ComboBox") {
        const bool list = cls == L"ListBox";
        const int count = static_cast<int>(::SendMessageW(
            child, list ? LB_GETCOUNT : CB_GETCOUNT, 0, 0));
        const int sel = static_cast<int>(::SendMessageW(
            child, list ? LB_GETCURSEL : CB_GETCURSEL, 0, 0));
        ctx->text += L" count=" + std::to_wstring(count) + L" sel=" + std::to_wstring(sel) + L"\n";
        const int shown = count > 20 ? 20 : count;
        for (int i = 0; i < shown; i++) {
            wchar_t item[512] = L"";
            ::SendMessageW(child, list ? LB_GETTEXT : CB_GETLBTEXT, static_cast<WPARAM>(i),
                           reinterpret_cast<LPARAM>(item));
            std::wstring itemText = item;
            NormalizeSnapshotText(itemText, ctx->exeDir);
            ctx->text += L"    [" + std::to_wstring(i) + L"] \"" + EscapeSnapshotText(itemText) +
                         L"\"\n";
        }
        if (count > shown) ctx->text += L"    ...\n";
        return TRUE;
    } else {
        std::wstring text = DialogChildText(child);
        NormalizeSnapshotText(text, ctx->exeDir);
        if (!text.empty()) ctx->text += L" text=\"" + EscapeSnapshotText(text) + L"\"";
    }
    ctx->text += L"\n";
    return TRUE;
}

// 取当前对话框的控件状态文本
std::wstring SnapshotOf(HWND dlg, const std::wstring& exeDir) {
    SnapshotContext ctx;
    ctx.exeDir = exeDir;
    if (dlg) ::EnumChildWindows(dlg, SnapshotDialogChild, reinterpret_cast<LPARAM>(&ctx));
    return ctx.text;
}

// 读取 UTF-16LE（可带/不带 BOM）或 UTF-8 BOM 文本
bool ReadTextFileAt(const std::wstring& path, std::wstring& out) {
    const HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(f, &size) || size.QuadPart <= 0 || size.QuadPart > (16 << 20)) {
        ::CloseHandle(f);
        return false;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool readOk = ::ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &read,
                                   nullptr) &&
                        read == bytes.size();
    ::CloseHandle(f);
    if (!readOk) return false;

    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
    std::wstring text;
    if (bytes.size() >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, bytes.data() + 3,
                                            static_cast<int>(bytes.size()) - 3, nullptr, 0);
        if (n <= 0) return false;
        text.resize(static_cast<std::size_t>(n));
        ::MultiByteToWideChar(CP_UTF8, 0, bytes.data() + 3,
                              static_cast<int>(bytes.size()) - 3, &text[0], n);
    } else {
        std::size_t offset = 0;
        if (bytes.size() >= 2 && p[0] == 0xFF && p[1] == 0xFE) offset = 2;
        if ((bytes.size() - offset) % 2 != 0) return false;
        text.resize((bytes.size() - offset) / 2);
        if (!text.empty()) std::memcpy(&text[0], bytes.data() + offset, bytes.size() - offset);
    }
    // 统一 LF：避免 git core.autocrlf 检出 CRLF 后与运行期快照不一致
    out.clear();
    out.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch != L'\r') out.push_back(ch);
    }
    return true;
}

// 写 UTF-8（带 BOM），便于 git diff 与编辑器查看
bool WriteTextFileAt(const std::wstring& path, const std::wstring& text) {
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                            static_cast<int>(text.size()), nullptr, 0, nullptr,
                                            nullptr);
    if (bytes <= 0 && !text.empty()) return false;
    std::string utf8(static_cast<std::size_t>(bytes), '\0');
    if (bytes > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &utf8[0],
                              bytes, nullptr, nullptr);
    }
    std::string data;
    data.reserve(utf8.size() + 3);
    data.push_back(static_cast<char>(0xEF));
    data.push_back(static_cast<char>(0xBB));
    data.push_back(static_cast<char>(0xBF));
    data += utf8;

    const HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() ||
                    (::WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written,
                                 nullptr) &&
                     written == data.size());
    ::CloseHandle(f);
    return ok;
}

std::vector<std::wstring> SplitSnapshotLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::size_t lineEnd = end;
        if (lineEnd > start && text[lineEnd - 1] == L'\r') lineEnd--;
        lines.push_back(text.substr(start, lineEnd - start));
        start = end + 1;
    }
    return lines;
}

// 行级差异：LCS 对齐，输出 -/+ 行（两列表最多各 400 行）
std::wstring BuildSnapshotDiff(const std::wstring& baseline, const std::wstring& current,
                               int& diffLines) {
    const std::vector<std::wstring> a = SplitSnapshotLines(baseline);
    const std::vector<std::wstring> b = SplitSnapshotLines(current);
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    // 617 行量级：u16 LCS 表约 0.8 MB
    std::vector<std::vector<u16>> lcs(n + 1, std::vector<u16>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            lcs[i][j] = a[i] == b[j] ? static_cast<u16>(lcs[i + 1][j + 1] + 1)
                                     : (lcs[i + 1][j] >= lcs[i][j + 1] ? lcs[i + 1][j]
                                                                        : lcs[i][j + 1]);
        }
    }

    std::wstring out;
    out += L"baselineLines=" + std::to_wstring(n) + L" currentLines=" + std::to_wstring(m) +
           L"\n";
    const std::size_t kMaxShown = 400;
    std::size_t shown = 0;
    diffLines = 0;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a[i] == b[j]) {
            i++;
            j++;
            continue;
        }
        if (j < m && (i >= n || lcs[i][j + 1] > lcs[i + 1][j])) {
            if (shown++ < kMaxShown) out += L"+ " + b[j] + L"\n";
            diffLines++;
            j++;
        } else {
            if (shown++ < kMaxShown) out += L"- " + a[i] + L"\n";
            diffLines++;
            i++;
        }
    }
    if (shown > kMaxShown) out += L"...\n";
    return out;
}

// --trigger-modal-test：复现真实触发编辑器路径——
// ShowDialog 用 DialogBoxParamW 模态循环，且使用满载数据的地图文档；
// 审计的 open() 是 CreateDialogParamW + 近乎空文档，覆盖不到这条路径。
// 后台线程找到“触发编辑器”窗口后驱动交互（新建/选择/切换面板/删除），最后 WM_CLOSE。
namespace {

struct ModalDriverCtx {
    DWORD pid = 0;
    HWND dlg = nullptr;
};

BOOL CALLBACK FindTriggerEditorWnd(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<ModalDriverCtx*>(lp);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    wchar_t cls[64] = L"";
    ::GetClassNameW(hwnd, cls, 63);
    if (::wcscmp(cls, L"#32770") != 0) return TRUE;
    wchar_t text[128] = L"";
    ::GetWindowTextW(hwnd, text, 127);
    if (::wcscmp(text, L"触发编辑器") != 0) return TRUE;
    ctx->dlg = hwnd;
    return FALSE;
}

DWORD WINAPI TriggerModalDriver(LPVOID param) {
    AppendTempTextLine(L"scmdplus_modal181_stage.txt", L"driver: started");
    auto* ctx = static_cast<ModalDriverCtx*>(param);
    for (int i = 0; i < 60 && !ctx->dlg; i++) {
        ::Sleep(50);
        ::EnumWindows(FindTriggerEditorWnd, reinterpret_cast<LPARAM>(ctx));
    }
    if (!ctx->dlg) return 1;
    ::Sleep(300);
    auto press = [&](int id) {
        ::SendMessageW(ctx->dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
        ::Sleep(80);
    };
    auto mark = [](const wchar_t* stage) {
        AppendTempTextLine(L"scmdplus_modal181_stage.txt", stage);
    };
    // 新建触发 → 选择第 2 条 → 切换 184/185/186/0 面板 → 删除
    mark(L"driver: new");
    press(IDC_D181_NEW);
    HWND list = ::GetDlgItem(ctx->dlg, IDC_D181_TRIGGER_LIST);
    if (::SendMessageW(list, LB_GETCOUNT, 0, 0) > 1) {
        mark(L"driver: select");
        ::SendMessageW(list, LB_SETCURSEL, 1, 0);
        ::SendMessageW(ctx->dlg, WM_COMMAND,
                       MAKEWPARAM(IDC_D181_TRIGGER_LIST, LBN_SELCHANGE),
                       reinterpret_cast<LPARAM>(list));
        ::Sleep(80);
    }
    for (int i = 0; i < 4; i++) {
        wchar_t st[64];
        ::wsprintfW(st, L"driver: modify %d", i + 1);
        mark(st);
        press(IDC_D181_MODIFY);
    }
    mark(L"driver: delete");
    press(IDC_D181_DELETE);
    mark(L"driver: close");
    ::PostMessageW(ctx->dlg, WM_CLOSE, 0, 0);
    return 0;
}

}  // namespace

// 崩溃采集（VEH，首轮机会）：仅 --trigger-modal-test 期间安装。
// 不用 CRT、不分配内存——只 WriteFile 记录异常码与 RIP 到
// %TEMP%\scmdplus_vex.txt，再用 sym_resolve + PDB 解析调用点。
static LONG WINAPI ModalTestVectoredHandler(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000005 && code != 0xC0000409 && code != 0xC00001A5) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    wchar_t buf[192];
#ifdef _WIN64
    const ULONG_PTR rip = ep->ContextRecord->Rip;
#else
    const ULONG_PTR rip = ep->ContextRecord->Eip;
#endif
    ::wsprintfW(buf, L"vex code=0x%08X addr=0x%p rip=0x%08p base=0x%08p rva=0x%08X\n",
                static_cast<unsigned>(code),
                ep->ExceptionRecord->ExceptionAddress,
                reinterpret_cast<void*>(rip),
                reinterpret_cast<void*>(::GetModuleHandleW(nullptr)),
                static_cast<unsigned>(rip - reinterpret_cast<ULONG_PTR>(::GetModuleHandleW(nullptr))));
    wchar_t path[MAX_PATH];
    ::GetTempPathW(MAX_PATH, path);
    ::lstrcatW(path, L"scmdplus_vex.txt");
    const HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ::WriteFile(f, buf, static_cast<DWORD>(::lstrlenW(buf) * sizeof(wchar_t)),
                    &written, nullptr);
        ::CloseHandle(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int RunTriggerModalTest(HWND owner) {
    ::AddVectoredExceptionHandler(1, &ModalTestVectoredHandler);
    // 写 settings.ini 的路径切到临时目录，避免污染用户配置
    wchar_t tempPath[MAX_PATH] = L"";
    ::GetTempPathW(MAX_PATH, tempPath);
    AppSettings& settings = AppSettings::Instance();
    settings.SetDirectory(std::wstring(tempPath) + L"scmdplus_dialog_audit");
    settings.Load();

    // 支持 --trigger-modal-test=<地图路径>：用真实地图复现（否则用合成满载数据）
    WriteTempTextFile(L"scmdplus_modal181_stage.txt", L"");   // 清空旧打点
    std::wstring report = L"modal181=";
    const std::wstring mapPath = ParseValueArg(L"--trigger-modal-test=");
    {
        wchar_t argLine[96];
        ::wsprintfW(argLine, L"stage: args mapLen=%u", static_cast<unsigned>(mapPath.size()));
        AppendTempTextLine(L"scmdplus_modal181_stage.txt", argLine);
    }
    MapDocument doc;
    bool ok = false;
    if (!mapPath.empty()) {
        AppendTempTextLine(L"scmdplus_modal181_stage.txt", L"stage: opening");
        if (!doc.Open(mapPath, std::wstring())) {
            report += L"open-failed\n";
            WriteTempTextFile(L"scmdplus_modal181.txt", report);
            return 2;
        }
        wchar_t head[160];
        ::wsprintfW(head, L"map=%dx%d triggers=%u strings=%u locations=%u\n",
                    doc.width(), doc.height(),
                    static_cast<unsigned>(doc.triggers().BlockCount()),
                    static_cast<unsigned>(doc.strings().Count()),
                    static_cast<unsigned>(doc.locations().Count()));
        report = head + report;
    } else {
        if (!doc.CreateNew(64, 64, 0, std::wstring(), 0)) return 2;
        doc.mutableStrings().Set(1, WideToUtf8(L"测试字符串"));
        for (int i = 0; i < 4; i++) {
            Location loc;
            loc.left = i * 64;
            loc.right = i * 64 + 64;
            loc.bottom = 64;
            loc.stringId = 1;
            doc.mutableLocations().MutableItems().push_back(loc);
        }
        for (int t = 0; t < 3; t++) {
            Trigger trig;
            for (int i = 0; i < 16; i++) {
                trig.conditions[i].type = static_cast<u8>(1 + (i % 23));
                trig.conditions[i].player = i % 9;
                trig.conditions[i].amount = i * 7;
                trig.conditions[i].unitType = static_cast<u16>(i);
                trig.conditions[i].locationId = i % 4;
                trig.conditions[i].comparison = i % 3;
                trig.conditions[i].resourceType = i % 8;
                trig.conditions[i].property = (i % 2) ? 2 : 0;
            }
            for (int a = 0; a < 64; a++) {
                trig.actions[a].type = static_cast<u8>(1 + (a % 57));
                trig.actions[a].stringId = 1;
                trig.actions[a].wavId = 2;
                trig.actions[a].player = a % 9;
                trig.actions[a].locationId = a % 4;
                trig.actions[a].setting = static_cast<u16>(a);
                trig.actions[a].amount = 1;
            }
            trig.executionFlags = 0xF;
            for (int p = 0; p < 27; p++) trig.effectivePlayers[p] = 1;
            doc.mutableTriggers().MutableItems().push_back(trig);
        }
    }

    SetDialogDocument(&doc);
    {
        // 数据概况：触发块数 / 条件行动条数上限 / 越界 ID 上限（崩溃定位用）
        wchar_t head[512];
        unsigned maxCond = 0, maxAct = 0, maxLocId = 0, maxSid = 0, maxLocName = 0;
        for (const Trigger& t : doc.triggers().Items()) {
            maxCond = maxCond > static_cast<unsigned>(t.ConditionCount())
                          ? maxCond : static_cast<unsigned>(t.ConditionCount());
            maxAct = maxAct > static_cast<unsigned>(t.ActionCount())
                          ? maxAct : static_cast<unsigned>(t.ActionCount());
            for (int i = 0; i < t.ConditionCount(); i++) {
                maxLocId = std::max(maxLocId, t.conditions[i].locationId);
            }
            for (int i = 0; i < t.ActionCount(); i++) {
                maxLocId = std::max(maxLocId, t.actions[i].locationId);
                maxSid = std::max(maxSid, t.actions[i].stringId);
            }
        }
        for (const Location& l : doc.locations().Items()) {
            if (l.stringId >= doc.strings().Count()) continue;
            const std::wstring w = Utf8ToWide(doc.strings().At(l.stringId));
            maxLocName = maxLocName > static_cast<unsigned>(w.size())
                              ? maxLocName : static_cast<unsigned>(w.size());
        }
        ::wsprintfW(head,
                    L"stage: doc-ready blocks=%u maxCond=%u maxAct=%u maxLocId=%u "
                    L"maxSid=%u locs=%u strings=%u maxLocName=%u",
                    static_cast<unsigned>(doc.triggers().BlockCount()), maxCond, maxAct,
                    maxLocId, maxSid, static_cast<unsigned>(doc.locations().Count()),
                    static_cast<unsigned>(doc.strings().Count()), maxLocName);
        AppendTempTextLine(L"scmdplus_modal181_stage.txt", head);
    }    ModalDriverCtx ctx;
    ctx.pid = ::GetCurrentProcessId();
    const HANDLE driver = ::CreateThread(nullptr, 0, TriggerModalDriver, &ctx, 0, nullptr);
    AppendTempTextLine(L"scmdplus_modal181_stage.txt", L"stage: opening-dialog");
    ok = ShowRecoveredDialog(IDD_DLG_181, owner);   // 真实模态路径
    AppendTempTextLine(L"scmdplus_modal181_stage.txt", L"stage: dialog-closed");
    ::WaitForSingleObject(driver, 5000);
    ::CloseHandle(driver);
    SetDialogDocument(nullptr);
    AppendTempTextLine(L"scmdplus_modal181_stage.txt", L"stage: doc-released");
    doc.Close();
    AppendTempTextLine(L"scmdplus_modal181_stage.txt", L"stage: complete");

    report += ok ? L"1" : L"0";
    report += L"\n";
    WriteTempTextFile(L"scmdplus_modal181.txt", report);
    return ok ? 0 : 1;
}

// --dialog-audit-test：全对话框控件状态 + 关键按钮路径回归。
// 覆盖：所有独立对话框可创建；155 映射/禁用/应用设定；166/172 置灰；
// 188 所有单位；189 行号与显隐；169/175 预设与结果；182 添加刷子。
int RunDialogAuditTest(HWND owner) {
    // “应用设定”会写 settings.ini，切到临时目录避免污染用户配置；
    // 同时把内存值重置为固定默认，保证快照可跨进程/跨配置对比
    wchar_t tempPath[MAX_PATH] = L"";
    ::GetTempPathW(MAX_PATH, tempPath);
    AppSettings& settings = AppSettings::Instance();
    settings.SetDirectory(std::wstring(tempPath) + L"scmdplus_dialog_audit");
    settings.SetDefaultMineral(8);
    settings.SetDefaultGas(1);
    settings.SetUndoLimit(50);
    settings.SetGameDir(L"");
    settings.SetMpqFiles({});
    settings.SetBrushFiles({});
    settings.SetBackupEnabled(true);
    settings.SetBackupCount(5);
    settings.SetAutoBackupEnabled(false);
    settings.SetAutoBackupMinutes(5);

    wchar_t exePath[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    const std::size_t slash = exeDir.find_last_of(L'\\');
    if (slash != std::wstring::npos) exeDir.resize(slash);

    std::wstring report;
    bool allOk = true;
    const HINSTANCE inst = ::GetModuleHandleW(nullptr);

    auto record = [&report, &allOk](const wchar_t* name, bool ok) {
        if (!ok) allOk = false;
        report += name;
        report += ok ? L"=1\n" : L"=0\n";
    };
    auto open = [inst, owner](int id) -> HWND {
        const DialogEntry* entry = FindDialog(id);
        if (!entry || entry->childPanel) return nullptr;
        return ::CreateDialogParamW(inst, MAKEINTRESOURCEW(id), owner, entry->proc, 0);
    };
    auto intOf = [](HWND dlg, int id) {
        BOOL ok = FALSE;
        const UINT v = ::GetDlgItemInt(dlg, id, &ok, FALSE);
        return ok ? static_cast<int>(v) : -1;
    };
    auto textOf = [](HWND dlg, int id) {
        wchar_t buf[512] = L"";
        ::GetDlgItemTextW(dlg, id, buf, 511);
        return std::wstring(buf);
    };
    auto enabled = [](HWND dlg, int id) {
        HWND h = ::GetDlgItem(dlg, id);
        return h && ::IsWindowEnabled(h);
    };
    auto press = [](HWND dlg, int id) {
        ::SendMessageW(::GetDlgItem(dlg, id), BM_CLICK, 0, 0);
    };
    auto command = [](HWND dlg, int id) {
        ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                       reinterpret_cast<LPARAM>(::GetDlgItem(dlg, id)));
    };
    auto selectList = [](HWND dlg, int id, int index) {
        HWND list = ::GetDlgItem(dlg, id);
        ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
        ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, LBN_SELCHANGE),
                       reinterpret_cast<LPARAM>(list));
    };
    auto selectCombo = [](HWND dlg, int id, int index) {
        HWND combo = ::GetDlgItem(dlg, id);
        ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE),
                       reinterpret_cast<LPARAM>(combo));
    };
    auto comboIndex = [](HWND dlg, int id) {
        return static_cast<int>(::SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0));
    };
    auto listCount = [](HWND dlg, int id) {
        return static_cast<int>(::SendDlgItemMessageW(dlg, id, LB_GETCOUNT, 0, 0));
    };
    auto listSel = [](HWND dlg, int id) {
        return static_cast<int>(::SendDlgItemMessageW(dlg, id, LB_GETCURSEL, 0, 0));
    };

    // 1) 全部独立对话框可创建，并输出控件状态快照
    const int kDialogIds[] = {103, 155, 163, 164, 166, 167, 169, 171, 172, 173, 174,
                              175, 179, 180, 181, 182, 183, 187, 188, 189, 191, 192,
                              195, 196, 197, 198, 199, 205};
    bool createAll = true;
    std::wstring snapshotText;
    int snapshotControls = 0;
    for (int id : kDialogIds) {
        HWND dlg = open(id);
        if (!dlg || !::GetWindow(dlg, GW_CHILD)) {
            createAll = false;
            continue;
        }
        const DialogEntry* entry = FindDialog(id);
        snapshotText += L"[dialog " + std::to_wstring(id) + L"]";
        if (entry) snapshotText += std::wstring(L" ") + entry->caption;
        snapshotText += L"\n";
        SnapshotContext ctx;
        ctx.exeDir = exeDir;
        ::EnumChildWindows(dlg, SnapshotDialogChild, reinterpret_cast<LPARAM>(&ctx));
        snapshotText += ctx.text;
        snapshotControls += ctx.controls;
        ::DestroyWindow(dlg);
    }
    record(L"createAll", createAll);
    record(L"snapshot", WriteTempTextFile(L"scmdplus_dialog_snapshot.txt", snapshotText) &&
                            snapshotControls > 0);
    report += L"snapshotControls=" + std::to_wstring(snapshotControls) + L"\n";

    // 可选：保存基准快照 / 与基准对比（--dialog-snapshot-baseline=<path> /
    // --dialog-snapshot-diff=<path>）
    const std::wstring baselineSave = ParseValueArg(L"--dialog-snapshot-baseline=");
    if (!baselineSave.empty()) {
        record(L"baselineSave", WriteTextFileAt(baselineSave, snapshotText));
    }
    const std::wstring baselineDiff = ParseValueArg(L"--dialog-snapshot-diff=");
    if (!baselineDiff.empty()) {
        std::wstring baseline;
        if (!ReadTextFileAt(baselineDiff, baseline)) {
            report += L"snapshotError=missing-baseline\n";
            record(L"snapshotMatch", false);
        } else if (baseline == snapshotText) {
            report += L"snapshotDiffLines=0\n";
            record(L"snapshotMatch", true);
        } else {
            int diffLines = 0;
            const std::wstring diff = BuildSnapshotDiff(baseline, snapshotText, diffLines);
            WriteTempTextFile(L"scmdplus_dialog_snapshot.diff.txt", diff);
            report += L"snapshotDiffLines=" + std::to_wstring(diffLines) + L"\n";
            record(L"snapshotMatch", false);
        }
    }

    // 2) 155：控件映射、原版剩余选项接线、“应用设定”不关闭
    {
        bool ok = true;
        AppSettings& s = AppSettings::Instance();
        s.SetDefaultMineral(12);
        s.SetDefaultGas(3);
        s.SetUndoLimit(75);
        HWND dlg = open(155);
        ok = dlg && intOf(dlg, 1010) == 12 && intOf(dlg, 1011) == 3 && intOf(dlg, 1012) == 75;
        // 原 7 个“未建模即禁用”的选项现已全部接线并可用
        const int kWired[] = {1005, 1009, 1013, 1093, 1094, 1129, 1140};
        for (int id : kWired) ok = ok && enabled(dlg, id);
        ok = ok && intOf(dlg, 1094) == 3400 && intOf(dlg, 1009) == 1700 &&
             intOf(dlg, 1013) == 500;
        ok = ok && ::IsDlgButtonChecked(dlg, 1129) == BST_CHECKED &&
             ::IsDlgButtonChecked(dlg, 1140) == BST_CHECKED &&
             ::IsDlgButtonChecked(dlg, 1005) == BST_UNCHECKED &&
             ::IsDlgButtonChecked(dlg, 1093) == BST_UNCHECKED;
        if (dlg) {
            // 改全部新选项后“应用设定”，断言写回且对话框不关闭
            ::SetDlgItemInt(dlg, 1010, 20, FALSE);
            ::SetDlgItemInt(dlg, 1094, 100, FALSE);
            ::SetDlgItemInt(dlg, 1009, 50, FALSE);
            ::SetDlgItemInt(dlg, 1013, 30, FALSE);
            ::CheckDlgButton(dlg, 1005, BST_CHECKED);
            ::CheckDlgButton(dlg, 1093, BST_CHECKED);
            ::CheckDlgButton(dlg, 1129, BST_UNCHECKED);
            ::CheckDlgButton(dlg, 1140, BST_UNCHECKED);
            press(dlg, 1007);   // 应用设定
            ok = ok && ::IsWindow(dlg) && s.defaultMineral() == 20 &&
                 s.maxUnits() == 100 && s.maxSpUnits() == 50 && s.maxLocations() == 30 &&
                 s.startMaximized() && s.triggerBaseNames() && !s.loadFitZoom() &&
                 !s.singleFile();
            // 重置默认恢复控件值（不写回）
            press(dlg, 1014);
            ok = ok && intOf(dlg, 1094) == 3400 && intOf(dlg, 1009) == 1700 &&
                 intOf(dlg, 1013) == 500;
            ::DestroyWindow(dlg);
            // 恢复设置，避免影响后续断言
            s.SetStartMaximized(false);
            s.SetTriggerBaseNames(false);
            s.SetLoadFitZoom(true);
            s.SetSingleFile(true);
            s.SetMaxUnits(3400);
            s.SetMaxSpUnits(1700);
            s.SetMaxLocations(500);
        }
        record(L"settings155", ok);
    }

    // 3) 166：两套方格（主 + 静态）全部可用，确定后写回 GridSettings
    {
        GridSettings seed;
        seed.enabled = true;
        seed.cellX = 16;
        seed.cellY = 24;
        SeedGridSettings(seed);
        HWND dlg = open(166);
        bool ok = dlg != nullptr;
        const int kAll[] = {1044, 1045, 1046, 1047, 1048,
                            1049, 1050, 1051, 1052, 1053};
        for (int id : kAll) ok = ok && enabled(dlg, id);
        ok = ok && intOf(dlg, 1044) == 16 && intOf(dlg, 1045) == 24;
        if (dlg) {
            ::SetDlgItemInt(dlg, 1049, 8, FALSE);
            ::SetDlgItemInt(dlg, 1050, 12, FALSE);
            ::SetDlgItemInt(dlg, 1051, 4, TRUE);
            ::CheckDlgButton(dlg, 1053, BST_CHECKED);
            press(dlg, 1);   // 确定
            ok = ok && LastGridSettingsAccepted() &&
                 LastGridSettings().cellX == 16 && LastGridSettings().cellY == 24 &&
                 LastGridSettings().staticEnabled &&
                 LastGridSettings().staticCellX == 8 &&
                 LastGridSettings().staticCellY == 12 &&
                 LastGridSettings().staticOffsetX == 4;
        }
        record(L"grid166", ok);
    }

    // 4) 172：units.dat 属性编辑控件只读（无地图文档时的只读预览）
    {
        HWND dlg = open(172);
        bool ok = dlg != nullptr;
        const int kDisabled[] = {1073, 1074, 1075, 1076, 1077,
                                 1078, 1079, 1080, 1081, 1082};
        for (int id : kDisabled) ok = ok && !enabled(dlg, id);
        if (dlg) ::DestroyWindow(dlg);
        record(L"unitNames172", ok);
    }

    // 4a-0) 174：在线升级只预留 CDN 配置，界面统一显示“已是最新版本”
    {
        HWND dlg = open(174);
        bool ok = dlg != nullptr;
        if (dlg) {
            ok = ok && textOf(dlg, IDC_D174_1091).find(L"最新版本") != std::wstring::npos;
            ::DestroyWindow(dlg);
        }
        record(L"updater174", ok);
    }

    // 4a) 163：兼容性问题列表可填充（问题由主窗口开图后检测）
    {
        SeedCompatIssues({L"单位坐标越界 2 个（点「保存」删除）",
                          L"地点范围异常 1 个（点「保存」修正为 1 格）",
                          L"装饰物坐标越界 3 个（点「保存」纠正）"});
        HWND dlg = open(163);
        bool ok = dlg != nullptr;
        ok = ok && listCount(dlg, IDC_D163_1038) == 3;
        if (dlg) ::DestroyWindow(dlg);
        record(L"compat163", ok);
    }

    // 4a-1) 163：自动修复路径（越界/非法单位、装饰物、SP、地点矩形）
    {
        MapDocument doc;
        bool ok = doc.CreateNew(16, 16, 0, std::wstring());
        if (ok) {
            UnitRecord outOfBounds;
            outOfBounds.serial = 1;
            outOfBounds.type = 0;
            outOfBounds.x = 600;   // 16 格 = 512 px
            doc.mutableUnits().push_back(outOfBounds);
            UnitRecord badType;
            badType.serial = 2;
            badType.type = 300;    // ≥ 228
            badType.x = 32;
            badType.y = 32;
            doc.mutableUnits().push_back(badType);
            Location badLoc;
            badLoc.left = 10;
            badLoc.top = 10;
            badLoc.right = 5;      // 反向矩形
            badLoc.bottom = 5;
            doc.mutableLocations().MutableItems().push_back(badLoc);
            doc.mutableDoodads().MutableItems().push_back(Doodad{1, 600, 0, 0, 0});
            SpriteRecord badSprite;
            badSprite.type = 1;
            badSprite.x = 600;
            doc.mutableSprites().MutableItems().push_back(badSprite);

            const int fixed = RepairCompatibilityIssues(doc);
            const auto& locs = doc.locations().Items();
            ok = fixed >= 5 && doc.units().empty() && locs.size() == 1 &&
                 locs[0].right > locs[0].left && locs[0].bottom > locs[0].top &&
                 doc.doodads().Count() == 0 && doc.sprites().Count() == 0 &&
                 RepairCompatibilityIssues(doc) == 0;   // 再次调用应为幂等
        }
        record(L"compat163Fix", ok);
    }

    // 4a-2) 206：触发文本视图（只读，需地图文档）
    {
        MapDocument doc;
        bool ok = doc.CreateNew(16, 16, 0, std::wstring(), 0);
        if (ok) {
            Trigger t;
            t.effectivePlayers[0] = 1;
            t.conditions[0].type = 1;
            t.conditions[0].amount = 100;
            t.conditions[0].comparison = 0;
            t.actions[0].type = 9;
            t.actions[0].stringId = 1;
            doc.mutableTriggers().MutableItems().push_back(t);
            doc.mutableStrings().Set(1, "Hello");
            SetDialogDocument(&doc);
            HWND dlg = open(206);
            ok = dlg != nullptr;
            if (dlg) {
                const std::wstring text = textOf(dlg, IDC_D206_TEXT);
                ok = ok && text.find(L"触发 1/1") != std::wstring::npos &&
                     text.find(L"T players=1 flags=0") != std::wstring::npos &&
                     text.find(L"C type=1") != std::wstring::npos &&
                     text.find(L"A type=9") != std::wstring::npos &&
                     text.find(L"Hello") != std::wstring::npos;

                // 原样应用：序列化结果应逐字节一致（无损往返）
                const Buffer roundTripBefore = doc.triggers().Serialize();
                command(dlg, IDC_D206_APPLY);
                ok = ok && doc.triggers().Serialize() == roundTripBefore &&
                     textOf(dlg, IDC_D206_STATUS).find(L"已应用") != std::wstring::npos;

                // 应用修改后的文本：玩家 1/2、仅一次、条件改为 type=2/player=3/amount=7/cmp=1
                ::SetDlgItemTextW(
                    dlg, IDC_D206_TEXT,
                    L"T players=1,2 flags=1\r\n"
                    L"C type=2 player=3 amount=7 unit=0 loc=0 cmp=1 res=0 prop=0\r\n"
                    L"A type=9 player=0 amount=0 unit=0 loc=0 str=1 wav=0 time=0 target=0 "
                    L"prop=0\r\n");
                command(dlg, IDC_D206_APPLY);
                const auto& items = doc.triggers().Items();
                ok = ok && items.size() == 1 && items[0].executionFlags == 1 &&
                     items[0].effectivePlayers[0] == 1 && items[0].effectivePlayers[1] == 1 &&
                     items[0].ConditionCount() == 1 && items[0].conditions[0].type == 2 &&
                     items[0].conditions[0].player == 3 && items[0].conditions[0].amount == 7 &&
                     items[0].conditions[0].comparison == 1 &&
                     items[0].ActionCount() == 1 && items[0].actions[0].type == 9 &&
                     items[0].actions[0].stringId == 1 &&
                     textOf(dlg, IDC_D206_STATUS).find(L"已应用") != std::wstring::npos;

                // 非法文本：只报错，不改地图
                ::SetDlgItemTextW(dlg, IDC_D206_TEXT, L"X bad=1");
                command(dlg, IDC_D206_APPLY);
                ok = ok && doc.triggers().Items()[0].conditions[0].type == 2 &&
                     textOf(dlg, IDC_D206_STATUS).find(L"解析失败") != std::wstring::npos;
                ::DestroyWindow(dlg);
            }
            SetDialogDocument(nullptr);
        }
        record(L"triggerTextView", ok);
    }

    // 4b) 180：状态位回填 + 位序映射（无敌=bit4 / 幻影=bit3 / 钻地=bit1 / 隐形=bit0 / 升起=bit2）
    {
        UnitProps seed;
        seed.flags = 0x11;   // 隐形 + 无敌
        seed.ammo = 5;
        SeedUnitProps(seed);
        HWND dlg = open(180);
        bool ok = dlg != nullptr;
        if (dlg) {
            ok = ok && ::IsDlgButtonChecked(dlg, IDC_D180_1073) == BST_CHECKED &&
                 ::IsDlgButtonChecked(dlg, IDC_D180_1076) == BST_CHECKED &&
                 ::IsDlgButtonChecked(dlg, IDC_D180_1074) == BST_UNCHECKED &&
                 ::IsDlgButtonChecked(dlg, IDC_D180_1075) == BST_UNCHECKED &&
                 ::IsDlgButtonChecked(dlg, IDC_D180_1077) == BST_UNCHECKED &&
                 intOf(dlg, IDC_D180_1082) == 5;
            ::CheckDlgButton(dlg, IDC_D180_1073, BST_UNCHECKED);   // 无敌
            ::CheckDlgButton(dlg, IDC_D180_1074, BST_CHECKED);     // 幻影
            ::CheckDlgButton(dlg, IDC_D180_1075, BST_CHECKED);     // 钻地
            ::CheckDlgButton(dlg, IDC_D180_1077, BST_CHECKED);     // 升起
            ::SetDlgItemInt(dlg, IDC_D180_1082, 9, FALSE);
            press(dlg, IDC_D180_1);
            const UnitProps& p = LastUnitProps();
            ok = ok && p.flags == 0x0F && p.ammo == 9;
            if (::IsWindow(dlg)) ::DestroyWindow(dlg);
        }
        record(L"unitProps180", ok);
    }

    // 5) 188：“所有单位”勾选后 255 + 禁用输入，取消勾选恢复
    {
        HWND dlg = open(188);
        bool ok = dlg != nullptr;
        if (dlg) {
            press(dlg, 1119);
            ok = ok && !enabled(dlg, 1118) && textOf(dlg, 1118) == L"255";
            press(dlg, 1119);
            ok = ok && enabled(dlg, 1118);
            ::DestroyWindow(dlg);
        }
        record(L"allUnits188", ok);
    }

    // 6) 189：行号随正文更新，预览框转义控制字符，“一直显示”切换行号列
    {
        HWND dlg = open(189);
        bool ok = dlg != nullptr;
        if (dlg) {
            ::SetDlgItemTextW(dlg, 1120, L"a\r\nb\r\nc");
            // 程序化 WM_SETTEXT 不产生 EN_CHANGE，手动补发以覆盖行号刷新逻辑
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDC_D189_1120, EN_CHANGE),
                           reinterpret_cast<LPARAM>(::GetDlgItem(dlg, IDC_D189_1120)));
            ok = ok && textOf(dlg, 1121) == L"1\r\n2\r\n3";
            ok = ok && textOf(dlg, 1079) == L"a\r\nb\r\nc";
            // 0x02=游戏内换行、0x07+色号=颜色切换、其余控制字符=<0xNN>
            ::SetDlgItemTextW(dlg, 1120, L"a\x02" L"b\x07" L"\x04" L"c\x01");
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDC_D189_1120, EN_CHANGE),
                           reinterpret_cast<LPARAM>(::GetDlgItem(dlg, IDC_D189_1120)));
            ok = ok && textOf(dlg, 1121) == L"1\r\n2";
            ok = ok && textOf(dlg, 1079) == L"a\r\nb[颜色4]c<0x01>";
            press(dlg, 1122);
            ok = ok && !::IsWindowVisible(::GetDlgItem(dlg, 1121));
            ::DestroyWindow(dlg);
        }
        record(L"string189", ok);
    }

    // 7) 169：尺寸预设联动 + 地形集写回 g_mapSize
    {
        HWND dlg = open(169);
        bool ok = dlg != nullptr;
        if (dlg) {
            selectList(dlg, 1061, 3);   // 192 x 192
            ok = ok && intOf(dlg, 1062) == 192 && intOf(dlg, 1063) == 192;
            selectList(dlg, 1060, 5);   // Desert
            press(dlg, 1);              // 创建地图
            const MapSizeParams& p = LastMapSize();
            ok = ok && p.width == 192 && p.height == 192 && p.era == 5;
        }
        record(L"newMap169", ok);
    }

    // 8) 175：按当前地图预填；改尺寸/目标地形集后写回 ResizeParams
    {
        ResizeParams seed;
        seed.width = 128;
        seed.height = 128;
        seed.currentEra = 4;
        seed.era = -1;
        SeedResizeParams(seed);
        HWND dlg = open(175);
        bool ok = dlg && intOf(dlg, 1062) == 128 && intOf(dlg, 1063) == 128 &&
                  listSel(dlg, 1060) == 4 && listSel(dlg, 1095) == 4;
        if (dlg) {
            ::SetDlgItemInt(dlg, 1062, 96, FALSE);
            ::SetDlgItemInt(dlg, 1063, 96, FALSE);
            selectList(dlg, 1095, 7);   // Twilight
            press(dlg, 1);              // 重设 / 改变 Tileset
            const ResizeParams& p = LastResizeParams();
            ok = ok && p.width == 96 && p.height == 96 && p.era == 7;
        }
        record(L"resize175", ok);
    }

    // 9) 182：添加当前刷子并应用
    {
        TerrainBrush current;
        current.SetSingle(0x0040, L"审计刷");
        SeedCustomBrushes({}, current, -1);
        HWND dlg = open(182);
        bool ok = dlg != nullptr;
        if (dlg) {
            ::SetDlgItemTextW(dlg, 1079, L"审计刷");
            press(dlg, 1006);   // 添加刷子
            ok = ok && listCount(dlg, 1060) == 1;
            // 182 没有 IDOK 按钮，回车路径直接发 WM_COMMAND
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            ok = ok && LastCustomBrushAccepted() && LastCustomBrushes().size() == 1 &&
                 LastCustomBrushes()[0].name == L"审计刷";
            if (::IsWindow(dlg)) ::DestroyWindow(dlg);
        }
        record(L"brush182", ok);
    }

    // 10) 需要地图文档的交互按钮：167 / 173 / 179 / 198 / 199
    {
        MapDocument doc;
        const bool docOk = doc.CreateNew(32, 32, 0, std::wstring(), 0);
        SetDialogDocument(&doc);
        Buffer probe;

        // 交互前后控件状态：写入 scmdplus_dialog_interaction.txt，并要求每对快照都有差异
        std::wstring interactionText;
        int interactionPairs = 0;
        bool interactionOk = true;
        auto capturePair = [&](const wchar_t* name, const std::wstring& before,
                               const std::wstring& after) {
            int diffLines = 0;
            const std::wstring diff = BuildSnapshotDiff(before, after, diffLines);
            interactionText += std::wstring(L"[") + name + L" before]\n" + before;
            interactionText += std::wstring(L"[") + name + L" after]\n" + after;
            interactionText += std::wstring(L"[") + name + L" diff] diffLines=" +
                               std::to_wstring(diffLines) + L"\n" + diff;
            interactionPairs++;
            if (diffLines <= 0) interactionOk = false;
        };

        // 167：添加 / 改名 / 删除附加文件
        bool mpqOk = docOk;
        if (docOk) {
            doc.SetExtraFile("custom.bin", Buffer{1, 2, 3});
            doc.SetExtraFile("other.bin", Buffer{4, 5, 6});
            HWND dlg = open(167);
            mpqOk = mpqOk && dlg && listCount(dlg, IDC_D167_1049) == 2;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                ::SetDlgItemTextW(dlg, IDC_D167_1056, L"added.bin");
                press(dlg, IDC_D167_1053);   // 添加
                mpqOk = mpqOk && listCount(dlg, IDC_D167_1049) == 3 &&
                        doc.ReadExtraFile("added.bin", probe);
                const std::wstring afterAdd = SnapshotOf(dlg, exeDir);
                selectList(dlg, IDC_D167_1049, 2);
                ::SetDlgItemTextW(dlg, IDC_D167_1056, L"renamed.bin");
                press(dlg, IDC_D167_1052);   // 编辑（改名）
                mpqOk = mpqOk && doc.ReadExtraFile("renamed.bin", probe) &&
                        !doc.ReadExtraFile("added.bin", probe);
                const std::wstring afterRename = SnapshotOf(dlg, exeDir);
                selectList(dlg, IDC_D167_1049, 0);
                press(dlg, IDC_D167_1092);   // 删除
                mpqOk = mpqOk && !doc.ReadExtraFile("custom.bin", probe) &&
                        doc.extraFiles().size() == 2;
                capturePair(L"167-add", before, afterAdd);
                capturePair(L"167-rename", afterAdd, afterRename);
                capturePair(L"167-delete", afterRename, SnapshotOf(dlg, exeDir));
                ::DestroyWindow(dlg);
            }
        }
        record(L"mpq167", mpqOk);

        // 179：设置 / 还原开关名
        bool switchOk = docOk;
        if (docOk) {
            doc.mutableSwitchNames().Set(0, 0);
            HWND dlg = open(179);
            switchOk = switchOk && dlg && listCount(dlg, IDC_D179_1060) == 256;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                ::SetDlgItemTextW(dlg, IDC_D179_1079, L"审计开关");
                press(dlg, IDC_D179_1110);   // 设置
                const u32 sid = doc.switchNames().Get(0);
                switchOk = switchOk && sid != 0 &&
                           doc.strings().At(sid) == WideToUtf8(L"审计开关");
                const std::wstring afterSet = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D179_1111);   // 还原
                switchOk = switchOk && doc.switchNames().Get(0) == 0;
                capturePair(L"179-set", before, afterSet);
                capturePair(L"179-restore", afterSet, SnapshotOf(dlg, exeDir));
                ::DestroyWindow(dlg);
            }
        }
        record(L"switch179", switchOk);

        // 198：设置 / 清除声音路径
        bool soundOk = docOk;
        if (docOk) {
            doc.mutableSounds().Set(0, 0);
            HWND dlg = open(198);
            soundOk = soundOk && dlg && listCount(dlg, IDC_D198_LIST) == 512;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                ::SetDlgItemTextW(dlg, IDC_D198_PATH, L"sound\\probe.wav");
                press(dlg, IDC_D198_SET);
                const u32 sid = doc.sounds().Get(0);
                soundOk = soundOk && sid != 0 &&
                          doc.strings().At(sid) == WideToUtf8(L"sound\\probe.wav");
                const std::wstring afterSet = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D198_CLEAR);
                soundOk = soundOk && doc.sounds().Get(0) == 0;
                capturePair(L"198-set", before, afterSet);
                capturePair(L"198-clear", afterSet, SnapshotOf(dlg, exeDir));
                ::DestroyWindow(dlg);
            }
        }
        record(L"sound198", soundOk);

        // 199：更新简报文本（构造一条类型 3 行动）
        bool briefOk = docOk;
        if (docOk) {
            Trigger brief;
            brief.actions[0].type = 3;   // 文本
            brief.actions[0].player = 0;
            brief.actions[0].stringId = 0;
            doc.mutableBriefing().MutableItems().clear();
            doc.mutableBriefing().MutableItems().push_back(brief);
            HWND dlg = open(199);
            briefOk = briefOk && dlg && listCount(dlg, IDC_D199_LIST) == 1 &&
                      enabled(dlg, IDC_D199_UPDATE);
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                ::SetDlgItemTextW(dlg, IDC_D199_TEXT, L"简报文本");
                press(dlg, IDC_D199_UPDATE);
                auto& items = doc.mutableBriefing().MutableItems();
                const u32 sid = items.empty() ? 0 : items[0].actions[0].stringId;
                briefOk = briefOk && sid != 0 &&
                          doc.strings().At(sid) == WideToUtf8(L"简报文本");
                capturePair(L"199-update", before, SnapshotOf(dlg, exeDir));
                ::DestroyWindow(dlg);
            }
        }
        record(L"brief199", briefOk);

        // 181：触发编辑器 新建 / 复制 / 修改 / 删除
        bool triggerOk = docOk;
        if (docOk) {
            doc.mutableTriggers().MutableItems().clear();
            HWND dlg = open(181);
            triggerOk = triggerOk && dlg && listCount(dlg, IDC_D181_TRIGGER_LIST) == 0;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D181_NEW);
                triggerOk = triggerOk && doc.triggers().BlockCount() == 1 &&
                            listCount(dlg, IDC_D181_TRIGGER_LIST) == 1;
                const std::wstring afterNew = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D181_COPY);
                triggerOk = triggerOk && doc.triggers().BlockCount() == 2 &&
                            listCount(dlg, IDC_D181_TRIGGER_LIST) == 2;
                const std::wstring afterCopy = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D181_MODIFY);
                const std::wstring afterModify = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D181_DELETE);
                triggerOk = triggerOk && doc.triggers().BlockCount() == 1;
                capturePair(L"181-new", before, afterNew);
                capturePair(L"181-copy", afterNew, afterCopy);
                capturePair(L"181-modify", afterCopy, afterModify);
                capturePair(L"181-delete", afterModify, SnapshotOf(dlg, exeDir));

                // 玩家树筛选：P2 下无触发，P1 / 全 仍可见
                HWND tree = ::GetDlgItem(dlg, IDC_D181_PLAYER_TREE);
                HTREEITEM allItem = reinterpret_cast<HTREEITEM>(
                    ::SendMessageW(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0));
                HTREEITEM p1 = reinterpret_cast<HTREEITEM>(
                    ::SendMessageW(tree, TVM_GETNEXTITEM, TVGN_NEXT,
                                   reinterpret_cast<LPARAM>(allItem)));
                HTREEITEM p2 = reinterpret_cast<HTREEITEM>(
                    ::SendMessageW(tree, TVM_GETNEXTITEM, TVGN_NEXT,
                                   reinterpret_cast<LPARAM>(p1)));
                ::SendMessageW(tree, TVM_SELECTITEM, TVGN_CARET,
                               reinterpret_cast<LPARAM>(p2));
                triggerOk = triggerOk && listCount(dlg, IDC_D181_TRIGGER_LIST) == 0;
                ::SendMessageW(tree, TVM_SELECTITEM, TVGN_CARET,
                               reinterpret_cast<LPARAM>(p1));
                triggerOk = triggerOk && listCount(dlg, IDC_D181_TRIGGER_LIST) == 1;
                ::SendMessageW(tree, TVM_SELECTITEM, TVGN_CARET,
                               reinterpret_cast<LPARAM>(allItem));
                triggerOk = triggerOk && listCount(dlg, IDC_D181_TRIGGER_LIST) == 1;
                ::DestroyWindow(dlg);
            }
        }
        record(L"trigger181", triggerOk);

        // 193 / 194：修改字段并确定
        bool condOk = true;
        {
            HWND dlg = open(193);
            condOk = dlg != nullptr;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                ::SetDlgItemInt(dlg, IDC_D193_AMOUNT, 7, FALSE);
                capturePair(L"193-amount", before, SnapshotOf(dlg, exeDir));
                // 类型下拉 <-> 类型号 双向同步
                selectCombo(dlg, IDC_D193_TYPE, 3);              // 类型 4
                condOk = condOk && intOf(dlg, IDC_D193_TYPE_ID) == 4;
                ::SetDlgItemInt(dlg, IDC_D193_TYPE_ID, 22, FALSE);   // 22 -> 索引 21
                condOk = condOk && comboIndex(dlg, IDC_D193_TYPE) == 21;
                ::SetDlgItemInt(dlg, IDC_D193_TYPE_ID, 99, FALSE);   // 越界 -> 自定义(23)
                condOk = condOk && comboIndex(dlg, IDC_D193_TYPE) == 23;
                // 类型 → 字段启用矩阵
                selectCombo(dlg, IDC_D193_TYPE, 0);              // 类型 1 倒计时
                condOk = condOk && enabled(dlg, IDC_D193_AMOUNT) &&
                         enabled(dlg, IDC_D193_COMPARE) &&
                         !enabled(dlg, IDC_D193_LOCATION) &&
                         !enabled(dlg, IDC_D193_PLAYER) &&
                         !enabled(dlg, IDC_D193_UNIT);
                selectCombo(dlg, IDC_D193_TYPE, 21);             // 类型 22 总是
                condOk = condOk && !enabled(dlg, IDC_D193_AMOUNT) &&
                         !enabled(dlg, IDC_D193_COMPARE) &&
                         !enabled(dlg, IDC_D193_LOCATION);
                selectCombo(dlg, IDC_D193_TYPE, 23);             // 自定义：全开
                condOk = condOk && enabled(dlg, IDC_D193_AMOUNT) &&
                         enabled(dlg, IDC_D193_LOCATION) &&
                         enabled(dlg, IDC_D193_PLAYER);
                ::SetDlgItemInt(dlg, IDC_D193_TYPE_ID, 22, FALSE);
                command(dlg, IDC_D193_1);
                condOk = condOk && LastTriggerConditionAccepted() &&
                         LastTriggerCondition().amount == 7 &&
                         LastTriggerCondition().type == 22;
                if (::IsWindow(dlg)) ::DestroyWindow(dlg);
            }
        }
        record(L"cond193", condOk);

        bool actOk = true;
        {
            HWND dlg = open(194);
            actOk = dlg != nullptr;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                ::SetDlgItemInt(dlg, IDC_D194_AMOUNT, 9, FALSE);
                capturePair(L"194-amount", before, SnapshotOf(dlg, exeDir));
                // 类型下拉 <-> 类型号 双向同步
                selectCombo(dlg, IDC_D194_TYPE, 2);              // 类型 3
                actOk = actOk && intOf(dlg, IDC_D194_TYPE_ID) == 3;
                ::SetDlgItemInt(dlg, IDC_D194_TYPE_ID, 57, FALSE);   // 57 -> 索引 56
                actOk = actOk && comboIndex(dlg, IDC_D194_TYPE) == 56;
                ::SetDlgItemInt(dlg, IDC_D194_TYPE_ID, 99, FALSE);   // 越界 -> 自定义(57)
                actOk = actOk && comboIndex(dlg, IDC_D194_TYPE) == 57;
                // 类型 → 字段启用矩阵
                selectCombo(dlg, IDC_D194_TYPE, 48);             // 类型 49 注释：仅文本
                actOk = actOk && enabled(dlg, IDC_D194_STRING) &&
                        !enabled(dlg, IDC_D194_TIME) &&
                        !enabled(dlg, IDC_D194_LOCATION);
                selectCombo(dlg, IDC_D194_TYPE, 3);              // 类型 4 等待：仅时间
                actOk = actOk && enabled(dlg, IDC_D194_TIME) &&
                        !enabled(dlg, IDC_D194_STRING);
                ::SetDlgItemInt(dlg, IDC_D194_TYPE_ID, 1, FALSE);
                command(dlg, IDC_D194_1);
                actOk = actOk && LastTriggerActionAccepted() &&
                        LastTriggerAction().amount == 9 &&
                        LastTriggerAction().type == 1;
                if (::IsWindow(dlg)) ::DestroyWindow(dlg);
            }
        }
        record(L"act194", actOk);

        // 196：切换玩家 / 控制者 / 队伍
        bool playerOk = true;
        {
            PlayerRoster roster;
            roster.Reset();
            SeedPlayerRoster(roster);
            HWND dlg = open(196);
            playerOk = dlg != nullptr;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                selectList(dlg, IDC_D196_PLAYER_LIST, 1);
                const std::wstring afterPlayer = SnapshotOf(dlg, exeDir);
                selectCombo(dlg, IDC_D196_CONTROLLER, 1);
                const std::wstring afterController = SnapshotOf(dlg, exeDir);
                selectList(dlg, IDC_D196_FORCE_LIST, 1);
                const std::wstring afterForce = SnapshotOf(dlg, exeDir);
                capturePair(L"196-player", before, afterPlayer);
                capturePair(L"196-controller", afterPlayer, afterController);
                capturePair(L"196-force", afterController, afterForce);
                press(dlg, IDC_D196_2);
            }
        }
        record(L"player196", playerOk);

        // 197：切换升级项 / 玩家 / 使用全局默认
        bool restrictionOk = true;
        {
            UpgradeTechRestrictions restrictions;
            restrictions.Reset();
            SeedRestrictions(restrictions);
            HWND dlg = open(197);
            restrictionOk = dlg != nullptr;
            if (dlg) {
                const std::wstring before = SnapshotOf(dlg, exeDir);
                selectList(dlg, IDC_D197_UPGRADE_LIST, 1);
                const std::wstring afterUpgrade = SnapshotOf(dlg, exeDir);
                selectCombo(dlg, IDC_D197_PLAYER, 1);
                const std::wstring afterPlayer = SnapshotOf(dlg, exeDir);
                press(dlg, IDC_D197_UP_DEFAULTS);
                const std::wstring afterDefaults = SnapshotOf(dlg, exeDir);
                capturePair(L"197-upgrade", before, afterUpgrade);
                capturePair(L"197-player", afterUpgrade, afterPlayer);
                capturePair(L"197-defaults", afterPlayer, afterDefaults);
                press(dlg, IDC_D197_2);
            }
        }
        record(L"restriction197", restrictionOk);

        // 173：备份列表填充（生成 1 份备份后打开）
        bool backupOk = docOk;
        if (docOk) {
            ClearAllBackups();
            wchar_t backupTemp[MAX_PATH] = L"";
            ::GetTempPathW(MAX_PATH, backupTemp);
            const std::wstring source = std::wstring(backupTemp) + L"scmdplus_audit_backup.scx";
            WriteTempTextFile(L"scmdplus_audit_backup.scx", L"audit");
            const bool backedUp = MakeBackup(source, 3);
            HWND dlg = open(173);
            backupOk = backupOk && backedUp && dlg && listCount(dlg, IDC_D173_1060) == 1;
            if (dlg) {
                selectList(dlg, IDC_D173_1060, 0);
                backupOk = backupOk && listCount(dlg, IDC_D173_1083) == 1;
                ::DestroyWindow(dlg);
            }
            ClearAllBackups();
            ::DeleteFileW(source.c_str());
        }
        record(L"backup173", backupOk);

        // 172（有文档）：按单位类型列出实例并写回 UNIT 字段
        {
            UnitRecord probe;
            probe.serial = 9001;
            probe.type = 64;
            probe.x = 96;
            probe.y = 96;
            probe.owner = 1;
            probe.hpPercent = 100;
            probe.shieldPercent = 100;
            probe.energyPercent = 100;
            doc.mutableUnits().push_back(probe);

            HWND dlg = open(172);
            bool ok = dlg != nullptr;
            ok = ok && enabled(dlg, IDC_D172_1078) && enabled(dlg, IDC_D172_1076);
            if (dlg) {
                ::SetDlgItemInt(dlg, IDC_D172_1078, 55, FALSE);
                ::SendMessageW(dlg, WM_COMMAND,
                               MAKEWPARAM(IDC_D172_1078, EN_KILLFOCUS),
                               reinterpret_cast<LPARAM>(::GetDlgItem(dlg, IDC_D172_1078)));
                press(dlg, IDC_D172_1076);   // 隐形
                const UnitRecord& back = doc.units().back();
                ok = ok && back.hpPercent == 55 && (back.queueCount & 0x0001) != 0 &&
                     (back.validState & 0x0001) != 0;
                ::DestroyWindow(dlg);
            }
            record(L"unitNames172Edit", ok);
        }

        const bool interactionWritten =
            WriteTempTextFile(L"scmdplus_dialog_interaction.txt", interactionText);
        report += L"interactionPairs=" + std::to_wstring(interactionPairs) + L"\n";
        record(L"interactionSnapshot",
               interactionWritten && interactionPairs >= 20 && interactionOk);

        // 可选：交互快照基准保存 / 对比
        const std::wstring interactionSave = ParseValueArg(L"--dialog-interaction-baseline=");
        if (!interactionSave.empty()) {
            record(L"interactionBaselineSave",
                   WriteTextFileAt(interactionSave, interactionText));
        }
        const std::wstring interactionDiff = ParseValueArg(L"--dialog-interaction-diff=");
        if (!interactionDiff.empty()) {
            std::wstring interactionBaseline;
            if (!ReadTextFileAt(interactionDiff, interactionBaseline)) {
                report += L"interactionError=missing-baseline\n";
                record(L"interactionMatch", false);
            } else if (interactionBaseline == interactionText) {
                report += L"interactionDiffLines=0\n";
                record(L"interactionMatch", true);
            } else {
                int diffLines = 0;
                const std::wstring diff =
                    BuildSnapshotDiff(interactionBaseline, interactionText, diffLines);
                WriteTempTextFile(L"scmdplus_dialog_interaction.diff.txt", diff);
                report += L"interactionDiffLines=" + std::to_wstring(diffLines) + L"\n";
                record(L"interactionMatch", false);
            }
        }

        SetDialogDocument(nullptr);
        doc.Close();
    }

    const bool ok = allOk;
    report += L"audit=";
    report += ok ? L"1\n" : L"0\n";
    WriteDiag(report);
    return ok ? 0 : 2;
}

void WriteDiag(const std::wstring& text) {
    WriteTempTextFile(L"scmdplus_mpq.txt", text);
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
        const bool exists = mpq.IsOpen() && mpq.FileExists(entry);
        wchar_t failMsg[320];
        ::wsprintfW(failMsg, L"extract=FAIL read\nexists=%d\nerr=%ls\n", exists ? 1 : 0,
                    mpq.LastError());
        WriteDiag(failMsg);
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
            if (!info.sectorTypes.empty()) {
                std::wstring types = L"types=";
                for (std::size_t i = 0; i < info.sectorTypes.size() && i < 12; i++) {
                    wchar_t one[16];
                    ::wsprintfW(one, L"%s%02X", i == 0 ? L"" : L",",
                                info.sectorTypes[i] < 0 ? 0xFF : info.sectorTypes[i]);
                    types += one;
                }
                types += L"\n";
                diag += types;
            }
        }
        wchar_t head[96];
        ::wsprintfW(head, L"head=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    data.size() > 0 ? data[0] : 0, data.size() > 1 ? data[1] : 0,
                    data.size() > 2 ? data[2] : 0, data.size() > 3 ? data[3] : 0,
                    data.size() > 4 ? data[4] : 0, data.size() > 5 ? data[5] : 0,
                    data.size() > 6 ? data[6] : 0, data.size() > 7 ? data[7] : 0);
        diag += head;
        wchar_t sizeMsg[96];
        ::wsprintfW(sizeMsg, L"explode=lit%d/dict%d\n", mpq::LastExplodeLiteralMode(),
                    mpq::LastExplodeDictBits());
        diag += sizeMsg;
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

    // Iscript / GRP 解析回归（无窗口）
    if (const std::wstring iscript = ParseIscriptTestArg(); !iscript.empty()) {
        return RunIscriptTest(iscript, ParseGrpArg());
    }

    // GRP 帧解码回归（无窗口）
    if (const std::wstring grpTest = ParseGrpTestArg(); !grpTest.empty()) {
        return RunGrpTest(grpTest, ParsePalArg(), ParseGrpBmpArg());
    }

    // units.dat 属性表回归（无窗口）
    if (HasSwitchArg(L"--unit-data-test")) {
        return RunUnitDataTest();
    }

    ::InitCommonControls();
    // 187 号对话框含 RichEdit20A 控件，必须先让 riched20 注册窗口类
    if (!::LoadLibraryW(L"riched20.dll")) {
        Log(L"[app] riched20.dll 加载失败，富文本控件不可用");
    }
    // 无头回归/工具模式：不弹开图兼容性提示（163），避免自动化被模态框阻塞
    mainWindow_.SetSuppressDialogs(IsHeadlessRun());
    if (!mainWindow_.Create(hInstance)) {        ::MessageBoxW(nullptr, L"创建主窗口失败。", L"SCMD Plus 3.0", MB_ICONERROR);
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
    mainWindow_.LoadBrushFiles(AppSettings::Instance().brushFiles());

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

    // 装饰物窗口放置/删除回归（无头）
    if (const std::wstring doodadOut = ParseDoodadTestArg(); !doodadOut.empty()) {
        const int rc = RunDoodadTest(mainWindow_, doodadOut);
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 地点描绘模式 / SP 对齐方格回归（无头）
    if (const std::wstring locDrawOut = ParseLocationDrawTestArg(); !locDrawOut.empty()) {
        const int rc = RunLocationDrawTest(mainWindow_, locDrawOut);
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
            // 该回归依赖确定的初始勾选状态：切到临时目录并重置为默认视图，
            // 避免用户的 settings.ini（层/显示选项）导致断言随环境漂移
            wchar_t tempPath[MAX_PATH] = L"";
            ::GetTempPathW(MAX_PATH, tempPath);
            AppSettings& settings = AppSettings::Instance();
            settings.SetDirectory(std::wstring(tempPath) + L"scmdplus_menu_cmd_test");
            settings.mutableView() = ViewState{};
            settings.SetPersistViewOnExit(false);
            mainWindow_.ReloadViewState();
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

    // 全对话框控件状态 / 按钮路径回归（无头）
    if (HasSwitchArg(L"--dialog-audit-test")) {
        const int rc = RunDialogAuditTest(mainWindow_.hwnd());
        ::DestroyWindow(mainWindow_.hwnd());
        return rc;
    }

    // 触发编辑器真实模态路径（DialogBoxParamW + 满载数据文档）回归（无头）
    if (HasSwitchArg(L"--trigger-modal-test") ||
        !ParseValueArg(L"--trigger-modal-test=").empty()) {
        const int rc = RunTriggerModalTest(mainWindow_.hwnd());
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
        if (MainWindow::TranslateAccelForMessage(msg)) continue;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    Log(L"[app] exit");
    return static_cast<int>(msg.wParam);
}

}  // namespace scmd
