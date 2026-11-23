#pragma once
#include "common/types.h"
#include "terrain/tileset.h"

namespace scmd {

// 平台崖壁铺设
// VERIFIED（Space/platform 地形集）：
//   * 崖壁 = 3 圈 type-20 非行走带（组 g302..g357），每圈按「甲板在哪一侧」分 N/E/S/W
//   * SQL 组族（N/E/S/W 三圈）
//   * 崖壁必须向虚空外侧压印，绝不可占用甲板，否则会切断 3 格宽的窄口
class EdgePlacer {
public:
    // 以 walkable 掩码为准，把崖壁压印到 void 侧；返回被改写的瓦片数。
    int StampOutward(const Buffer& walkable, int width, int height, const Tileset& ts,
                     Buffer& mtxm) const;
};

}  // namespace scmd
