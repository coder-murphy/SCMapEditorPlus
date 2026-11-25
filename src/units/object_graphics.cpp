#include "units/object_graphics.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <fstream>

#include "app/app_settings.h"
#include "archive/mpq_archive.h"

namespace scmd {

namespace {

constexpr int kUnitCount = 228;
constexpr int kFlingyCount = 209;
constexpr int kSpriteCount = 517;
constexpr int kImageCount = 999;

u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

bool ReadWholeFile(const std::wstring& path, Buffer& out) {
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > (16ll << 20)) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    return in.good() || in.eof();
}

std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (slash) *slash = L'\0';
    return path;
}

struct TableFile {
    const wchar_t* name;   // 相对路径（解包文件）
    const char* entry;     // MPQ 条目名
    Buffer data;
    bool found = false;
};

bool ReadFromMpq(const std::wstring& mpqPath, TableFile& file) {
    if (::GetFileAttributesW(mpqPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    MpqArchive mpq;
    if (!mpq.Open(mpqPath)) return false;
    return mpq.ReadFile(file.entry, file.data);
}

}  // namespace

bool ObjectGraphics::LoadAuto() {
    loaded_ = false;
    unitFlingy_.clear();
    flingySprite_.clear();
    spriteImage_.clear();
    imageGrpFile_.clear();
    grpNames_.clear();

    std::vector<std::wstring> dirs;
    const std::wstring gameDir = AppSettings::Instance().gameDir();
    if (!gameDir.empty()) dirs.push_back(gameDir);
    const std::wstring exeDir = ExeDir();
    dirs.push_back(exeDir);
    dirs.push_back(exeDir + L"\\..");
    dirs.push_back(exeDir + L"\\..\\..\\..");

    TableFile files[] = {
        {L"arr\\units.dat", "arr\\units.dat"},
        {L"arr\\flingy.dat", "arr\\flingy.dat"},
        {L"arr\\sprites.dat", "arr\\sprites.dat"},
        {L"arr\\images.dat", "arr\\images.dat"},
        {L"arr\\images.tbl", "arr\\images.tbl"},
    };

    for (const std::wstring& dir : dirs) {
        for (TableFile& file : files) {
            if (file.found) continue;
            if (ReadWholeFile(dir + L"\\" + file.name, file.data)) file.found = true;
        }
    }
    if (!files[0].found || !files[1].found || !files[2].found || !files[3].found ||
        !files[4].found) {
        for (const std::wstring& dir : dirs) {
            const std::wstring mpqs[] = {dir + L"\\mpq\\BrooDat.mpq", dir + L"\\BrooDat.mpq",
                                         dir + L"\\mpq\\StarDat.mpq", dir + L"\\StarDat.mpq"};
            for (const std::wstring& mpq : mpqs) {
                bool all = true;
                for (TableFile& file : files) {
                    if (file.found || ReadFromMpq(mpq, file)) {
                        file.found = true;
                    } else {
                        all = false;
                    }
                }
                if (all) break;
            }
        }
    }
    if (!files[0].found || !files[1].found || !files[2].found || !files[3].found ||
        !files[4].found) {
        return false;
    }

    const Buffer& units = files[0].data;
    if (units.size() < static_cast<std::size_t>(kUnitCount)) return false;
    unitFlingy_.assign(units.begin(), units.begin() + kUnitCount);

    const Buffer& flingy = files[1].data;
    if (flingy.size() < static_cast<std::size_t>(kFlingyCount) * 2) return false;
    flingySprite_.resize(kFlingyCount);
    for (int i = 0; i < kFlingyCount; i++) {
        flingySprite_[static_cast<std::size_t>(i)] = ReadU16(flingy.data() + i * 2);
    }

    const Buffer& sprites = files[2].data;
    if (sprites.size() < static_cast<std::size_t>(kSpriteCount) * 2) return false;
    spriteImage_.resize(kSpriteCount);
    for (int i = 0; i < kSpriteCount; i++) {
        spriteImage_[static_cast<std::size_t>(i)] = ReadU16(sprites.data() + i * 2);
    }

    const Buffer& images = files[3].data;
    if (images.size() < static_cast<std::size_t>(kImageCount) * 4) return false;
    imageGrpFile_.resize(kImageCount);
    for (int i = 0; i < kImageCount; i++) {
        imageGrpFile_[static_cast<std::size_t>(i)] = ReadU32(images.data() + i * 4);
    }

    const Buffer& tbl = files[4].data;
    if (tbl.size() < 2) return false;
    const int count = ReadU16(tbl.data());
    if (count <= 0 || tbl.size() < 2 + static_cast<std::size_t>(count) * 2) return false;
    grpNames_.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; i++) {
        const std::size_t offset = ReadU16(tbl.data() + 2 + static_cast<std::size_t>(i) * 2);
        if (offset == 0 || offset >= tbl.size()) continue;
        const char* text = reinterpret_cast<const char*>(tbl.data() + offset);
        std::size_t len = 0;
        while (offset + len < tbl.size() && text[len] != 0) len++;
        std::wstring name;
        name.reserve(len);
        for (std::size_t k = 0; k < len; k++) name.push_back(static_cast<wchar_t>(text[k]));
        grpNames_[static_cast<std::size_t>(i)] = name;
    }

    loaded_ = true;
    return true;
}

std::wstring ObjectGraphics::UnitGrpPath(int unitType) const {
    if (!loaded_ || unitType < 0 || unitType >= static_cast<int>(unitFlingy_.size())) {
        return std::wstring();
    }
    const int flingy = unitFlingy_[static_cast<std::size_t>(unitType)];
    if (flingy < 0 || flingy >= static_cast<int>(flingySprite_.size())) return std::wstring();
    return SpriteGrpPath(flingySprite_[static_cast<std::size_t>(flingy)]);
}

std::wstring ObjectGraphics::SpriteGrpPath(int spriteId) const {
    if (!loaded_ || spriteId < 0 || spriteId >= static_cast<int>(spriteImage_.size())) {
        return std::wstring();
    }
    const int image = spriteImage_[static_cast<std::size_t>(spriteId)];
    if (image < 0 || image >= static_cast<int>(imageGrpFile_.size())) return std::wstring();
    const u32 file = imageGrpFile_[static_cast<std::size_t>(image)];
    if (file == 0 || file > grpNames_.size()) return std::wstring();
    return grpNames_[file - 1];
}

}  // namespace scmd
