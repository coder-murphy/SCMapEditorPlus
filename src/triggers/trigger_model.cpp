#include "triggers/trigger_model.h"

#include <cstring>

namespace scmd {

namespace {
u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
void WriteU16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}
}  // namespace

// ── LocationTable ──
bool LocationTable::Parse(const Buffer& mrgn) {
    items_.clear();
    if (mrgn.size() % 20 != 0) return false;
    const std::size_t count = mrgn.size() / 20;
    items_.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        const u8* p = mrgn.data() + i * 20;
        Location loc;
        loc.left = ReadU16(p + 0);
        loc.top = ReadU16(p + 2);
        loc.right = ReadU16(p + 4);
        loc.bottom = ReadU16(p + 6);
        loc.stringId = ReadU16(p + 8);
        loc.flags = ReadU16(p + 10);
        std::memcpy(loc.reserved, p + 12, 8);
        items_.push_back(loc);
    }
    return true;
}

Buffer LocationTable::Serialize() const {
    Buffer out(items_.size() * 20, 0);
    for (std::size_t i = 0; i < items_.size(); i++) {
        u8* p = out.data() + i * 20;
        WriteU16(p + 0, items_[i].left);
        WriteU16(p + 2, items_[i].top);
        WriteU16(p + 4, items_[i].right);
        WriteU16(p + 6, items_[i].bottom);
        WriteU16(p + 8, items_[i].stringId);
        WriteU16(p + 10, items_[i].flags);
        std::memcpy(p + 12, items_[i].reserved, 8);
    }
    return out;
}

// ── StringTable ──
bool StringTable::Parse(const Buffer& str) {
    items_.clear();
    trailing_.clear();
    declaredCount_ = 0;
    if (str.size() < 2) return false;

    declaredCount_ = ReadU16(str.data());
    std::size_t at = 2;
    for (u16 i = 0; i < declaredCount_ && at < str.size(); i++) {
        const std::size_t start = at;
        while (at < str.size() && str[at] != 0) at++;
        items_.emplace_back(reinterpret_cast<const char*>(str.data() + start), at - start);
        if (at < str.size()) at++;          // 跳过结尾 0
    }
    if (at < str.size()) {
        trailing_.assign(str.begin() + static_cast<std::ptrdiff_t>(at), str.end());
    }
    return true;
}

Buffer StringTable::Serialize() const {
    const u16 count = items_.empty() && declaredCount_ > 0
                          ? declaredCount_
                          : static_cast<u16>(items_.size() > declaredCount_ ? items_.size()
                                                                          : declaredCount_);
    std::size_t size = 2 + trailing_.size();
    for (const auto& s : items_) size += s.size() + 1;
    Buffer out(size, 0);
    WriteU16(out.data(), count);
    std::size_t at = 2;
    for (const auto& s : items_) {
        if (!s.empty()) std::memcpy(out.data() + at, s.data(), s.size());
        at += s.size();
        out[at] = 0;
        at++;
    }
    if (!trailing_.empty()) {
        std::memcpy(out.data() + at, trailing_.data(), trailing_.size());
    }
    return out;
}

const std::string& StringTable::At(std::size_t index) const {
    return index < items_.size() ? items_[index] : empty_;
}

void StringTable::Set(std::size_t index, std::string value) {
    if (index < items_.size()) items_[index] = std::move(value);
}

// ── TriggerSection ──
bool TriggerSection::Parse(const Buffer& trig) {
    raw_ = trig;
    return trig.size() % kBlockSize == 0;
}

}  // namespace scmd
