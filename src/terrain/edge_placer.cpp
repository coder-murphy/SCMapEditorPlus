#include "terrain/edge_placer.h"

namespace scmd {

// VERIFIED 组族（来自 (8)Astro World v1.3.scx 实测）
namespace {
// ring 1：贴甲板的一圈
const u16 kRing1N[] = {308, 309};
const u16 kRing1E[] = {334, 336};
const u16 kRing1S[] = {310, 311, 316, 317};
const u16 kRing1W[] = {339, 341};
// ring 2
const u16 kRing2N[] = {306, 307};
const u16 kRing2E[] = {335, 337};
const u16 kRing2S[] = {318, 319};
const u16 kRing2W[] = {338, 340};
// ring 3
const u16 kRing3N[] = {304, 305};
const u16 kRing3E[] = {354, 330};
const u16 kRing3S[] = {320, 321};
const u16 kRing3W[] = {325, 357};
}  // namespace

void EdgePlacer::StampOutward(const Buffer& walkable, int width, int height, const Tileset& ts, Buffer& mtxm) const {
    (void)walkable;
    (void)width;
    (void)height;
    (void)ts;
    (void)mtxm;
    // TODO: 4 邻域 BFS 求 ring(1..3) + dir(N/E/S/W)，沿边按位置循环取组、索引随圈轮转
}

}  // namespace scmd
