#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "api/scmd_core.h"

namespace {

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n,
                          nullptr, nullptr);
    return out;
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const std::size_t at = dir.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : dir.substr(0, at);
}

// 与 GUI 相同的查找顺序：exe\ts，然后 exe\..\..\..\ts
std::wstring DefaultTilesetDir() {
    const std::wstring dir = ExeDir();
    if (dir.empty()) return std::wstring();

    const std::wstring local = dir + L"\\ts";
    if (::GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) return local;

    const std::wstring root = dir + L"\\..\\..\\..\\ts";
    wchar_t full[MAX_PATH] = L"";
    if (::GetFullPathNameW(root.c_str(), MAX_PATH, full, nullptr) > 0 &&
        ::GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
        return full;
    }
    return std::wstring();
}

void Usage() {
    wprintf(L"SCMD Core CLI (ABI %d)\n", scmd_abi_version());
    wprintf(L"用法:\n");
    wprintf(L"  scmd_cli info <map.scx> [--ts <dir>]\n");
    wprintf(L"  scmd_cli render <map.scx> <out.bmp> [--scale 2|4|8|16|32] [--ts <dir>]\n");
    wprintf(L"  scmd_cli new <out.scx> <width> <height> [--era 0..7] [--ts <dir>]\n");
    wprintf(L"  scmd_cli extract-ts <game-dir> <out-dir>\n");
    wprintf(L"\n未指定 --ts 时依次查找 <exe>\\ts 与 <exe>\\..\\..\\..\\ts。\n");
}

int Fail(const wchar_t* action) {
    std::wstring err = ToWide(scmd_last_error());
    if (err.empty()) err = L"未知错误";
    wprintf(L"%ls失败: %ls\n", action, err.c_str());
    return 2;
}

struct Options {
    std::wstring tileset;
    int scale = 8;
    int era = 0;
    std::vector<std::wstring> positional;
};

bool ParseArgs(int argc, wchar_t** argv, Options& opt) {
    for (int i = 2; i < argc; i++) {
        const std::wstring arg = argv[i];
        if (arg == L"--ts" && i + 1 < argc) {
            opt.tileset = argv[++i];
        } else if (arg.rfind(L"--ts=", 0) == 0) {
            opt.tileset = arg.substr(5);
        } else if (arg == L"--scale" && i + 1 < argc) {
            opt.scale = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        } else if (arg.rfind(L"--scale=", 0) == 0) {
            opt.scale = static_cast<int>(std::wcstol(arg.c_str() + 8, nullptr, 10));
        } else if (arg == L"--era" && i + 1 < argc) {
            opt.era = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        } else if (arg.rfind(L"--era=", 0) == 0) {
            opt.era = static_cast<int>(std::wcstol(arg.c_str() + 6, nullptr, 10));
        } else if (arg.rfind(L"--", 0) == 0) {
            wprintf(L"未知选项: %ls\n", arg.c_str());
            return false;
        } else {
            opt.positional.push_back(arg);
        }
    }
    return true;
}

ScmdMap* OpenMap(const std::wstring& path, const std::wstring& tsDir) {
    const std::string pathUtf8 = ToUtf8(path);
    const std::string tsUtf8 = ToUtf8(tsDir);
    return scmd_map_open(pathUtf8.c_str(), tsUtf8.empty() ? nullptr : tsUtf8.c_str());
}

int CmdInfo(const Options& opt) {
    if (opt.positional.size() != 1) {
        Usage();
        return 1;
    }

    const std::wstring tsDir = opt.tileset.empty() ? DefaultTilesetDir() : opt.tileset;
    ScmdMap* map = OpenMap(opt.positional[0], tsDir);
    if (!map) return Fail(L"打开地图");

    ScmdMapInfo info = {};
    if (!scmd_map_info(map, &info)) {
        scmd_map_free(map);
        return Fail(L"读取地图信息");
    }

    wprintf(L"map: %ls\n", opt.positional[0].c_str());
    wprintf(L"size: %dx%d\n", info.width, info.height);
    wprintf(L"era: %d (%ls)\n", info.era, ToWide(scmd_era_name(info.era)).c_str());
    wprintf(L"units: %d\n", info.unit_count);
    wprintf(L"tileset: %ls\n", info.has_tileset ? L"yes" : L"no");
    scmd_map_free(map);
    return 0;
}

