#pragma once
#include <string>

#include "common/types.h"
#include "terrain/tileset.h"

namespace scmd {

// 离屏地形图（32 位 BGRA，自上而下）
struct TerrainImage {
    int width = 0;
    int height = 0;
    Buffer pixels;   // BGRA，长度 = width * height * 4
};

// 把 MTXM 瓦片表渲染成位图；pxPerTile 支持 1..128（最近邻采样）
class TerrainRenderer {
public:
    void SetTileset(const Tileset* ts) { tileset_ = ts; }

    bool Render(const Buffer& mtxm, int mapWidth, int mapHeight, int pxPerTile, TerrainImage& out) const;

    // 只重绘 [tx0,tx1] × [ty0,ty1] 的瓦片区域（含端点）。
    // out 必须已按同一 pxPerTile 分配且尺寸匹配；用于撤销/重做后的局部刷新。
    bool RenderRegion(const Buffer& mtxm, int mapWidth, int mapHeight, int pxPerTile,
                      int tx0, int ty0, int tx1, int ty1, TerrainImage& out) const;

    // 写 24 位 BMP（用于自动化比对）
    static bool WriteBmp24(const std::wstring& path, const TerrainImage& image);

private:
    const Tileset* tileset_ = nullptr;
};

}  // namespace scmd
