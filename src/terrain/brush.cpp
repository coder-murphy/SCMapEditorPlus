#include "terrain/brush.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "common/string_util.h"

namespace scmd {

namespace {

constexpr char kMagic[8] = {'S', 'C', 'M', 'D', 'B', 'R', 'S', 'H'};
constexpr u32 kVersion = 1;
constexpr std::size_t kMaxFileSize = 16u * 1024u * 1024u;
constexpr u32 kMaxBrushes = 1024;
constexpr int kMaxBrushSide = 256;

void AppendU16(Buffer& out, u16 v) {
    out.push_back(static_cast<u8>(v & 0xFF));
    out.push_back(static_cast<u8>(v >> 8));
}

void AppendU32(Buffer& out, u32 v) {
    out.push_back(static_cast<u8>(v & 0xFF));
    out.push_back(static_cast<u8>((v >> 8) & 0xFF));
    out.push_back(static_cast<u8>((v >> 16) & 0xFF));
    out.push_back(static_cast<u8>((v >> 24) & 0xFF));
}

u16 ReadU16(const Buffer& data, std::size_t at) {
    return static_cast<u16>(data[at] | (data[at + 1] << 8));
}

u32 ReadU32(const Buffer& data, std::size_t at) {
    return static_cast<u32>(data[at]) | (static_cast<u32>(data[at + 1]) << 8) |
           (static_cast<u32>(data[at + 2]) << 16) | (static_cast<u32>(data[at + 3]) << 24);
}

}  // namespace

void TerrainBrush::SetSingle(u16 value, const std::wstring& label) {
    width = 1;
    height = 1;
    tiles.assign(1, value);
    name = label;
}

void TerrainBrush::SetFromTiles(int w, int h, std::vector<u16> data, const std::wstring& label) {
    width = std::max(1, std::min(w, kMaxBrushSide));
    height = std::max(1, std::min(h, kMaxBrushSide));
    tiles = std::move(data);
    tiles.resize(static_cast<std::size_t>(width) * height, 0);
    name = label;
}

bool SaveBrushes(const std::wstring& path, const std::vector<TerrainBrush>& brushes) {
    if (brushes.size() > kMaxBrushes) return false;

    Buffer out;
    out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
    AppendU32(out, kVersion);
    AppendU32(out, static_cast<u32>(brushes.size()));

    for (const TerrainBrush& brush : brushes) {
        if (brush.width < 1 || brush.height < 1 ||
            brush.width > kMaxBrushSide || brush.height > kMaxBrushSide) {
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(brush.width) * brush.height;
        if (brush.tiles.size() != count) return false;

        const std::string name = WideToUtf8(brush.name);
        if (name.size() > 4096) return false;
        AppendU32(out, static_cast<u32>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        AppendU32(out, static_cast<u32>(brush.width));
        AppendU32(out, static_cast<u32>(brush.height));
        for (u16 value : brush.tiles) AppendU16(out, value);
    }

    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return f.good();
}

bool LoadBrushes(const std::wstring& path, std::vector<TerrainBrush>& brushes) {
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff size = f.tellg();
    if (size < 16 || static_cast<std::size_t>(size) > kMaxFileSize) return false;
    f.seekg(0, std::ios::beg);

    Buffer data(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) return false;

    if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) return false;
    std::size_t at = sizeof(kMagic);
    if (ReadU32(data, at) != kVersion) return false;
    at += 4;
    const u32 count = ReadU32(data, at);
    at += 4;
    if (count > kMaxBrushes) return false;

    std::vector<TerrainBrush> loaded;
    loaded.reserve(count);
    for (u32 i = 0; i < count; i++) {
        if (at + 4 > data.size()) return false;
        const u32 nameLen = ReadU32(data, at);
        at += 4;
        if (nameLen > 4096 || at + nameLen > data.size()) return false;
        std::string name(reinterpret_cast<const char*>(data.data() + at), nameLen);
        at += nameLen;

        if (at + 8 > data.size()) return false;
        const u32 w = ReadU32(data, at);
        const u32 h = ReadU32(data, at + 4);
        at += 8;
        if (w < 1 || h < 1 || w > kMaxBrushSide || h > kMaxBrushSide) return false;
        const std::size_t tileCount = static_cast<std::size_t>(w) * h;
        if (at + tileCount * 2 > data.size()) return false;

        TerrainBrush brush;
        brush.name = Utf8ToWide(name);
        brush.width = static_cast<int>(w);
        brush.height = static_cast<int>(h);
        brush.tiles.resize(tileCount);
        for (std::size_t t = 0; t < tileCount; t++) {
            brush.tiles[t] = ReadU16(data, at);
            at += 2;
        }
        loaded.push_back(std::move(brush));
    }

    brushes = std::move(loaded);
    return true;
}

}  // namespace scmd
