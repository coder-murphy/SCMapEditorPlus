#pragma once
#include <string>

#include "common/types.h"

namespace scmd {

// 地形集四件套：CV5 / VX4 / VR4 / WPE
// VERIFIED 渲染管线：
//   MTXM v -> group = (v & 0x7FF0) >> 4, index = v & 0x0F
//   CV5[group] 52 字节，16 个大瓦片引用位于 u16[10..25]（字节偏移 20）
//   VX4[mega] 32 字节 -> 16 个 u16 引用：ref>>1 = VR4 id，ref&1 = 水平翻转
//   VR4[vrid] 64 字节 = 8x8 调色板索引
//   WPE 每色 4 字节（第 4 字节忽略）
class Tileset {
public:
    // dir 例如 "..\tools\mapgen\ts"，文件名为 <prefix>.cv5/.vx4/.vr4/.wpe
    bool Load(const std::wstring& dir, const std::wstring& prefix);
    void Unload();
    bool IsLoaded() const { return !cv5_.empty(); }

    int GroupCount() const { return static_cast<int>(cv5_.size() / 52); }
    int MegaCount() const { return static_cast<int>(vx4_.size() / 32); }
    int VrCount() const { return static_cast<int>(vr4_.size() / 64); }

    u16 MegatileOf(u16 mtxmValue) const;
    // 输出 32x32=1024 字节的调色板索引
    bool TileBlock(u16 mtxmValue, u8 out[1024]) const;

    bool IsWalkable(u16 group) const;
    u16 Type(u16 group) const;

    // 组内子索引（0..15）是否有有效大瓦片（CV5 引用非 0）
    bool HasSubtile(u16 group, int index) const;
    // 组内第一个有效子索引；无有效项时返回 0
    int FirstValidSubtile(u16 group) const;
    // 随机取有效子索引（与 ScmDraft2 一致：优先 0..5，其次 6..15）
    int RandomValidSubtile(u16 group) const;

    // CV5 里 flags & 0x0040（Creep）的 4×4 图案组；无则 HasCreep() 为 false
    bool HasCreep() const { return creepGroup_ >= 0; }
    int CreepGroup() const { return creepGroup_; }
    // 取 creep 图案第 slot（0..15，4×4 行优先）个 MTXM 值；空槽返回 false
    bool CreepTile(int slot, u16& value) const;

    // WPE 原始数据，每色 4 字节
    const Buffer& palette() const { return wpe_; }

private:
    Buffer cv5_, vx4_, vr4_, wpe_;
    int creepGroup_ = -1;
};

}  // namespace scmd
