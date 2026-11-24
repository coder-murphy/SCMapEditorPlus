#include "units/stat_txt.h"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <fstream>

#include "app/app_settings.h"
#include "archive/mpq_archive.h"

namespace scmd {

namespace {

constexpr std::size_t kMaxEntries = 4096;

u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }

bool ReadWholeFile(const std::wstring& path, Buffer& out) {
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > (32ll << 20)) return false;
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

// TBL 字符串为单字节；英文游戏用 CP1252，中文汉化版可能用 936。
// 先按 1252 解码，出现不可打印字符比例较高时改按 936 重解。
std::wstring DecodeTblString(const char* text) {
    auto decode = [text](UINT cp) {
        const int need = ::MultiByteToWideChar(cp, 0, text, -1, nullptr, 0);
        if (need <= 1) return std::wstring();
        std::wstring out(static_cast<std::size_t>(need - 1), L'\0');
        ::MultiByteToWideChar(cp, 0, text, -1, out.data(), need);
        return out;
    };
    std::wstring wide = decode(1252);
    int bad = 0;
    for (wchar_t ch : wide) {
        if (ch < 0x20 || (ch >= 0x7F && ch < 0xA0)) bad++;
    }
    if (!wide.empty() && bad * 5 > static_cast<int>(wide.size())) {
        const std::wstring gbk = decode(936);
        if (!gbk.empty()) return gbk;
    }
    return wide;
}

bool LoadFromMpq(const std::wstring& mpqPath, StatTextTable& out, std::wstring& source) {
    if (::GetFileAttributesW(mpqPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    MpqArchive mpq;
    Buffer data;
    if (!mpq.Open(mpqPath)) return false;
    // 正式版在 rez\stat_txt.tbl，部分版本放在 arr\ 下
    const char* entries[] = {"rez\\stat_txt.tbl", "arr\\stat_txt.tbl"};
    for (const char* entry : entries) {
        if (!mpq.ReadFile(entry, data)) continue;
        if (!out.Parse(data)) continue;
        source = mpqPath + L"  (" + std::wstring(entry, entry + std::strlen(entry)) + L")";
        return true;
    }
    return false;
}

}  // namespace

bool StatTextTable::Parse(const Buffer& data) {
    loaded_ = false;
    names_.clear();
    if (data.size() < 2) return false;
    const std::size_t count = ReadU16(data.data());
    if (count == 0 || count > kMaxEntries || data.size() < 2 + count * 2) return false;
    names_.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        const std::size_t offset = ReadU16(data.data() + 2 + i * 2);
        if (offset == 0 || offset >= data.size()) {
            names_.emplace_back();
            continue;
        }
        const char* text = reinterpret_cast<const char*>(data.data() + offset);
        std::size_t maxLen = data.size() - offset;
        std::size_t len = 0;
        while (len + 1 < maxLen && text[len] != 0) len++;
        names_.push_back(DecodeTblString(std::string(text, len).c_str()));
    }
    loaded_ = true;
    return true;
}

const std::wstring& StatTextTable::Name(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= names_.size()) return empty_;
    return names_[static_cast<std::size_t>(index)];
}

bool StatTextTable::LoadAuto(StatTextTable& out, std::wstring& source) {
    std::vector<std::wstring> dirs;
    const std::wstring gameDir = AppSettings::Instance().gameDir();
    if (!gameDir.empty()) dirs.push_back(gameDir);
    dirs.push_back(ExeDir());
    dirs.push_back(ExeDir() + L"\\..\\..\\..");

    for (const std::wstring& dir : dirs) {
        const std::wstring files[] = {dir + L"\\rez\\stat_txt.tbl", dir + L"\\arr\\stat_txt.tbl",
                                      dir + L"\\ts\\stat_txt.tbl",
                                      dir + L"\\stat_txt.tbl"};
        for (const std::wstring& file : files) {
            if (::GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            Buffer data;
            if (!ReadWholeFile(file, data)) continue;
            if (out.Parse(data)) {
                source = file;
                return true;
            }
        }
    }
    for (const std::wstring& dir : dirs) {
        const std::wstring mpqs[] = {dir + L"\\mpq\\BrooDat.mpq",
                                     dir + L"\\mpq\\StarDat.mpq",
                                     dir + L"\\BrooDat.mpq",
                                     dir + L"\\StarDat.mpq"};
        for (const std::wstring& mpq : mpqs) {
            if (LoadFromMpq(mpq, out, source)) return true;
        }
    }
    return false;
}

}  // namespace scmd
