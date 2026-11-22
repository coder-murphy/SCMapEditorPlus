/*
 * SCMDCore C ABI
 * ────────────────────────────────────────────────────────────────
 * 面向 CLI / WPF / Python 等外部调用者的稳定 C 接口。
 *  - 所有路径与字符串均为 UTF-8（不带 BOM），宽字符只在 DLL 内部使用；
 *  - 失败时返回 0 / NULL，具体原因用 scmd_last_error() 获取（线程局部）；
 *  - 句柄不跨线程共享；用 scmd_map_free() 释放，不要自行 delete。
 *
 * C# / WPF 示例：
 *   [DllImport("SCMDCore.dll", CallingConvention = CallingConvention.Cdecl)]
 *   static extern IntPtr scmd_map_open(
 *       [MarshalAs(UnmanagedType.LPUTF8Str)] string path,
 *       [MarshalAs(UnmanagedType.LPUTF8Str)] string tilesetDir);
 */
#ifndef SCMD_CORE_H
#define SCMD_CORE_H

#include <stdint.h>

#ifdef _WIN32
#  ifdef SCMDCORE_EXPORTS
#    define SCMD_API __declspec(dllexport)
#  else
#    define SCMD_API __declspec(dllimport)
#  endif
#  define SCMD_CALL __cdecl
#else
#  define SCMD_API
#  define SCMD_CALL
#endif

#define SCMD_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明地图句柄 */
typedef struct ScmdMap ScmdMap;

/* 与 UNIT 段 36 字节记录一一对应；C# 用 LayoutKind.Sequential */
typedef struct ScmdUnit {
    uint32_t serial;
    uint16_t x;            /* 像素坐标 */
    uint16_t y;
    uint16_t type;
    uint16_t unused0;
    uint16_t valid_state;
    uint16_t valid_props;
    uint8_t owner;
    uint8_t hp_percent;
    uint8_t shield_percent;
    uint8_t energy_percent;
    uint32_t resource;
    uint16_t stack_count;
    uint16_t queue_count;
    uint8_t reserved[8];
} ScmdUnit;

typedef struct ScmdMapInfo {
    int32_t width;
    int32_t height;
    int32_t era;
    int32_t unit_count;
    int32_t has_tileset;
} ScmdMapInfo;

typedef struct ScmdExtractResult {
    int32_t files;
    int32_t failed;
    uint64_t bytes;
} ScmdExtractResult;

/* ── 版本与错误 ── */
SCMD_API int32_t SCMD_CALL scmd_abi_version(void);
SCMD_API const char* SCMD_CALL scmd_last_error(void);
SCMD_API const char* SCMD_CALL scmd_era_name(int32_t era);  /* ASCII，越界返回 "" */

/* ── 地图文档 ──
 * tileset_dir 可为 NULL/空：此时只解析文档，不做可通行校验与渲染。
 * 返回的句柄需用 scmd_map_free 释放。
 */
SCMD_API ScmdMap* SCMD_CALL scmd_map_open(const char* path_utf8, const char* tileset_dir_utf8);
SCMD_API ScmdMap* SCMD_CALL scmd_map_create(int32_t width, int32_t height, int32_t era,
                                            const char* tileset_dir_utf8);
SCMD_API void SCMD_CALL scmd_map_free(ScmdMap* map);

/* 另存为 .scm/.scx；成功返回 1 */
SCMD_API int32_t SCMD_CALL scmd_map_save(ScmdMap* map, const char* path_utf8);
SCMD_API int32_t SCMD_CALL scmd_map_info(ScmdMap* map, ScmdMapInfo* out);
SCMD_API int32_t SCMD_CALL scmd_map_resize(ScmdMap* map, int32_t width, int32_t height,
                                           int32_t offset_x, int32_t offset_y, int32_t smooth_edges);

/* ── 地形编辑 ── */
SCMD_API int32_t SCMD_CALL scmd_map_tile_at(ScmdMap* map, int32_t tx, int32_t ty, uint16_t* out_value);
SCMD_API int32_t SCMD_CALL scmd_map_is_walkable(ScmdMap* map, int32_t tx, int32_t ty);
SCMD_API int32_t SCMD_CALL scmd_map_paint_tile(ScmdMap* map, int32_t tx, int32_t ty, uint16_t value);
SCMD_API int32_t SCMD_CALL scmd_map_paint_rect(ScmdMap* map, int32_t x0, int32_t y0,
                                               int32_t x1, int32_t y1, uint16_t value);

/* ── 单位 ── */
SCMD_API int32_t SCMD_CALL scmd_map_unit_count(ScmdMap* map);
SCMD_API int32_t SCMD_CALL scmd_map_unit_at(ScmdMap* map, int32_t index, ScmdUnit* out_unit);
SCMD_API int32_t SCMD_CALL scmd_map_place_unit(ScmdMap* map, int32_t tx, int32_t ty,
                                               uint16_t type, uint8_t owner, uint32_t resource);
SCMD_API int32_t SCMD_CALL scmd_map_remove_units_at(ScmdMap* map, int32_t tx, int32_t ty);

/* ── 渲染与资源导出 ── */
/* px_per_tile 仅支持 2/4/8/16/32；输出 24 位 BMP */
SCMD_API int32_t SCMD_CALL scmd_map_render_bmp(ScmdMap* map, const char* out_path_utf8,
                                               int32_t px_per_tile);

/* 从 game_dir 下的 StarDat.mpq / BrooDat.mpq 导出 8 套地形集到 out_dir */
SCMD_API int32_t SCMD_CALL scmd_extract_tilesets(const char* game_dir_utf8,
                                                 const char* out_dir_utf8,
                                                 ScmdExtractResult* out_result);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* SCMD_CORE_H */
