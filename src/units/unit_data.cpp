#include "units/unit_data.h"

#include <windows.h>

#include <cstddef>
#include <fstream>

#include "app/app_settings.h"
#include "archive/mpq_archive.h"

namespace scmd {

namespace {

constexpr int kUnitCount = 228;
constexpr std::size_t kUnitDataSize = 19876;

u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

bool ReadWholeFile(const std::wstring& path, Buffer& out) {
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > (64ll << 20)) return false;
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

bool LoadFromMpq(const std::wstring& mpqPath, UnitDataTable& out, std::wstring& source) {
    if (::GetFileAttributesW(mpqPath.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    MpqArchive mpq;
    Buffer data;
    if (!mpq.Open(mpqPath) || !mpq.ReadFile("arr\\units.dat", data)) return false;
    if (!out.Parse(data)) return false;
    source = mpqPath + L"  (arr\\units.dat)";
    return true;
}

}  // namespace

bool UnitDataTable::Load(const std::wstring& path) {
    Buffer data;
    if (!ReadWholeFile(path, data)) return false;
    return Parse(data);
}

bool UnitDataTable::Parse(const Buffer& data) {
    loaded_ = false;
    units_.clear();
    if (data.size() < kUnitDataSize) return false;
    units_.resize(kUnitCount);
    for (int i = 0; i < kUnitCount; i++) {
        UnitData u;
        u.id = i;
        u.graphic = data[0x0000 + static_cast<std::size_t>(i)];
        u.shieldEnabled = data[0x09A8 + static_cast<std::size_t>(i)] != 0;
        u.shields = u.shieldEnabled
                        ? ReadU16(data.data() + 0x0A8C + static_cast<std::size_t>(i) * 2)
                        : 0;
        u.hp = static_cast<int>((ReadU32(data.data() + 0x0C54 +
                                         static_cast<std::size_t>(i) * 4) &
                                 0xFFFFFFu) /
                                256);
        u.sight = data[0x1FEC + static_cast<std::size_t>(i)];
        u.size = data[0x21B4 + static_cast<std::size_t>(i)];
        u.armor = data[0x2298 + static_cast<std::size_t>(i)];
        u.width = ReadU16(data.data() + 0x3124 + static_cast<std::size_t>(i) * 2);
        u.height = ReadU16(data.data() + 0x32EC + static_cast<std::size_t>(i) * 2);
        u.circleWidth = ReadU16(data.data() + 0x34B4 + static_cast<std::size_t>(i) * 2);
        u.circleHeight = ReadU16(data.data() + 0x367C + static_cast<std::size_t>(i) * 2);
        u.portrait = ReadU16(data.data() + 0x3844 + static_cast<std::size_t>(i) * 2);
        u.minerals = ReadU16(data.data() + 0x3A0C + static_cast<std::size_t>(i) * 2);
        u.gas = ReadU16(data.data() + 0x3BD4 + static_cast<std::size_t>(i) * 2);
        u.buildTime = ReadU16(data.data() + 0x3D9C + static_cast<std::size_t>(i) * 2);
        u.movementType = data[0x10C8 + static_cast<std::size_t>(i)];
        u.weaponGround = data[0x1704 + static_cast<std::size_t>(i)];
        u.weaponAir = data[0x18CC + static_cast<std::size_t>(i)];
        u.specialFlags = ReadU32(data.data() + 0x1B78 + static_cast<std::size_t>(i) * 4);
        u.broodWar = data[0x4AF8 + static_cast<std::size_t>(i)] != 0;
        units_[static_cast<std::size_t>(i)] = u;
    }
    loaded_ = true;
    return true;
}

const UnitData* UnitDataTable::Get(int id) const {
    if (id < 0 || id >= static_cast<int>(units_.size())) return nullptr;
    return &units_[static_cast<std::size_t>(id)];
}

bool UnitDataTable::LoadAuto(UnitDataTable& out, std::wstring& source) {
    std::vector<std::wstring> dirs;
    const std::wstring gameDir = AppSettings::Instance().gameDir();
    if (!gameDir.empty()) dirs.push_back(gameDir);
    dirs.push_back(ExeDir());
    dirs.push_back(ExeDir() + L"\\..\\..\\..");

    for (const std::wstring& dir : dirs) {
        const std::wstring files[] = {dir + L"\\arr\\units.dat", dir + L"\\units.dat"};
        for (const std::wstring& file : files) {
            if (::GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (out.Load(file)) {
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
