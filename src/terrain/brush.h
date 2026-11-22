#pragma once
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

// 地形刷图案：width × height 个 MTXM 值，行优先。
struct TerrainBrush {
    std::wstring name;
    int width = 1;
    int height = 1;
    std::vector<u16> tiles;

    bool Empty() const {
        return width <= 0 || height <= 0 ||
               tiles.size() < static_cast<std::size_t>(width) * height;
    }

    u16 At(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        const std::size_t at = static_cast<std::size_t>(y) * width + x;
        return at < tiles.size() ? tiles[at] : 0;
    }

    void SetSingle(u16 value, const std::wstring& label);
    void SetFromTiles(int w, int h, std::vector<u16> data, const std::wstring& label);
};

// 自定义刷子文件（*.scb）：magic "SCMDBRSH"，版本 1，小端。
bool SaveBrushes(const std::wstring& path, const std::vector<TerrainBrush>& brushes);
bool LoadBrushes(const std::wstring& path, std::vector<TerrainBrush>& brushes);

}  // namespace scmd