int CmdRender(const Options& opt) {
    if (opt.positional.size() != 2) {
        Usage();
        return 1;
    }

    const std::wstring tsDir = opt.tileset.empty() ? DefaultTilesetDir() : opt.tileset;
    ScmdMap* map = OpenMap(opt.positional[0], tsDir);
    if (!map) return Fail(L"打开地图");

    ScmdMapInfo info = {};
    if (!scmd_map_info(map, &info)) {
        scmd_map_free(map);
        return Fail(L"读取地图信息");
    }

    const std::string outUtf8 = ToUtf8(opt.positional[1]);
    if (!scmd_map_render_bmp(map, outUtf8.c_str(), opt.scale)) {
        scmd_map_free(map);
        return Fail(L"渲染");
    }

    wprintf(L"bmp: %ls (%dx%d, %d px/tile)\n", opt.positional[1].c_str(),
            info.width * opt.scale, info.height * opt.scale, opt.scale);
    scmd_map_free(map);
    return 0;
}

int CmdNew(const Options& opt) {
    if (opt.positional.size() != 3) {
        Usage();
        return 1;
    }

    const int width = static_cast<int>(std::wcstol(opt.positional[1].c_str(), nullptr, 10));
    const int height = static_cast<int>(std::wcstol(opt.positional[2].c_str(), nullptr, 10));
    const std::wstring tsDir = opt.tileset.empty() ? DefaultTilesetDir() : opt.tileset;

    const std::string tsUtf8 = ToUtf8(tsDir);
    ScmdMap* map = scmd_map_create(width, height, opt.era, tsUtf8.empty() ? nullptr : tsUtf8.c_str());
    if (!map) return Fail(L"新建地图");

    const std::string outUtf8 = ToUtf8(opt.positional[0]);
    if (!scmd_map_save(map, outUtf8.c_str())) {
        scmd_map_free(map);
        return Fail(L"保存地图");
    }

    wprintf(L"created: %ls (%dx%d, era %d)\n", opt.positional[0].c_str(), width, height, opt.era);
    scmd_map_free(map);
    return 0;
}

int CmdExtractTilesets(const Options& opt) {
    if (opt.positional.size() != 2) {
        Usage();
        return 1;
    }

    const std::string gameDir = ToUtf8(opt.positional[0]);
    const std::string outDir = ToUtf8(opt.positional[1]);
    ScmdExtractResult result = {};
    if (!scmd_extract_tilesets(gameDir.c_str(), outDir.c_str(), &result)) {
        Fail(L"导出地形集");
        wprintf(L"files=%d failed=%d bytes=%llu\n", result.files, result.failed,
                static_cast<unsigned long long>(result.bytes));
        return 2;
    }

    wprintf(L"extract: files=%d failed=%d bytes=%llu\n", result.files, result.failed,
            static_cast<unsigned long long>(result.bytes));
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        Usage();
        return 1;
    }

    const std::wstring command = argv[1];
    if (command == L"-h" || command == L"--help" || command == L"help") {
        Usage();
        return 0;
    }

    Options opt;
    if (!ParseArgs(argc, argv, opt)) return 1;

    if (command == L"info") return CmdInfo(opt);
    if (command == L"render") return CmdRender(opt);
    if (command == L"new") return CmdNew(opt);
    if (command == L"extract-ts") return CmdExtractTilesets(opt);

    wprintf(L"未知命令: %ls\n", command.c_str());
    Usage();
    return 1;
}
