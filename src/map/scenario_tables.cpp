#include "map/scenario_tables.h"

#include <cstring>

namespace scmd {

namespace {
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
void WriteU32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>((v >> 8) & 0xFF);
    p[2] = static_cast<u8>((v >> 16) & 0xFF);
    p[3] = static_cast<u8>((v >> 24) & 0xFF);
}
}  // namespace

bool StringIndexTable::Parse(const Buffer& data) {
    Reset();
    if (data.empty()) return true;
    present_ = true;
    const std::size_t need = expected_ * 4;
    if (data.size() != need) {
        raw_ = data;      // 异常长度：不解析，保存时原样写回
        return false;
    }
    ids_.resize(expected_, 0);
    for (std::size_t i = 0; i < expected_; i++) ids_[i] = ReadU32(data.data() + i * 4);
    return true;
}

Buffer StringIndexTable::Serialize() const {
    if (!present_) return Buffer();
    if (!raw_.empty()) return raw_;
    Buffer out(expected_ * 4, 0);
    for (std::size_t i = 0; i < ids_.size() && i < expected_; i++) {
        WriteU32(out.data() + i * 4, ids_[i]);
    }
    return out;
}

void StringIndexTable::Reset() {
    ids_.clear();
    raw_.clear();
    present_ = false;
    modified_ = false;
}

u32 StringIndexTable::Get(std::size_t index) const {
    return index < ids_.size() ? ids_[index] : 0;
}

bool StringIndexTable::Set(std::size_t index, u32 value) {
    if (index >= expected_ || !raw_.empty()) return false;
    if (ids_.size() < expected_) ids_.resize(expected_, 0);
    if (ids_[index] == value) return false;
    ids_[index] = value;
    present_ = true;
    modified_ = true;
    return true;
}

}  // namespace scmd
