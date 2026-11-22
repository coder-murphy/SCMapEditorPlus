#include "map/decor_model.h"

#include <algorithm>
#include <cstring>

namespace scmd {

namespace {
u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
void WriteU16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v & 0xFF);
    p[1] = static_cast<u8>(v >> 8);
}
constexpr std::size_t kDoodadSize = 8;
constexpr std::size_t kSpriteSize = 10;
}  // namespace

bool DoodadTable::Parse(const Buffer& dd2) {
    Reset();
    if (dd2.empty()) return true;
    present_ = true;
    if (dd2.size() % kDoodadSize != 0) {
        raw_ = dd2;      // 异常长度：不解析，保存时原样写回
        return false;
    }
    const std::size_t count = dd2.size() / kDoodadSize;
    items_.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        const u8* p = dd2.data() + i * kDoodadSize;
        Doodad d;
        d.type = ReadU16(p + 0);
        d.x = ReadU16(p + 2);
        d.y = ReadU16(p + 4);
        d.owner = p[6];
        d.enabled = p[7];
        items_.push_back(d);
    }
    return true;
}

Buffer DoodadTable::Serialize() const {
    if (!raw_.empty()) return raw_;
    Buffer out(items_.size() * kDoodadSize, 0);
    for (std::size_t i = 0; i < items_.size(); i++) {
        u8* p = out.data() + i * kDoodadSize;
        WriteU16(p + 0, items_[i].type);
        WriteU16(p + 2, items_[i].x);
        WriteU16(p + 4, items_[i].y);
        p[6] = items_[i].owner;
        p[7] = items_[i].enabled;
    }
    return out;
}

void DoodadTable::Reset() {
    items_.clear();
    raw_.clear();
    present_ = false;
    modified_ = false;
}

int DoodadTable::RemoveInTileRect(int tx0, int ty0, int tx1, int ty1) {
    if (tx0 > tx1) std::swap(tx0, tx1);
    if (ty0 > ty1) std::swap(ty0, ty1);
    const std::size_t before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [=](const Doodad& d) {
                                    const int tx = static_cast<int>(d.x) / kTilePixels;
                                    const int ty = static_cast<int>(d.y) / kTilePixels;
                                    return tx >= tx0 && tx <= tx1 && ty >= ty0 && ty <= ty1;
                                }),
                 items_.end());
    const int removed = static_cast<int>(before - items_.size());
    if (removed > 0) modified_ = true;
    return removed;
}

bool SpriteTable::Parse(const Buffer& thg2) {
    Reset();
    if (thg2.empty()) return true;
    present_ = true;
    if (thg2.size() % kSpriteSize != 0) {
        raw_ = thg2;      // 异常长度：不解析，保存时原样写回
        return false;
    }
    const std::size_t count = thg2.size() / kSpriteSize;
    items_.reserve(count);
    for (std::size_t i = 0; i < count; i++) {
        const u8* p = thg2.data() + i * kSpriteSize;
        SpriteRecord s;
        s.type = ReadU16(p + 0);
        s.x = ReadU16(p + 2);
        s.y = ReadU16(p + 4);
        s.owner = p[6];
        s.unused = p[7];
        s.flags = ReadU16(p + 8);
        items_.push_back(s);
    }
    return true;
}

Buffer SpriteTable::Serialize() const {
    if (!raw_.empty()) return raw_;
    Buffer out(items_.size() * kSpriteSize, 0);
    for (std::size_t i = 0; i < items_.size(); i++) {
        u8* p = out.data() + i * kSpriteSize;
        WriteU16(p + 0, items_[i].type);
        WriteU16(p + 2, items_[i].x);
        WriteU16(p + 4, items_[i].y);
        p[6] = items_[i].owner;
        p[7] = items_[i].unused;
        WriteU16(p + 8, items_[i].flags);
    }
    return out;
}

