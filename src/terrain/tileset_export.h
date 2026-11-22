#pragma once
#include <cstddef>
#include <string>

namespace scmd {

// 从 StarDat.mpq / BrooDat.mpq 导出 8 套地形集四件套。
struct TilesetExportResult {
    int files = 0;
    int failed = 0;
    std::size_t bytes = 0;
    std::wstring log;
};

// gameDir 下需存在 StarDat.mpq 与 BrooDat.mpq；outDir 不存在时自动创建。
TilesetExportResult ExtractTilesets(const std::wstring& gameDir, const std::wstring& outDir);

}  // namespace scmd
