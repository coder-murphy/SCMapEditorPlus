#include "terrain/tileset_export.h"

#include <shlobj.h>

#include <cwchar>
#include <fstream>
#include <string>

#include "archive/mpq_archive.h"
#include "common/log.h"

namespace scmd {

namespace {

// 输出文件名与 MapDocument::kEraPrefix 对齐
struct TsSource {
    const wchar_t* archive;
    const wchar_t* prefix;
    const char* name;      // MPQ 内部名（ASCII）
    const wchar_t* wname;  // 输出文件名
};

const TsSource kTsSources[] = {
    {L"StarDat.mpq", L"StarDat.mpq_", "badlands", L"badlands"},
    {L"StarDat.mpq", L"StarDat.mpq_", "platform", L"platform"},
    {L"StarDat.mpq", L"StarDat.mpq_", "install", L"install"},
    {L"StarDat.mpq", L"StarDat.mpq_", "ashworld", L"ashworld"},
    {L"StarDat.mpq", L"StarDat.mpq_", "jungle", L"jungle"},
    {L"BrooDat.mpq", L"BrooDat.mpq_", "desert", L"desert"},
    {L"BrooDat.mpq", L"BrooDat.mpq_", "ice", L"ice"},
    {L"BrooDat.mpq", L"BrooDat.mpq_", "twilight", L"twilight"},
};

const wchar_t* kTsExts[] = {L"cv5", L"vx4", L"vr4", L"wpe"};

bool WriteWholeFile(const std::wstring& path, const Buffer& data) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return out.good();
}

}  // namespace

TilesetExportResult ExtractTilesets(const std::wstring& gameDir, const std::wstring& outDir) {
    TilesetExportResult result;
    if (gameDir.empty() || outDir.empty()) {
        result.failed = 1;
        result.log = L"missing gameDir / outDir\n";
        return result;
    }

    const int dirRc = ::SHCreateDirectoryExW(nullptr, outDir.c_str(), nullptr);
    if (dirRc != ERROR_SUCCESS && dirRc != ERROR_ALREADY_EXISTS && dirRc != ERROR_FILE_EXISTS) {
        result.failed = 1;
        result.log = L"mkdir=" + std::to_wstring(dirRc) + L" dir=" + outDir + L"\n";
        return result;
    }

    for (int pass = 0; pass < 2; pass++) {
        const wchar_t* archive = pass == 0 ? L"StarDat.mpq" : L"BrooDat.mpq";
        MpqArchive mpq;
        if (!mpq.Open(gameDir + L"\\" + archive)) {
            result.log += L"archive=FAIL ";
            result.log += archive;
            result.log += L"\n";
            continue;
        }

        for (const TsSource& src : kTsSources) {
            if (std::wcscmp(src.archive, archive) != 0) continue;
            for (const wchar_t* ext : kTsExts) {
                std::string entry = "tileset\\";
                entry += src.name;
                entry += '.';
                for (const wchar_t* p = ext; *p; ++p) entry += static_cast<char>(*p);

                Buffer data;
                if (!mpq.ReadFile(entry, data)) {
                    result.failed++;
                    result.log += L"read=FAIL ";
                    result.log += src.wname;
                    result.log += L".";
                    result.log += ext;
                    result.log += L" (";
                    result.log += mpq.LastError();
                    result.log += L")\n";
                    continue;
                }

                const std::wstring path = outDir + L"\\" + src.prefix + src.wname + L"." + ext;
                if (!WriteWholeFile(path, data)) {
                    result.failed++;
                    result.log += L"write=FAIL ";
                    result.log += path;
                    result.log += L"\n";
                    continue;
                }
                result.files++;
                result.bytes += data.size();
            }
        }
    }

    Log(L"[ts] extract files=%d failed=%d bytes=%u", result.files, result.failed,
        static_cast<unsigned>(result.bytes));
    return result;
}

}  // namespace scmd