void SpriteTable::Reset() {
    items_.clear();
    raw_.clear();
    present_ = false;
    modified_ = false;
}

bool FogMask::Parse(const Buffer& mask, int width, int height) {
    Reset();
    width_ = width;
    height_ = height;
    if (width <= 0 || height <= 0) return false;
    if (mask.empty()) return true;      // 段缺失：保持 absent
    present_ = true;
    const std::size_t need = static_cast<std::size_t>(width) * height;
    mask_.assign(need, 0xFF);           // 规范：缺失部分默认 0xFF
    const std::size_t n = std::min(need, mask.size());
    if (n > 0) std::memcpy(mask_.data(), mask.data(), n);
    return true;
}

Buffer FogMask::Serialize() const {
    if (!present_) return Buffer();
    return mask_;
}

void FogMask::Reset() {
    mask_.clear();
    width_ = 0;
    height_ = 0;
    present_ = false;
    modified_ = false;
}

bool FogMask::IsFogged(int tx, int ty) const {
    if (!present_ || tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return false;
    const std::size_t at = static_cast<std::size_t>(ty) * width_ + tx;
    return at < mask_.size() && (mask_[at] & 0x01u) != 0;
}

bool FogMask::SetFogged(int tx, int ty, bool fogged) {
    if (tx < 0 || ty < 0 || tx >= width_ || ty >= height_) return false;
    const std::size_t need = static_cast<std::size_t>(width_) * height_;
    if (!present_ || mask_.size() != need) {
        Buffer old = mask_;
        mask_.assign(need, 0xFF);
        if (!old.empty()) std::memcpy(mask_.data(), old.data(), std::min(old.size(), need));
        present_ = true;
    }
    const std::size_t at = static_cast<std::size_t>(ty) * width_ + tx;
    const u8 before = mask_[at];
    if (fogged) {
        mask_[at] |= 0x01u;
    } else {
        mask_[at] &= static_cast<u8>(~0x01u);
    }
    if (mask_[at] == before) return false;
    modified_ = true;
    return true;
}

bool FogMask::SetPlayerFog(int player, bool fogged, int width, int height) {
    if (player < 0 || player > 7 || width <= 0 || height <= 0) return false;
    const std::size_t need = static_cast<std::size_t>(width) * height;
    if (!present_ || mask_.size() != need) {
        Buffer old = mask_;
        mask_.assign(need, 0xFF);
        if (!old.empty()) std::memcpy(mask_.data(), old.data(), std::min(old.size(), need));
        present_ = true;
    }
    width_ = width;
    height_ = height;
    const u8 bit = static_cast<u8>(1u << player);
    bool changed = false;
    for (u8& b : mask_) {
        const u8 before = b;
        if (fogged) b |= bit;
        else b &= static_cast<u8>(~bit);
        if (b != before) changed = true;
    }
    if (changed) modified_ = true;
    return changed;
}

void FogMask::SetAll(u8 value, int width, int height) {
    if (width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    mask_.assign(static_cast<std::size_t>(width) * height, value);
    present_ = true;
    modified_ = true;
}

void FogMask::Resize(int width, int height, int offsetX, int offsetY) {
    if (width <= 0 || height <= 0) return;
    Buffer next(static_cast<std::size_t>(width) * height, 0xFF);
    if (present_ && width_ > 0 && height_ > 0) {
        for (int y = 0; y < height_; y++) {
            const int ny = y + offsetY;
            if (ny < 0 || ny >= height) continue;
            for (int x = 0; x < width_; x++) {
                const int nx = x + offsetX;
                if (nx < 0 || nx >= width) continue;
                const std::size_t src = static_cast<std::size_t>(y) * width_ + x;
                if (src < mask_.size()) next[static_cast<std::size_t>(ny) * width + nx] = mask_[src];
            }
        }
        modified_ = true;
    }
    mask_ = std::move(next);
    width_ = width;
    height_ = height;
}

}  // namespace scmd
