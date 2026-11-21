#pragma once
#include <string>

#include "chk/chk_document.h"
#include "common/types.h"
#include "terrain/tileset.h"
#include "triggers/trigger_model.h"
#include "units/unit_model.h"

namespace scmd {

// 地图文档：负责 .scm/.scx 的打开与保存，装配 archive + chk + terrain + units
class MapDocument {
public:
    // 打开：MPQ -> scenario.chk -> DIM/ERA/MTXM/UNIT
    // tilesetDir 为空时只打开文档、不渲染
    bool Open(const std::wstring& path, const std::wstring& tilesetDir);
    bool Open(const std::wstring& path) { return Open(path, std::wstring()); }
    // 新建空白地图：只写 DIM/ERA/MTXM（全 0，可行走），单位表为空
    bool CreateNew(int width, int height, u16 era, const std::wstring& tilesetDir);
    bool Save(const std::wstring& path);
    // 更改地图尺寸：旧内容按 (offsetX, offsetY) 平移进新画布，越界单位丢弃
    bool Resize(int width, int height, int offsetX, int offsetY, bool smoothEdges);
    // 关闭文档并释放地形集（path 清空）
    void Close();

    bool IsOpen() const { return opened_; }
    const std::wstring& path() const { return path_; }

    int width() const { return width_; }
    int height() const { return height_; }
    u16 era() const { return era_; }
    // ERA 对应的地形集名称（静态字符串，不会失效）
    const wchar_t* eraName() const;

    const Buffer& tiles() const { return tiles_; }
    Buffer& mutableTiles() { return tiles_; }

    const Tileset& tileset() const { return tileset_; }
    bool hasTileset() const { return tileset_.IsLoaded(); }

    const TriggerSection& triggers() const { return triggers_; }
    TriggerSection& mutableTriggers() { return triggers_; }
    const LocationTable& locations() const { return locations_; }
    const StringTable& strings() const { return strings_; }
    StringTable& mutableStrings() { return strings_; }
    LocationTable& mutableLocations() { return locations_; }

    // 原始段内容（用于无损重建校验）
    const ChkDocument& chk() const { return chk_; }

    const std::vector<UnitRecord>& units() const { return units_; }
    std::vector<UnitRecord>& mutableUnits() { return units_; }
    std::size_t unitCount() const { return units_.size(); }

    bool IsDirty() const { return dirty_; }

    // ── 编辑原语（P2）──
    u16 TileAt(int tx, int ty) const;
    // 地形集未加载时无法校验，返回 true（不阻断编辑）
    bool IsWalkableTile(int tx, int ty) const;

    bool PaintTile(int tx, int ty, u16 value);
    bool PaintRect(int x0, int y0, int x1, int y1, u16 value);

    // 单位锚点规则（VERIFIED）：
    //   矿物 176/177/178 -> (tx*32, ty*32+16)
    //   气矿 188        -> (tx*32, ty*32)
    //   其它（含出生点） -> (tx*32, ty*32+16)
    bool PlaceUnit(int tx, int ty, u16 type, u8 owner, u32 resource);
    // 删除像素落点所在格的全部单位，返回删除数量
    int RemoveUnitsAtTile(int tx, int ty);
    // 返回该格上的单位数
    int UnitCountAtTile(int tx, int ty) const;
    // 标记脏数据（编辑操作调用），Save 成功后清零
    void MarkDirty() { dirty_ = true; }

private:
    std::wstring path_;
    ChkDocument chk_;
    Tileset tileset_;

    int width_ = 0;
    int height_ = 0;
    u16 era_ = 0;                  // 0..7，越界按 0 处理
    Buffer tiles_;                 // MTXM 原始字节（u16 小端）
    std::vector<UnitRecord> units_;
    TriggerSection triggers_;
    LocationTable locations_;
    StringTable strings_;
    bool opened_ = false;
    bool dirty_ = false;
};

}  // namespace scmd
