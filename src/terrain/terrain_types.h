#pragma once

#include "common/types.h"

namespace scmd {

// ScmDraft2 地形类型表（从 [CN]ScmDraft2.exe 逆向提取）。
// 每个地形集固定 13 个槽位；槽位的名称与 CV5 组号一一对应，group == 0 表示未定义。
// 等距地形铺设规则：偶数列使用 group，奇数列使用 group + 1。
constexpr int kTerrainSetCount = 8;
constexpr int kTerrainTypeSlots = 13;

struct TerrainTypeInfo {
    const wchar_t* name;
    u16 group;   // CV5 组号；0 = Undefined
};

// tileset 0..7（CHK ERA）；越界返回 nullptr。
const wchar_t* TerrainSetName(int tileset);
// 返回该地形集的 13 个槽位；越界返回 nullptr。
const TerrainTypeInfo* TerrainTypes(int tileset);
// 按组号查找槽位（同时接受 group 与 group + 1）；找不到返回 -1。
int TerrainTypeSlotForGroup(int tileset, u16 group);

}  // namespace scmd
