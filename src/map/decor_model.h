#pragma once
#include <vector>

#include "common/types.h"

namespace scmd {

// ── DD2（装饰物）──
// VERIFIED（CHK 规范 + 实图 72 B）：8 字节/条
//   u16 类型 / u16 x / u16 y / u8 玩家 / u8 启用标志（0 启用，1 禁用）
struct Doodad {
    u16 type = 0;
    u16 x = 0;
    u16 y = 0;
    u8 owner = 0;
    u8 enabled = 0;
};

class DoodadTable {
public:
    bool Parse(const Buffer& dd2);
    Buffer Serialize() const;
    void Reset();
    bool present() const { return present_; }
    bool Modified() const { return modified_; }
    void MarkModified() { modified_ = true; }
    std::size_t Count() const { return items_.size(); }
    const std::vector<Doodad>& Items() const { return items_; }
    std::vector<Doodad>& MutableItems() {
        modified_ = true;
        return items_;
    }
    // 删除落在瓦片矩形内的装饰物（转换地形用），返回删除数量
    int RemoveInTileRect(int tx0, int ty0, int tx1, int ty1);

private:
    std::vector<Doodad> items_;
    Buffer raw_;           // 长度不是 8 的倍数时原样保留
    bool present_ = false;
    bool modified_ = false;
};

// ── THG2（SP 单位 / 精灵）──
// VERIFIED（CHK 规范）：10 字节/条
//   u16 类型 / u16 x / u16 y / u8 玩家 / u8 未用 / u16 标志（bit12 = 按精灵绘制）
struct SpriteRecord {
    u16 type = 0;
    u16 x = 0;
    u16 y = 0;
    u8 owner = 0;
    u8 unused = 0;
    u16 flags = 0;
    bool DrawAsSprite() const { return (flags & 0x1000u) != 0; }
};

class SpriteTable {
public:
    bool Parse(const Buffer& thg2);
    Buffer Serialize() const;
    void Reset();
    bool present() const { return present_; }
    bool Modified() const { return modified_; }
    void MarkModified() { modified_ = true; }
    std::size_t Count() const { return items_.size(); }
    const std::vector<SpriteRecord>& Items() const { return items_; }
    std::vector<SpriteRecord>& MutableItems() {
        modified_ = true;
        return items_;
    }

private:
    std::vector<SpriteRecord> items_;
    Buffer raw_;           // 长度不是 10 的倍数时原样保留
    bool present_ = false;
    bool modified_ = false;
};

// ── MASK（战争迷雾）──
// 每格 1 字节，bit0..bit7 对应玩家 1..8；1 = 该玩家视野中被迷雾覆盖。
// 缺失的尾部按 0xFF 补齐；多于 宽×高 的字节忽略。
class FogMask {
public:
    bool Parse(const Buffer& mask, int width, int height);
    Buffer Serialize() const;
    void Reset();
    bool present() const { return present_; }
    bool Modified() const { return modified_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool IsFogged(int tx, int ty) const;              // 玩家 1（bit0）
    bool SetFogged(int tx, int ty, bool fogged);      // 返回是否发生变化
    // 按玩家设置整图迷雾位（player 0..7）；返回是否有任意格变化
    bool SetPlayerFog(int player, bool fogged, int width, int height);
    void SetAll(u8 value, int width, int height);     // 整图默认值（0x00 已探索 / 0xFF 未探索）
    // 重设地图尺寸时按偏移搬移迷雾，新增区域默认 0xFF（未探索）
    void Resize(int width, int height, int offsetX, int offsetY);
    void MarkModified() { modified_ = true; }

private:
    Buffer mask_;
    int width_ = 0;
    int height_ = 0;
    bool present_ = false;
    bool modified_ = false;
};

}  // namespace scmd
