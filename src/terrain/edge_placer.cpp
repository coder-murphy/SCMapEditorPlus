#include "terrain/edge_placer.h"

#include <algorithm>
#include <vector>

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

struct Family {
    const u16* items;
    int count;
};

// 方向顺序：N / E / S / W（与 nx/ny 表一致）
const Family kFamilies[3][4] = {
    {{kRing1N, 2}, {kRing1E, 2}, {kRing1S, 4}, {kRing1W, 2}},
    {{kRing2N, 2}, {kRing2E, 2}, {kRing2S, 2}, {kRing2W, 2}},
    {{kRing3N, 2}, {kRing3E, 2}, {kRing3S, 2}, {kRing3W, 2}},
};

constexpr int kRingCount = 3;
// CV5 首字：raw 0x14（bit0=0，Type() = raw>>1 = 10）；兼容按未移位值判断的写法
bool IsCliffGroup(const Tileset& ts, u16 group) {
    if (ts.IsWalkable(group)) return false;
    const u16 type = ts.Type(group);
    return type == 10 || type == 20;
}

}  // namespace

int EdgePlacer::StampOutward(const Buffer& walkable, int width, int height, const Tileset& ts,
                             Buffer& mtxm) const {
    if (width <= 0 || height <= 0 || !ts.IsLoaded()) return 0;
    const std::size_t tiles = static_cast<std::size_t>(width) * height;
    if (walkable.size() < tiles || mtxm.size() < tiles * 2) return 0;

    // 多源 BFS：甲板距离 0，向外 1..3 圈；更远的虚空不处理
    std::vector<u8> dist(tiles, 0xFF);
    std::vector<int> queue;
    queue.reserve(tiles);
    for (std::size_t i = 0; i < tiles; i++) {
        if (walkable[i]) {
            dist[i] = 0;
            queue.push_back(static_cast<int>(i));
        }
    }
    if (queue.empty()) return 0;

    const int nx[4] = {0, 1, 0, -1};   // N E S W
    const int ny[4] = {-1, 0, 1, 0};
    for (std::size_t head = 0; head < queue.size(); head++) {
        const int idx = queue[head];
        if (dist[static_cast<std::size_t>(idx)] >= kRingCount) continue;
        const int x = idx % width;
        const int y = idx / width;
        for (int d = 0; d < 4; d++) {
            const int px = x + nx[d];
            const int py = y + ny[d];
            if (px < 0 || py < 0 || px >= width || py >= height) continue;
            const std::size_t p = static_cast<std::size_t>(py) * width + px;
            if (dist[p] != 0xFF) continue;
            dist[p] = static_cast<u8>(dist[static_cast<std::size_t>(idx)] + 1);
            queue.push_back(static_cast<int>(p));
        }
    }

    int stamped = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            const u8 d = dist[i];
            if (d == 0 || d == 0xFF || d > kRingCount) continue;

            // 甲板在哪个方向：距离为 d-1 的邻居所在侧
            int dir = -1;
            for (int k = 0; k < 4; k++) {
                const int px = x + nx[k];
                const int py = y + ny[k];
                if (px < 0 || py < 0 || px >= width || py >= height) continue;
                const u8 nd = dist[static_cast<std::size_t>(py) * width + px];
                if (nd == d - 1) {
                    dir = k;
                    break;
                }
            }
            if (dir < 0) continue;

            const Family& fam = kFamilies[d - 1][dir];
            if (fam.count <= 0) continue;

            // 沿边坐标：N/S 沿 x 变化，E/W 沿 y 变化
            const int along = (dir == 0 || dir == 2) ? x : y;

            // 按位置循环取组；跳过当前地形集里不是崖壁的组
            u16 group = 0;
            bool found = false;
            for (int k = 0; k < fam.count; k++) {
                const u16 g = fam.items[(along + k) % fam.count];
                if (IsCliffGroup(ts, g)) {
                    group = g;
                    found = true;
                    break;
                }
            }
            if (!found) continue;

            // index 只提供纹理变体（0..2 有效）
            const u16 value = static_cast<u16>((group << 4) | (along % 3));
            const std::size_t at = i * 2;
            const u16 before = static_cast<u16>(mtxm[at] | (mtxm[at + 1] << 8));
            if (before == value) continue;
            mtxm[at] = static_cast<u8>(value & 0xFF);
            mtxm[at + 1] = static_cast<u8>(value >> 8);
            stamped++;
        }
    }
    return stamped;
}

}  // namespace scmd
