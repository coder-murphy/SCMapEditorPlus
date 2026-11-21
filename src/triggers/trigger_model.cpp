#include "triggers/trigger_model.h"

#include <cstring>

namespace scmd {

namespace {
u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
void WriteU16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}
void WriteU32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>((v >> 8) & 0xFF);
    p[2] = static_cast<u8>((v >> 16) & 0xFF);
    p[3] = static_cast<u8>((v >> 24) & 0xFF);
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
    if (index >= items_.size()) items_.resize(index + 1);
    items_[index] = std::move(value);
}

// ── Trigger ──
int Trigger::ConditionCount() const {
    int n = 0;
    while (n < 16 && conditions[n].type != 0) n++;
    return n;
}

int Trigger::ActionCount() const {
    int n = 0;
    while (n < 64 && actions[n].type != 0) n++;
    return n;
}

bool Trigger::HasPlayer(int player) const {
    return player >= 0 && player < 27 && effectivePlayers[player] != 0;
}

void Trigger::ClearCondition(int index) {
    if (index >= 0 && index < 16) conditions[index] = TriggerCondition();
}

void Trigger::ClearAction(int index) {
    if (index >= 0 && index < 64) actions[index] = TriggerAction();
}

void Trigger::Clear() {
    for (auto& c : conditions) c = TriggerCondition();
    for (auto& a : actions) a = TriggerAction();
    executionFlags = 0;
    for (u8& p : effectivePlayers) p = 0;
    currentAction = 0;
}

// ── TriggerSection ──
bool TriggerSection::Parse(const Buffer& trig) {
    items_.clear();
    trailing_.clear();
    const std::size_t count = trig.size() / kBlockSize;
    items_.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        const u8* b = trig.data() + i * kBlockSize;
        Trigger t;
        for (std::size_t c = 0; c < kConditionCount; c++) {
            const u8* p = b + c * 20;
            TriggerCondition& cond = t.conditions[c];
            cond.locationId = ReadU32(p + 0);
            cond.player = ReadU32(p + 4);
            cond.amount = ReadU32(p + 8);
            cond.unitType = ReadU16(p + 12);
            cond.comparison = p[14];
            cond.type = p[15];
            cond.resourceType = p[16];
            cond.property = p[17];
            cond.reserved[0] = p[18];
            cond.reserved[1] = p[19];
        }
        for (std::size_t a = 0; a < kActionCount; a++) {
            const u8* p = b + 320 + a * 32;
            TriggerAction& act = t.actions[a];
            act.locationId = ReadU32(p + 0);
            act.stringId = ReadU32(p + 4);
            act.wavId = ReadU32(p + 8);
            act.time = ReadU32(p + 12);
            act.player = ReadU32(p + 16);
            act.target = ReadU32(p + 20);
            act.setting = ReadU16(p + 24);
            act.type = p[26];
            act.amount = p[27];
            act.property = p[28];
            act.reserved[0] = p[29];
            act.reserved[1] = p[30];
            act.reserved[2] = p[31];
        }
        t.executionFlags = ReadU32(b + 2368);
        std::memcpy(t.effectivePlayers, b + 2372, 27);
        t.currentAction = b[2399];
        items_.push_back(t);
    }
    const std::size_t used = count * kBlockSize;
    if (used < trig.size()) {
        trailing_.assign(trig.begin() + static_cast<std::ptrdiff_t>(used), trig.end());
    }
    return trig.size() % kBlockSize == 0;
}

Buffer TriggerSection::Serialize() const {
    Buffer out(items_.size() * kBlockSize + trailing_.size(), 0);
    for (std::size_t i = 0; i < items_.size(); i++) {
        u8* b = out.data() + i * kBlockSize;
        const Trigger& t = items_[i];
        for (std::size_t c = 0; c < kConditionCount; c++) {
            u8* p = b + c * 20;
            const TriggerCondition& cond = t.conditions[c];
            WriteU32(p + 0, cond.locationId);
            WriteU32(p + 4, cond.player);
            WriteU32(p + 8, cond.amount);
            WriteU16(p + 12, cond.unitType);
            p[14] = cond.comparison;
            p[15] = cond.type;
            p[16] = cond.resourceType;
            p[17] = cond.property;
            p[18] = cond.reserved[0];
            p[19] = cond.reserved[1];
        }
        for (std::size_t a = 0; a < kActionCount; a++) {
            u8* p = b + 320 + a * 32;
            const TriggerAction& act = t.actions[a];
            WriteU32(p + 0, act.locationId);
            WriteU32(p + 4, act.stringId);
            WriteU32(p + 8, act.wavId);
            WriteU32(p + 12, act.time);
            WriteU32(p + 16, act.player);
            WriteU32(p + 20, act.target);
            WriteU16(p + 24, act.setting);
            p[26] = act.type;
            p[27] = act.amount;
            p[28] = act.property;
            p[29] = act.reserved[0];
            p[30] = act.reserved[1];
            p[31] = act.reserved[2];
        }
        WriteU32(b + 2368, t.executionFlags);
        std::memcpy(b + 2372, t.effectivePlayers, 27);
        b[2399] = t.currentAction;
    }
    if (!trailing_.empty()) {
        std::memcpy(out.data() + items_.size() * kBlockSize, trailing_.data(),
                    trailing_.size());
    }
    return out;
}

}  // namespace scmd
