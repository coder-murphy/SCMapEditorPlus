#pragma once
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/types.h"

namespace scmd {

class Tileset;

// 原版 ScmDraft2 等距地形（ISOM）管线：
//   ISOM 四个边值 + 邻居连接 -> 计算瓦片索引（type + 邻接） -> 查字典得到 CV5 组
//   -> 按组的有效子瓦片掩码挑选 mega index，写入 MTXM。
// 字典（ISOM_TILE -> 组）从当前地图的 ISOM + MTXM 样本学习，无需外部数据文件。
class IsomTerrain {
public:
    // 从地图样本建立 ERA 参数与字典。isom 长度必须为 8*(w/2+1)*(h+1)；
    // 缺失/尺寸不符或 ERA 无参数时返回 false（调用方回退旧逻辑）。
    bool Learn(const Tileset& tileset, const Buffer& isom, const Buffer& tiles, int width,
               int height, int era);
    void Clear();
    bool ready() const { return ready_; }
    // 该地形类型槽位在当前 ERA 是否有定义（对应 PARAM_TABLE.center[slot].isom != 0）
    bool SlotDefined(int slot) const;

    // 在瓦片坐标 (tileX, tileY) 落一笔等距菱形（更新周围 4 个 ISOM 单元并解析边缘）
    bool Brush(int slot, int tileX, int tileY);

    // 内部 ISOM 是否有改动（用于脏标记/撤销）
    bool isomChanged() const { return isomChanged_; }

    // 内部 ISOM -> 定长字节缓冲（u16 小端，顺序与 CHK 一致）
    void WriteIsom(Buffer& out) const;

    // 把脏单元对应的 MTXM 写回 tilesOut（u16 小端），并收集改动瓦片坐标
    void FlushTiles(Buffer& tilesOut, std::vector<std::pair<int, int>>* changed);

private:
    // ── ERA 参数表（数据来源：ScmDraft2 逆向的 PARAM_TABLE / SHAPE_TABLE）──
    struct CenterParam {
        u16 isom;    // 中心 ISOM 值
        u16 type;    // TILE 类型值
        u16 abut;    // 水平邻接 ID
        u16 style;   // 地形样式（S_NONE/S_DIFFUSE/...）
    };
    struct EdgeParam {
        u16 low;       // 低地中心 ISOM
        u16 high;      // 高地中心 ISOM
        bool upend;    // 上下关系是否反转（仅 Platform 两种）
        u16 type;      // 过渡 TILE 类型
        u16 cliff[4];  // 悬崖水平邻接 ID（非悬崖时全 0）
    };
    struct EraParam {
        u16 edgeStart;          // 边缘 ISOM 起始值
        CenterParam center[13];
        EdgeParam edge[12];
    };
    struct ShapeParam {
        u8 order[4];    // 0 = LOW, 1 = HIGH
        u16 type[4];    // AS_LOW / AS_HIGH / AS_EDGE / M_DIFFUSE(n) / M_CLIFF(n)
        u16 xAbut[4];
        u16 yAbut[2];
    };

    struct EraInfo {
        bool init = false;
        int centerNum = 0;
        int edgeNum = 0;
        int maxCenterType = 0;
        int isom2center[16] = {};
        struct CenterLink {
            int below = -1;
            int belowEdge = -1;
            int aboveNum = 0;
            int above[6] = {};
            int aboveEdge[6] = {};
        } center[13];
        struct EdgeInfo {
            int low = 0;
            int high = 0;
            bool cliff = false;
            int shape[14][4] = {};
        } edge[12];
    };

    // 计算得到的瓦片索引（ISOM 单元 -> TILE 类型 + 邻接关系）
    struct TileIndex {
        u16 type = 0;
        u16 leftAbut = 0;
        u16 topAbut = 0;
        u16 rightAbut = 0;
        u16 bottomAbut = 0;
        u16 upAbut = 0;
        u16 downAbut = 0;
    };
    struct TileMap {
        u16 type[4] = {};
        u16 xAbut[4] = {};
        u16 yAbut[4] = {};
        u16 zAbut[4] = {};
        u16 proj[2] = {};
    };
    struct DictEntry {
        u16 group = 0;   // 偶数组成员（奇数格 = group + 1）
        u16 megaMask = 0;
    };

    static const EraParam& ParamFor(int era);
    static const ShapeParam* Shapes();
    // 过渡模板 -> TILE 类型 / 邻接（对应原版 map_edge_* 系列）
    static u16 MapEdgeTileType(const EraParam& p, const EraInfo& info, int low, int high,
                               int edge, u16 temp);
    static u16 MapEdgeHorAbuttal(const EraParam& p, const EraInfo& info, int low, int high,
                                 int edge, u16 temp, u16* proj);
    static u16 MapEdgeVerAbuttal(const EraParam& p, const EraInfo& info, int high, int edge,
                                 u16 temp);

    void BuildEraInfo();
    bool InBounds(int cx, int cy) const {
        return cx >= 0 && cy >= 0 && cx < rowW_ && cy < line_;
    }
    u16* Cell(int cx, int cy) { return &isom_[(static_cast<std::size_t>(cy) * rowW_ + cx) * 4]; }
    const u16* Cell(int cx, int cy) const {
        return &isom_[(static_cast<std::size_t>(cy) * rowW_ + cx) * 4];
    }
    u16 TileAt(int x, int y) const;
    void SetTile(int x, int y, u16 v);

    static void CalcCornerPos(int from, int bx, int by, int& cx, int& cy);
    static void CalcLinkPos(int from, int bx, int by, int& lx, int& ly);

    bool GetIsomShape(u16 isom, int& low, int& high, int shape[4]) const;
    int IsomToCenter(u16 isom) const;
    u16 CenterIsom(int center) const;
    u16 MatchShape(const int shape[4]) const;
    int SearchBrushLink(int from, int to) const;

    bool SetTilePos(int cx, int cy);
    bool UpdateIsom(u16 isom, int from, int bx, int by);
    u16 IsometricLink(u16 isom, int from, int bx, int by, bool& updated);
    void ResolveLinks(std::deque<std::pair<int, int>>& queue);
    bool MakeTileMap(int cx, int cy, const TileIndex* computed) const;
    void MapIsomTile(int cx, int cy, const TileMap& tm, TileIndex* computed) const;
    bool MakeTileIndex(int cx, int cy, TileIndex* computed) const;
    void AdjustDirty(int cx, int cy, const TileIndex* computed);
    u16 GenMegaIndex(int cx, int cy, const DictEntry& dict, u16 upAbut) const;
    void UpdateTile(int cx, int cy, const TileIndex* computed);
    bool KeyFor(const TileIndex& t, u32& key) const;
    static u16 MegaMaskOf(const Tileset& tileset, u16 group);

    bool isomChanged_ = false;
    bool ready_ = false;
    int width_ = 0;
    int height_ = 0;
    int era_ = 0;
    int rowW_ = 0;
    int line_ = 0;
    std::vector<u16> isom_;                 // 内部 ISOM（每单元 4 个 u16）
    std::vector<u8> dirty_;                 // 每单元 1 位（这里用 1 字节简化）
    std::vector<u16> tiles_;                // 当前 MTXM 副本
    std::unordered_map<u32, DictEntry> dict_;  // 样本学习到的 ISOM_TILE -> 组
    // 放宽匹配：忽略上下邻接 / 只按类型（地图样本不足以覆盖所有组合时使用）
    std::unordered_map<u32, DictEntry> dictFlat_;
    std::unordered_map<u16, DictEntry> dictType_;
    EraInfo info_;
    const Tileset* tileset_ = nullptr;
};

}  // namespace scmd
