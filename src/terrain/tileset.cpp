#include "terrain/tileset.h"

#include <cstring>
#include <fstream>
#include <random>

#include "common/log.h"

namespace scmd {

namespace {

bool ReadWholeFile(const std::wstring& path, Buffer& out) {
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out.data()), size);
    return true;
}

u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }

}  // namespace

bool Tileset::Load(const std::wstring& dir, const std::wstring& prefix) {
    Unload();
    const std::wstring base = dir.empty() ? prefix : dir + L"\\" + prefix;
    if (!ReadWholeFile(base + L".cv5", cv5_)) { Log(L"[ts] 缺少 %s.cv5", base.c_str()); Unload(); return false; }
    if (!ReadWholeFile(base + L".vx4", vx4_)) { Log(L"[ts] 缺少 %s.vx4", base.c_str()); Unload(); return false; }
    if (!ReadWholeFile(base + L".vr4", vr4_)) { Log(L"[ts] 缺少 %s.vr4", base.c_str()); Unload(); return false; }
    if (!ReadWholeFile(base + L".wpe", wpe_)) { Log(L"[ts] 缺少 %s.wpe", base.c_str()); Unload(); return false; }

    // 找 creep 图案组：type=0 且 flags 带 0x0040，至少一个有效 megatile
    creepGroup_ = -1;
    for (int g = 0; g < GroupCount(); g++) {
        const std::size_t at = static_cast<std::size_t>(g) * 52;
        if (ReadU16(cv5_.data() + at) != 0) continue;
        if ((ReadU16(cv5_.data() + at + 2) & 0x0040) == 0) continue;
        bool any = false;
        for (int i = 0; i < 16 && !any; i++) {
            if (ReadU16(cv5_.data() + at + 20 + i * 2) != 0) any = true;
        }
        if (any) {
            creepGroup_ = g;
            break;
        }
    }

    Log(L"[ts] loaded %s: groups=%d megatiles=%d vr=%d creep=%d", prefix.c_str(),
        GroupCount(), MegaCount(), VrCount(), creepGroup_);
    return true;
}

void Tileset::Unload() {
    cv5_.clear();
    vx4_.clear();
    vr4_.clear();
    wpe_.clear();
    creepGroup_ = -1;
}

bool Tileset::CreepTile(int slot, u16& value) const {
    if (creepGroup_ < 0 || slot < 0 || slot >= 16) return false;
    const std::size_t at = static_cast<std::size_t>(creepGroup_) * 52 + 20 +
                           static_cast<std::size_t>(slot) * 2;
    if (at + 1 >= cv5_.size()) return false;
    if (ReadU16(cv5_.data() + at) == 0) return false;      // 空槽不绘制
    value = static_cast<u16>((static_cast<u16>(creepGroup_) << 4) | static_cast<u16>(slot));
    return true;
}

u16 Tileset::MegatileOf(u16 mtxmValue) const {
    const u16 group = static_cast<u16>((mtxmValue & 0x7FF0) >> 4);
    const u16 index = static_cast<u16>(mtxmValue & 0x0F);
    if (group >= GroupCount()) return 0;
    return ReadU16(cv5_.data() + group * 52 + 20 + index * 2);
}

bool Tileset::TileBlock(u16 mtxmValue, u8 out[1024]) const {
    if (!IsLoaded()) return false;
    const u16 mega = MegatileOf(mtxmValue);
    if (mega >= MegaCount()) { std::memset(out, 0, 1024); return false; }
    std::memset(out, 0, 1024);

    const u8* refs = vx4_.data() + static_cast<std::size_t>(mega) * 32;
    for (int k = 0; k < 16; k++) {
        const u16 ref = ReadU16(refs + k * 2);
        const u16 vrid = static_cast<u16>(ref >> 1);
        if (vrid >= VrCount()) continue;
        const u8* src = vr4_.data() + static_cast<std::size_t>(vrid) * 64;
        const int bx = (k % 4) * 8;
        const int by = (k / 4) * 8;
        const bool hflip = (ref & 1) != 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                const u8 px = src[y * 8 + (hflip ? (7 - x) : x)];
                out[(by + y) * 32 + bx + x] = px;
            }
        }
    }
    return true;
}

bool Tileset::IsWalkable(u16 group) const {
    if (group >= GroupCount()) return false;
    return (ReadU16(cv5_.data() + group * 52) & 1u) != 0;
}

u16 Tileset::Type(u16 group) const {
    if (group >= GroupCount()) return 0;
    return static_cast<u16>(ReadU16(cv5_.data() + group * 52) >> 1);
}

bool Tileset::GroupTileIndex(u16 group, u16& type, u16 abuts[4]) const {
    if (group >= GroupCount()) return false;
    const u8* rec = cv5_.data() + static_cast<std::size_t>(group) * 52;
    type = ReadU16(rec);
    for (int i = 0; i < 4; i++) abuts[i] = ReadU16(rec + 4 + i * 2);
    return type != 0;
}

bool Tileset::HasSubtile(u16 group, int index) const {
    if (group >= GroupCount() || index < 0 || index > 15) return false;
    return ReadU16(cv5_.data() + group * 52 + 20 + index * 2) != 0;
}

int Tileset::FirstValidSubtile(u16 group) const {
    for (int i = 0; i < 16; i++) {
        if (HasSubtile(group, static_cast<int>(i))) return i;
    }
    return 0;
}

int Tileset::RandomValidSubtile(u16 group) const {
    // ScmDraft2 先尝试 0..5（rand() & 7 且 < 6），失败后在 6..15 中重试
    static std::mt19937 rng(std::random_device{}());
    int index = static_cast<int>(rng() & 7u);
    if (index < 6 && HasSubtile(group, index)) return index;
    for (int tries = 0; tries < 64; tries++) {
        index = 6 + static_cast<int>(rng() % 10u);
        if (HasSubtile(group, index)) return index;
    }
    return FirstValidSubtile(group);
}

}  // namespace scmd
