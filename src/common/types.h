#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace scmd {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

using Buffer = std::vector<u8>;

// 星际 1 地图的两个上限：256x256 瓦片，单位坐标按像素（1 瓦片 = 32 像素）
constexpr int kMaxMapSize = 256;
constexpr int kTilePixels = 32;

struct TilePos {
    int x = 0;
    int y = 0;
};

}  // namespace scmd
