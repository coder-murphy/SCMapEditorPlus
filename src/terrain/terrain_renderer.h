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

// 把 MTXM 瓦片表渲染成位图；pxPerTile 支持 2/4/8/16/32
class TerrainRenderer {
public:
    void SetTileset(const Tileset* ts) { tileset_ = ts; }

    bool Render(const Buffer& mtxm, int mapWidth, int mapHeight, int pxPerTile, TerrainImage& out) const;

    // 写 24 位 BMP（用于自动化比对）
    static bool WriteBmp24(const std::wstring& path, const TerrainImage& image);

private:
    const Tileset* tileset_ = nullptr;
};

}  // namespace scmd
