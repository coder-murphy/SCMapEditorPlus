#include "app/app.h"

#include <shellapi.h>
#include <cwchar>
#include <cstring>
#include <string>

#include "ui/dialog_registry.h"
#include "ui/dialogs.h"

#include "archive/mpq_archive.h"
#include "map/edit_history.h"
#include "map/map_document.h"
#include "terrain/terrain_renderer.h"
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

    wchar_t msg[512];
    ::wsprintfW(msg,
        L"tileEdit=%d\ntileUndo=%d\ntileRedo=%d\nplaceOk=%d\nplaceUndo=%d\nplaceRedo=%d\n"
        L"removeUndo=%d\nundoCount=%u\nredoCleared=%d\n",
        tileEditOk ? 1 : 0, tileUndoOk ? 1 : 0, tileRedoOk ? 1 : 0,
        placeOk ? 1 : 0, placeUndoOk ? 1 : 0, placeRedoOk ? 1 : 0,
        removeUndoOk ? 1 : 0, static_cast<unsigned>(history.UndoCount()),
        redoCleared ? 1 : 0);
    WriteDiag(msg);

    const bool ok = tileEditOk && tileUndoOk && tileRedoOk && placeOk && placeUndoOk &&
                    placeRedoOk && removeUndoOk;
    return ok ? 0 : 2;
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

    wchar_t msg[384];
    ::wsprintfW(msg, L"trigBlocks=%u\nmrgn=%u\nstr=%u\ntrigEq=%d\nmrgnEq=%d\nstrEq=%d\n",
                static_cast<unsigned>(doc.triggers().BlockCount()),
                static_cast<unsigned>(doc.locations().Count()),
                static_cast<unsigned>(doc.strings().Count()), trig, mrgn, str);
    WriteDiag(msg);

    const bool ok = trig != 0 && mrgn != 0 && str != 0;
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

}  // namespace

App& App::Instance() {
    static App instance;
    return instance;
}

int App::Run(HINSTANCE hInstance) {
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

    // 地形集目录：--ts 优先，否则用 exe 目录下的 ts
    std::wstring tsDir = ParseTsArg();
    if (tsDir.empty()) {
        wchar_t exePath[MAX_PATH] = L"";
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = std::wcsrchr(exePath, L'\\');
        if (slash) *slash = L'\0';
        tsDir = std::wstring(exePath) + L"\\ts";
    }
    mainWindow_.SetTilesetDir(tsDir);

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
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    Log(L"[app] exit");
    return static_cast<int>(msg.wParam);
}

}  // namespace scmd
