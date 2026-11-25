#include "terrain/terrain_types.h"

namespace scmd {

namespace {

// 名称表：exe 文件偏移 0x7B038（8 个地形集名，15 字节定长）
// 类型名：exe 文件偏移 0x7B0B0（8 × 13 × 30 字节定长）
static const wchar_t* const kTerrainSetNames[kTerrainSetCount] = {
    L"Badlands",       L"Space Platform", L"Installation",   L"Ash World",
    L"Jungle World",   L"Desert World",   L"Ice World",      L"Twilight World",
};

// 组号表：exe 虚拟地址 0x47C878（8 × 14 个 u32，第 14 项保留为 0）
static const TerrainTypeInfo kTerrainTypes[kTerrainSetCount][kTerrainTypeSlots] = {
    {   // Badlands
        {L"Dirt", 2},
        {L"Mud", 20},
        {L"High Dirt", 4},
        {L"Water", 6},
        {L"Grass", 8},
        {L"High Grass", 10},
        {L"Structure", 18},
        {L"Asphalt", 16},
        {L"Rocky Ground", 12},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
    },
    {   // Space Platform
        {L"Space", 2},
        {L"Low Platform", 14},
        {L"Rusty Pit", 18},
        {L"Platform", 4},
        {L"Dark platform", 16},
        {L"Plating", 6},
        {L"Solar Array", 12},
        {L"High Platform", 8},
        {L"High Plating", 10},
        {L"Elevated Catwalk", 20},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
    },
    {   // Installation
        {L"Substructure", 2},
        {L"Floor", 6},
        {L"Roof", 8},
        {L"Substructure Plating", 4},
        {L"Plating", 10},
        {L"Sunstructure Panels", 14},
        {L"Bottomless Pit", 12},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
    },
    {   // Ash World
        {L"Magma", 14},
        {L"Dirt", 2},
        {L"Lava", 4},
        {L"Shale", 10},
        {L"Broken Rock", 16},
        {L"High Dirt", 6},
        {L"High Lava", 8},
        {L"High Shale", 12},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
        {L"Undefined", 0},
    },
    {   // Jungle World
        {L"Water", 6},
        {L"Dirt", 2},
        {L"Mud", 26},
        {L"Jungle", 8},
        {L"Rocky Ground", 10},
        {L"Ruins", 14},
        {L"Raised Jungle", 12},
        {L"Temple", 16},
        {L"High Dirt", 4},
        {L"High Jungle", 18},
        {L"High Ruins", 20},
        {L"High Raised Jungle", 22},
        {L"High Temple", 24},
    },
    {   // Desert World
        {L"Tar", 6},
        {L"Dirt", 2},
        {L"Dried Mud", 26},
        {L"Sand Dunes", 8},
        {L"Rocky Ground", 10},
        {L"Crags", 14},
        {L"Sandy Sunken Pit", 12},
        {L"Compound", 16},
        {L"High Dirt", 4},
        {L"High Sand Dunes", 18},
        {L"High Crags", 20},
        {L"High Sandy Sunken Pit", 22},
        {L"High Compound", 24},
    },
    {   // Ice World
        {L"Ice", 6},
        {L"Snow", 2},
        {L"Moguls", 26},
        {L"Dirt", 8},
        {L"Rocky Snow", 10},
        {L"Grass", 14},
        {L"Water", 12},
        {L"Outpost", 16},
        {L"High Snow", 4},
        {L"High Dirt", 18},
        {L"High Grass", 20},
        {L"High Water", 22},
        {L"High Outpost", 24},
    },
    {   // Twilight World
        {L"Water", 6},
        {L"Dirt", 2},
        {L"Mud", 26},
        {L"Crushed Rock", 8},
        {L"Crevices", 10},
        {L"Flagstones", 14},
        {L"Sunken Ground", 12},
        {L"Basilica", 16},
        {L"High Dirt", 4},
        {L"High Crushed Rock", 18},
        {L"High Flagstones", 20},
        {L"High Sunken Ground", 22},
        {L"High Basilica", 24},
    },
};

}  // namespace

const wchar_t* TerrainSetName(int tileset) {
    if (tileset < 0 || tileset >= kTerrainSetCount) return nullptr;
    return kTerrainSetNames[tileset];
}

const TerrainTypeInfo* TerrainTypes(int tileset) {
    if (tileset < 0 || tileset >= kTerrainSetCount) return nullptr;
    return kTerrainTypes[tileset];
}

int TerrainTypeSlotForGroup(int tileset, u16 group) {
    const TerrainTypeInfo* types = TerrainTypes(tileset);
    if (!types) return -1;
    const u16 pair = static_cast<u16>(group & ~1u);
    for (int slot = 0; slot < kTerrainTypeSlots; slot++) {
        const u16 base = types[slot].group;
        if (base != 0 && (base & ~1u) == pair) return slot;
    }
    return -1;
}

}  // namespace scmd
