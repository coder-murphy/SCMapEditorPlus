#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/app_settings.h"
#include "app/backups.h"
#include "archive/mpq_archive.h"
#include "archive/mpq_codec.h"
#include "chk/chk_section.h"
#include "common/log.h"
#include "common/string_util.h"
#include "map/edit_history.h"
#include "map/map_document.h"
#include "map/map_switches.h"
#include "terrain/edge_placer.h"
#include "terrain/terrain_renderer.h"
#include "terrain/tileset_export.h"
#include "tests/headless_tests.h"
#include "units/iscript.h"
#include "units/stat_txt.h"
#include "units/unit_data.h"

namespace scmd {

namespace {

// --ts=<目录>：地形集四件套所在目录（与 GUI 版保持一致）
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

void WriteDiag(const std::wstring& text) {
    WriteTempTextFile(L"scmdplus_mpq.txt", text);
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

}  // namespace

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
    const std::size_t baseSprites = doc.sprites().Count();   // 样本地图已有的 THG2 条数

    auto& items = doc.mutableSprites().MutableItems();
    items.push_back({100, 4 * 32, 4 * 32, 0, 0, 0x1000});
    items.push_back({101, 6 * 32, 4 * 32, 1, 0, 0x0000});
    items.push_back({102, 8 * 32, 5 * 32, 2, 0, 0x1000});
    doc.MarkDirty();

    // 模拟 164：玩家 / 未用字节 / Flag 位（bit0 + 普通SP + Flipped + 禁用）
    const Buffer spritesBefore = doc.sprites().Serialize();
    const Buffer doodadsNow = doc.doodads().Serialize();
    const Buffer fogNow = doc.fog().Serialize();
    SpriteRecord& s0 = doc.mutableSprites().MutableItems()[baseSprites];
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
    cmd.hasSprites = true;
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
    const bool fieldsOk = reopened && check.sprites().Count() == baseSprites + 3 &&
                          check.sprites().Items()[baseSprites].type == 100 &&
                          check.sprites().Items()[baseSprites].x == 4 * 32 &&
                          check.sprites().Items()[baseSprites].owner == 2 &&
                          check.sprites().Items()[baseSprites].unused == 0x7F &&
                          check.sprites().Items()[baseSprites].flags == 0xB001u &&
                          check.sprites().Items()[baseSprites + 1].owner == 1 &&
                          check.sprites().Items()[baseSprites + 2].DrawAsSprite() &&
                          sectionOf(check, "THG2").size() == (baseSprites + 3) * 10;
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
    dcmd.hasSprites = true;
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
void WriteHeadlessDiag(const std::wstring& text) {
    WriteDiag(text);
}

}  // namespace scmd
