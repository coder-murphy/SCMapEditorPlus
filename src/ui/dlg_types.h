#pragma once

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
    int elevation = 0;   // 0 凹陷 / 1 平地 / 2 高地 / 3 低空 / 4 普空 / 5 高空
};

struct UnitProps {
    int hp = 100;
    int shield = 100;
    int energy = 100;
    int resource = 0;
    int ammo = 0;
    unsigned flags = 0;   // bit0 无敌 / bit1 幻影 / bit2 钻地 / bit3 隐形 / bit4 升起
};

struct GridSettings {
    bool enabled = false;
    int cellX = 32;
    int cellY = 32;
    int offsetX = 0;
    int offsetY = 0;
};

}  // namespace scmd
