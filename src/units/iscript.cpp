#include "units/iscript.h"

#include <cstddef>
#include <cstring>
#include <fstream>

namespace scmd {

namespace {

u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

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

const char* const kAnimationNames[] = {
    "Init",          "Death",        "GndAttkInit",  "AirAttkInit",
    "Unused1",       "GndAttkRpt",   "AirAttkRpt",   "CastSpell",
    "GndAttkToIdle", "AirAttkToIdle", "Unused2",     "Walking",
    "WalkingToIdle", "SpecialState1", "SpecialState2", "AlmostBuilt",
    "Built",         "Landing",      "LiftOff",      "IsWorking",
    "WorkingToIdle", "WarpIn",       "Unused3",      "StarEditInit",
    "Disable",       "Burrow",       "UnBurrow",     "Enable",
};

const char* const kOpcodeNames[] = {
    "playfram",       "playframtile",   "sethorpos",      "setvertpos",
    "setpos",         "wait",           "waitrand",       "goto",
    "imgol",          "imgul",          "imgolorig",      "switchul",
    "__0c",           "imgoluselo",     "imguluselo",     "sprol",
    "highsprol",      "lowsprul",       "uflunstable",    "spruluselo",
    "sprul",          "sproluselo",     "end",            "setflipstate",
    "playsnd",        "playsndrand",    "playsndbtwn",    "domissiledmg",
    "attackmelee",    "followmaingraphic", "randcondjmp", "turnccwise",
    "turncwise",      "turn1cwise",     "turnrand",       "setspawnframe",
    "sigorder",       "attackwith",     "attack",         "castspell",
    "useweapon",      "move",           "gotorepeatattk", "engframe",
    "engset",         "__2d",           "nobrkcodestart", "nobrkcodeend",
    "ignorerest",     "attkshiftproj",  "tmprmgraphicstart", "tmprmgraphicend",
    "setfldirect",    "call",           "return",         "setflspeed",
    "creategasoverlays", "pwrupcondjmp", "trgtrangecondjmp", "trgtarccondjmp",
    "curdirectcondjmp", "imgulnextid",  "__3e",           "liftoffcondjmp",
    "warpoverlay",    "orderdone",      "grdsprol",       "__43",
    "dogrddamage",
};

}  // namespace

bool IscriptFile::Load(const std::wstring& path) {
    Buffer data;
    if (!ReadWholeFile(path, data)) return false;
    return Parse(data);
}

bool IscriptFile::Parse(const Buffer& data) {
    loaded_ = false;
    entries_.clear();
    if (data.size() < 12) return false;
    data_ = data;

    // 索引表：(u16 id, u16 headerOffset)，headerOffset == 0 结束
    std::size_t tableEnd = 0;
    for (std::size_t pos = 0; pos + 4 <= data_.size(); pos += 4) {
        const u16 offset = ReadU16(data_.data() + pos + 2);
        if (offset == 0) {
            tableEnd = pos + 4;
            break;
        }
        if (static_cast<std::size_t>(offset) + 8 > data_.size()) return false;
        IscriptHeader h;
        h.id = ReadU16(data_.data() + pos);
        h.offset = offset;
        entries_.push_back(h);
    }
    if (entries_.empty() || tableEnd == 0) return false;

    for (IscriptHeader& h : entries_) {
        const u8* at = data_.data() + h.offset;
        if (std::memcmp(at, "SCPE", 4) != 0) {
            // 少数条目直接指向共享的偏移表（没有 SCPE/type），稍后按偏移匹配别名
            continue;
        }
        h.type = static_cast<u16>(ReadU32(at + 4) & 0xFFFF);
        const int count = AnimationCount(h.type);
        h.animOffsets.assign(static_cast<std::size_t>(count), 0);
        for (int i = 0; i < count; i++) {
            const std::size_t pos = h.offset + 8 + static_cast<std::size_t>(i) * 2;
            if (pos + 2 > data_.size()) return false;
            const u16 abs = ReadU16(data_.data() + pos);
            h.animOffsets[static_cast<std::size_t>(i)] =
                abs < data_.size() ? abs : 0;
        }
    }

    // 无 SCPE 的条目：前两个 u16 是共享动画偏移，按相同偏移找到同构头并复制
    for (IscriptHeader& h : entries_) {
        if (!h.animOffsets.empty()) continue;
        if (h.offset + 4 > data_.size()) continue;
        const u16 a = ReadU16(data_.data() + h.offset);
        const u16 b = ReadU16(data_.data() + h.offset + 2);
        for (const IscriptHeader& src : entries_) {
            if (src.animOffsets.size() >= 2 && src.animOffsets[0] == a &&
                src.animOffsets[1] == b) {
                h.type = src.type;
                h.animOffsets = src.animOffsets;
                break;
            }
        }
    }

    loaded_ = true;
    return true;
}

const IscriptHeader* IscriptFile::Find(u16 id) const {
    for (const IscriptHeader& h : entries_) {
        if (h.id == id) return &h;
    }
    return nullptr;
}

int IscriptFile::AnimationCount(u16 type) {
    switch (type) {
        case 0: case 1: return 2;
        case 2: return 4;
        case 12: case 13: return 14;
        case 14: case 15: return 16;
        case 20: case 21: return 22;
        case 23: return 24;
        case 24: return 26;
        case 26: case 27: case 28: case 29: return 28;
        default: return 0;
    }
}

const char* IscriptFile::AnimationName(int index) {
    if (index < 0 || index >= static_cast<int>(sizeof(kAnimationNames) /
                                               sizeof(kAnimationNames[0]))) {
        return "?";
    }
    return kAnimationNames[index];
}

const char* IscriptFile::OpcodeName(u8 opcode) {
    if (opcode >= sizeof(kOpcodeNames) / sizeof(kOpcodeNames[0])) return "?";
    return kOpcodeNames[opcode];
}

int IscriptFile::FixedOperandSize(u8 opcode) {
    switch (opcode) {
        case 0x00: case 0x01: case 0x04: case 0x06: case 0x07:
        case 0x0A: case 0x0B: case 0x12: case 0x18:
        case 0x35: case 0x37: case 0x39: case 0x3D: case 0x3F:
        case 0x40:
            return 2;
        case 0x02: case 0x03: case 0x05: case 0x17: case 0x1F:
        case 0x20: case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x28: case 0x29: case 0x2B: case 0x2C: case 0x31:
        case 0x34: case 0x38: case 0x41:
            return 1;
        case 0x08: case 0x09: case 0x0D: case 0x0E: case 0x0F:
        case 0x10: case 0x11: case 0x13: case 0x14: case 0x1A:
        case 0x3A: case 0x42:
            return 4;
        case 0x15:
            return 3;
        case 0x3B: case 0x3C:
            return 6;
        case 0x0C:
        case 0x16: case 0x1B: case 0x1D: case 0x21: case 0x26:
        case 0x27: case 0x2A: case 0x2D: case 0x2E: case 0x2F:
        case 0x30: case 0x32: case 0x33: case 0x36: case 0x3E:
        case 0x43: case 0x44:
            return 0;
        default:
            return -1;      // 0x19 / 0x1C 变长，其余未知
    }
}

bool IscriptFile::ReadOperands(std::size_t& pos, u8 opcode, std::vector<u8>& out) const {
    out.clear();
    const int fixed = FixedOperandSize(opcode);
    if (fixed >= 0) {
        if (pos + static_cast<std::size_t>(fixed) > data_.size()) return false;
        out.insert(out.end(), data_.begin() + pos, data_.begin() + pos + fixed);
        pos += static_cast<std::size_t>(fixed);
        return true;
    }
    if (opcode != 0x19 && opcode != 0x1C) return false;   // 未知 opcode
    if (pos >= data_.size()) return false;
    const u8 count = data_[pos++];
    out.push_back(count);
    const std::size_t bytes = static_cast<std::size_t>(count) * 2;
    if (pos + bytes > data_.size()) return false;
    out.insert(out.end(), data_.begin() + pos, data_.begin() + pos + bytes);
    pos += bytes;
    return true;
}

bool IscriptFile::Disassemble(const IscriptHeader& header, int animIndex,
                              std::vector<IscriptInstruction>& out) const {
    out.clear();
    if (!loaded_ || animIndex < 0 ||
        static_cast<std::size_t>(animIndex) >= header.animOffsets.size()) {
        return false;
    }
    std::size_t pos = header.animOffsets[static_cast<std::size_t>(animIndex)];
    if (pos == 0 || pos >= data_.size()) return false;

    // 少数动画偏移指向共享头（"SCPE"），沿其第一个动画继续
    for (int hop = 0; hop < 4 && pos + 4 <= data_.size(); hop++) {
        if (std::memcmp(data_.data() + pos, "SCPE", 4) != 0) break;
        const IscriptHeader* shared = nullptr;
        for (const IscriptHeader& e : entries_) {
            if (e.offset == pos) {
                shared = &e;
                break;
            }
        }
        u16 next = 0;
        if (shared && !shared->animOffsets.empty()) {
            next = shared->animOffsets[0];
        } else {
            // 共享头可能不在索引表中：直接读 SCPE 后的第一个动画偏移
            if (pos + 10 > data_.size()) return false;
            if (AnimationCount(static_cast<u16>(ReadU32(data_.data() + pos + 4) & 0xFFFF)) < 1) {
                return false;
            }
            next = ReadU16(data_.data() + pos + 8);
        }
        if (next == 0 || next == pos) return false;
        pos = next;
    }
    if (pos == 0 || pos >= data_.size()) return false;

    while (pos < data_.size() && out.size() < 4096) {
        IscriptInstruction ins;
        ins.offset = pos;
        ins.opcode = data_[pos++];
        if (!ReadOperands(pos, ins.opcode, ins.operands)) {
            out.push_back(ins);   // 未知/截断：保留该条以便定位
            break;
        }
        out.push_back(ins);
        if (ins.opcode == 0x16 || ins.opcode == 0x36 ||
            ins.opcode == 0x07 || ins.opcode == 0x30) {
            break;                // end / return / goto / ignorerest
        }
    }
    return !out.empty();
}

bool GrpFile::Load(const std::wstring& path) {
    loaded_ = false;
    frames_.clear();
    Buffer data;
    if (!ReadWholeFile(path, data) || data.size() < 6) return false;
    const int count = ReadU16(data.data());
    if (count <= 0 || count > 65535) return false;
    const std::size_t tableEnd = 6 + static_cast<std::size_t>(count) * 4;
    if (tableEnd > data.size()) return false;

    frameCount_ = count;
    width_ = ReadU16(data.data() + 2);
    height_ = ReadU16(data.data() + 4);
    frames_.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; i++) {
        const std::size_t at = static_cast<std::size_t>(ReadU32(
            data.data() + 6 + static_cast<std::size_t>(i) * 4));
        GrpFrameMeta& f = frames_[static_cast<std::size_t>(i)];
        if (at + 4 <= data.size()) {
            f.x = data[at];
            f.y = data[at + 1];
            f.width = data[at + 2];
            f.height = data[at + 3];
        }
    }
    loaded_ = true;
    return true;
}

}  // namespace scmd
