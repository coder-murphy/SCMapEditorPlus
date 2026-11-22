#include "api/scmd_core.h"

#include <cstring>
#include <new>
#include <string>

#include "common/string_util.h"
#include "map/map_document.h"
#include "terrain/terrain_renderer.h"
#include "terrain/tileset_export.h"

static_assert(sizeof(scmd::UnitRecord) == sizeof(ScmdUnit), "ScmdUnit 必须与 UNIT 记录同布局");

struct ScmdMap {
    scmd::MapDocument doc;
};

namespace {

thread_local std::string g_lastError;

void ClearError() {
    g_lastError.clear();
}

void SetError(const std::wstring& msg) {
    g_lastError = scmd::WideToUtf8(msg);
}

void SetError(const char* msg) {
    g_lastError = msg ? msg : "";
}

bool CheckMap(ScmdMap* map) {
    if (!map || !map->doc.IsOpen()) {
        SetError("地图句柄无效或未打开");
        return false;
    }
    return true;
}

}  // namespace

extern "C" {

int32_t SCMD_CALL scmd_abi_version(void) {
    return SCMD_ABI_VERSION;
}

const char* SCMD_CALL scmd_last_error(void) {
    return g_lastError.c_str();
}

const char* SCMD_CALL scmd_era_name(int32_t era) {
    static const char* kNames[] = {
        "badlands", "platform", "install", "ashworld",
        "jungle", "desert", "ice", "twilight",
    };
    if (era < 0 || era >= 8) return "";
    return kNames[era];
}

ScmdMap* SCMD_CALL scmd_map_open(const char* path_utf8, const char* tileset_dir_utf8) {
    ClearError();
    if (!path_utf8 || !*path_utf8) {
        SetError("地图路径为空");
        return nullptr;
    }

    ScmdMap* map = new (std::nothrow) ScmdMap();
    if (!map) {
        SetError("内存不足");
        return nullptr;
    }

    const std::wstring path = scmd::Utf8ToWide(path_utf8);
    const std::wstring tsDir = tileset_dir_utf8 ? scmd::Utf8ToWide(tileset_dir_utf8) : std::wstring();
    if (!map->doc.Open(path, tsDir)) {
        SetError(std::wstring(L"打开地图失败: ") + path);
        delete map;
        return nullptr;
    }
    return map;
}

ScmdMap* SCMD_CALL scmd_map_create(int32_t width, int32_t height, int32_t era,
                                   const char* tileset_dir_utf8) {
    ClearError();
    ScmdMap* map = new (std::nothrow) ScmdMap();
    if (!map) {
        SetError("内存不足");
        return nullptr;
    }

    const std::wstring tsDir = tileset_dir_utf8 ? scmd::Utf8ToWide(tileset_dir_utf8) : std::wstring();
    if (!map->doc.CreateNew(width, height, static_cast<uint16_t>(era), tsDir)) {
        SetError("新建地图失败（尺寸需在 1..256 之间）");
        delete map;
        return nullptr;
    }
    return map;
}

void SCMD_CALL scmd_map_free(ScmdMap* map) {
    delete map;
}

int32_t SCMD_CALL scmd_map_save(ScmdMap* map, const char* path_utf8) {
    ClearError();
    if (!CheckMap(map)) return 0;
    if (!path_utf8 || !*path_utf8) {
        SetError("保存路径为空");
        return 0;
    }
    if (!map->doc.Save(scmd::Utf8ToWide(path_utf8))) {
        SetError("保存地图失败");
        return 0;
    }
    return 1;
}

int32_t SCMD_CALL scmd_map_info(ScmdMap* map, ScmdMapInfo* out) {
    ClearError();
    if (!out) {
        SetError("输出参数为空");
        return 0;
    }
    if (!CheckMap(map)) return 0;

    out->width = map->doc.width();
    out->height = map->doc.height();
    out->era = map->doc.era();
    out->unit_count = static_cast<int32_t>(map->doc.unitCount());
    out->has_tileset = map->doc.hasTileset() ? 1 : 0;
    return 1;
}

int32_t SCMD_CALL scmd_map_resize(ScmdMap* map, int32_t width, int32_t height,
                                  int32_t offset_x, int32_t offset_y, int32_t smooth_edges) {
    ClearError();
    if (!CheckMap(map)) return 0;
    if (!map->doc.Resize(width, height, offset_x, offset_y, smooth_edges != 0)) {
        SetError("重设尺寸失败");
        return 0;
    }
    return 1;
}

int32_t SCMD_CALL scmd_map_tile_at(ScmdMap* map, int32_t tx, int32_t ty, uint16_t* out_value) {
    ClearError();
    if (!out_value) {
        SetError("输出参数为空");
        return 0;
    }
    if (!CheckMap(map)) return 0;
    if (tx < 0 || ty < 0 || tx >= map->doc.width() || ty >= map->doc.height()) {
        SetError("瓦片坐标越界");
        return 0;
    }
    *out_value = map->doc.TileAt(tx, ty);
    return 1;
}

int32_t SCMD_CALL scmd_map_is_walkable(ScmdMap* map, int32_t tx, int32_t ty) {
    ClearError();
    if (!CheckMap(map)) return 0;
    return map->doc.IsWalkableTile(tx, ty) ? 1 : 0;
}

int32_t SCMD_CALL scmd_map_paint_tile(ScmdMap* map, int32_t tx, int32_t ty, uint16_t value) {
    ClearError();
    if (!CheckMap(map)) return 0;
    if (!map->doc.PaintTile(tx, ty, value)) {
        SetError("绘制瓦片失败（坐标越界或 MTXM 缺失）");
        return 0;
    }
    return 1;
}

int32_t SCMD_CALL scmd_map_paint_rect(ScmdMap* map, int32_t x0, int32_t y0,
                                      int32_t x1, int32_t y1, uint16_t value) {
    ClearError();
    if (!CheckMap(map)) return 0;
    if (!map->doc.PaintRect(x0, y0, x1, y1, value)) {
        SetError("矩形绘制失败（区域越界）");
        return 0;
    }
    return 1;
}

int32_t SCMD_CALL scmd_map_unit_count(ScmdMap* map) {
    ClearError();
    if (!CheckMap(map)) return 0;
    return static_cast<int32_t>(map->doc.unitCount());
}

int32_t SCMD_CALL scmd_map_unit_at(ScmdMap* map, int32_t index, ScmdUnit* out_unit) {
    ClearError();
    if (!out_unit) {
        SetError("输出参数为空");
        return 0;
    }
    if (!CheckMap(map)) return 0;
    const auto& units = map->doc.units();
    if (index < 0 || static_cast<std::size_t>(index) >= units.size()) {
        SetError("单位下标越界");
        return 0;
    }

    const scmd::UnitRecord& r = units[static_cast<std::size_t>(index)];
    std::memset(out_unit, 0, sizeof(*out_unit));
    out_unit->serial = r.serial;
    out_unit->x = r.x;
    out_unit->y = r.y;
    out_unit->type = r.type;
    out_unit->unused0 = r.unused0;
    out_unit->valid_state = r.validState;
    out_unit->valid_props = r.validProps;
    out_unit->owner = r.owner;
    out_unit->hp_percent = r.hpPercent;
    out_unit->shield_percent = r.shieldPercent;
    out_unit->energy_percent = r.energyPercent;
    out_unit->resource = r.resource;
    out_unit->stack_count = r.stackCount;
    out_unit->queue_count = r.queueCount;
    std::memcpy(out_unit->reserved, r.reserved, sizeof(out_unit->reserved));
    return 1;
}

int32_t SCMD_CALL scmd_map_place_unit(ScmdMap* map, int32_t tx, int32_t ty,
                                      uint16_t type, uint8_t owner, uint32_t resource) {
    ClearError();
    if (!CheckMap(map)) return 0;
    if (!map->doc.PlaceUnit(tx, ty, type, owner, resource)) {
        SetError("放置单位失败（坐标越界或该格不可行走）");
        return 0;
    }
    return 1;
}

int32_t SCMD_CALL scmd_map_remove_units_at(ScmdMap* map, int32_t tx, int32_t ty) {
    ClearError();
    if (!CheckMap(map)) return 0;
    return static_cast<int32_t>(map->doc.RemoveUnitsAtTile(tx, ty));
}

int32_t SCMD_CALL scmd_map_render_bmp(ScmdMap* map, const char* out_path_utf8,
                                      int32_t px_per_tile) {
    ClearError();
    if (!CheckMap(map)) return 0;
    if (!out_path_utf8 || !*out_path_utf8) {
        SetError("输出路径为空");
        return 0;
    }
    if (!map->doc.hasTileset()) {
        SetError("地形集未加载，无法渲染（请传入 tileset_dir）");
        return 0;
    }
    if (px_per_tile != 2 && px_per_tile != 4 && px_per_tile != 8 &&
        px_per_tile != 16 && px_per_tile != 32) {
        SetError("px_per_tile 仅支持 2/4/8/16/32");
        return 0;
    }

    scmd::TerrainRenderer renderer;
    renderer.SetTileset(&map->doc.tileset());
    scmd::TerrainImage image;
    if (!renderer.Render(map->doc.tiles(), map->doc.width(), map->doc.height(),
                         px_per_tile, image)) {
        SetError("地形渲染失败");
        return 0;
    }
    if (!scmd::TerrainRenderer::WriteBmp24(scmd::Utf8ToWide(out_path_utf8), image)) {
        SetError("写入 BMP 失败");
        return 0;
    }
    return 1;
}

int32_t SCMD_CALL scmd_extract_tilesets(const char* game_dir_utf8,
                                        const char* out_dir_utf8,
                                        ScmdExtractResult* out_result) {
    ClearError();
    if (out_result) std::memset(out_result, 0, sizeof(*out_result));
    if (!game_dir_utf8 || !*game_dir_utf8 || !out_dir_utf8 || !*out_dir_utf8) {
        SetError("game_dir / out_dir 不能为空");
        return 0;
    }

    const scmd::TilesetExportResult r =
        scmd::ExtractTilesets(scmd::Utf8ToWide(game_dir_utf8), scmd::Utf8ToWide(out_dir_utf8));

    if (out_result) {
        out_result->files = r.files;
        out_result->failed = r.failed;
        out_result->bytes = static_cast<uint64_t>(r.bytes);
    }
    if (!r.log.empty()) {
        g_lastError = scmd::WideToUtf8(r.log);
    }
    return (r.failed == 0 && r.files == 32) ? 1 : 0;
}

}  // extern "C"
