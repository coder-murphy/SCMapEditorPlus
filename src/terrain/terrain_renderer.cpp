#include "terrain/terrain_renderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace scmd {

namespace {
u16 ReadU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
}

bool TerrainRenderer::Render(const Buffer& mtxm, int mapWidth, int mapHeight,
                             int pxPerTile, TerrainImage& out) const {
    if (!tileset_ || !tileset_->IsLoaded()) return false;
    if (mapWidth <= 0 || mapHeight <= 0) return false;
    if (pxPerTile < 1 || pxPerTile > 128) return false;

    out.width = mapWidth * pxPerTile;
    out.height = mapHeight * pxPerTile;
    out.pixels.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0);
    return RenderRegion(mtxm, mapWidth, mapHeight, pxPerTile, 0, 0, mapWidth - 1, mapHeight - 1,
                        out);
}

bool TerrainRenderer::RenderRegion(const Buffer& mtxm, int mapWidth, int mapHeight,
                                  int pxPerTile, int tx0, int ty0, int tx1, int ty1,
                                  TerrainImage& out) const {
    if (!tileset_ || !tileset_->IsLoaded()) return false;
    if (mapWidth <= 0 || mapHeight <= 0) return false;
    if (pxPerTile < 1 || pxPerTile > 128) return false;

    const std::size_t tileCount = static_cast<std::size_t>(mapWidth) * mapHeight;
    if (mtxm.size() < tileCount * 2) return false;
    if (out.width != mapWidth * pxPerTile || out.height != mapHeight * pxPerTile) return false;
    if (out.pixels.size() != static_cast<std::size_t>(out.width) * out.height * 4) return false;

    tx0 = std::max(0, tx0);
    ty0 = std::max(0, ty0);
    tx1 = std::min(mapWidth - 1, tx1);
    ty1 = std::min(mapHeight - 1, ty1);
    if (tx0 > tx1 || ty0 > ty1) return true;

    const Buffer& wpe = tileset_->palette();
    if (wpe.size() < 256 * 4) return false;

    std::unordered_map<u16, std::vector<u8>> blockCache;
    blockCache.reserve(512);

    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            const u16 v = ReadU16(mtxm.data() + (static_cast<std::size_t>(ty) * mapWidth + tx) * 2);
            auto it = blockCache.find(v);
            if (it == blockCache.end()) {
                std::vector<u8> block(1024, 0);
                tileset_->TileBlock(v, block.data());
                it = blockCache.emplace(v, std::move(block)).first;
            }
            const std::vector<u8>& block = it->second;

            const int dstX0 = tx * pxPerTile;
            const int dstY0 = ty * pxPerTile;
            for (int py = 0; py < pxPerTile; py++) {
                // 最近邻：缩小取整数步长，放大时相邻目标像素复用同一源像素
                const int sy = (py * 32) / pxPerTile;
                u8* dstRow = out.pixels.data() +
                             (static_cast<std::size_t>(dstY0 + py) * out.width + dstX0) * 4;
                const u8* srcRow = block.data() + sy * 32;
                for (int px = 0; px < pxPerTile; px++) {
                    const int sx = (px * 32) / pxPerTile;
                    const u8 index = srcRow[sx];
                    const u8* rgb = wpe.data() + static_cast<std::size_t>(index) * 4;
                    // WPE 为 RGB(A) 顺序，DIB 需要 BGRA
                    dstRow[px * 4 + 0] = rgb[2];
                    dstRow[px * 4 + 1] = rgb[1];
                    dstRow[px * 4 + 2] = rgb[0];
                    dstRow[px * 4 + 3] = 0xFF;
                }
            }
        }
    }
    return true;
}

bool TerrainRenderer::WriteBmp24(const std::wstring& path, const TerrainImage& image) {
    if (image.width <= 0 || image.height <= 0) return false;
    const int rowBytes = image.width * 3;
    const int pad = (4 - (rowBytes % 4)) % 4;
    const int stride = rowBytes + pad;
    const u32 dataSize = static_cast<u32>(stride) * static_cast<u32>(image.height);
    const u32 fileSize = 54 + dataSize;

    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;

    u8 header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &fileSize, 4);
    const u32 offset = 54;
    std::memcpy(header + 10, &offset, 4);
    const u32 headerSize = 40;
    std::memcpy(header + 14, &headerSize, 4);
    const int w = image.width, h = image.height;
    std::memcpy(header + 18, &w, 4);
    std::memcpy(header + 22, &h, 4);
    const u16 planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &dataSize, 4);
    out.write(reinterpret_cast<const char*>(header), 54);

    std::vector<u8> row(stride, 0);
    for (int y = image.height - 1; y >= 0; y--) {          // BMP 自下而上
        const u8* src = image.pixels.data() + static_cast<std::size_t>(y) * image.width * 4;
        for (int x = 0; x < image.width; x++) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        out.write(reinterpret_cast<const char*>(row.data()), stride);
    }
    return out.good();
}

}  // namespace scmd
