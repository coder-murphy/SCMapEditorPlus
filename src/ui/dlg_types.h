#pragma once

#include <string>

namespace scmd {

// 各对话框返回给调用方的数据（对话框本身只负责收集，不落盘）

struct MapSizeParams {
    int width = 64;
    int height = 64;
};

struct ResizeParams {
    int width = 64;
    int height = 64;
    int offsetX = 0;
    int offsetY = 0;
    bool smoothEdges = false;
};

struct LocationRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    // MRGN flags 低 6 位：1 凹陷 / 2 平地 / 4 高地 / 8 低空 / 16 普空 / 32 高空
    unsigned elevationMask = 0;
    int stringId = 0;          // 名称在 STR 中的下标
    std::wstring name;         // 名称文本（由调用方从 STR 读出/写回）
};

struct UnitProps {
    int hp = 100;
    int shield = 100;
    int energy = 100;
    int resource = 0;
    int ammo = 0;
    unsigned flags = 0;   // bit0 无敌 / bit1 幻影 / bit2 钻地 / bit3 隐形 / bit4 升起
};

// 195 单位放置参数
struct UnitPlacementParams {
    int type = 176;       // 单位类型 ID
    int owner = 11;       // 0..7 玩家，11 中立
    int resource = 1500;
};

struct GridSettings {
    bool enabled = false;
    int cellX = 32;
    int cellY = 32;
    int offsetX = 0;
    int offsetY = 0;
};

}  // namespace scmd
