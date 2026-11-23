#pragma once
#include <string>

#include "archive/mpq_archive.h"
#include "chk/chk_document.h"
#include "common/types.h"
#include "map/decor_model.h"
#include "map/scenario_tables.h"
#include "players/player_model.h"
#include "players/restriction_model.h"
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

    // MPQ 中除 scenario.chk 外的文件（自定义 BIN / 瓦片 / 音频等），保存时原样带回
    std::size_t ExtraFileCount() const { return extraFiles_.size(); }
    const std::vector<MpqFileEntry>& extraFiles() const { return extraFiles_; }
    // 附加文件管理（167 对话框使用）：名称按不区分大小写匹配
    bool ReadExtraFile(const std::string& name, Buffer& out) const;
    // 添加或替换；内部文件名（(listfile) 等）与 scenario.chk 一律拒绝
    bool SetExtraFile(const std::string& name, Buffer data);
    bool RemoveExtraFile(const std::string& name);

    const TriggerSection& triggers() const { return triggers_; }
    TriggerSection& mutableTriggers() { return triggers_; }
    const LocationTable& locations() const { return locations_; }
    const StringTable& strings() const { return strings_; }
    StringTable& mutableStrings() { return strings_; }
    LocationTable& mutableLocations() { return locations_; }

    // OWNR / SIDE / FORC / COLR
    const PlayerRoster& players() const { return players_; }
    PlayerRoster& mutablePlayers() { return players_; }

    // UPGR / PTEC / PUPx / PTEx
    const UpgradeTechRestrictions& restrictions() const { return restrictions_; }
    UpgradeTechRestrictions& mutableRestrictions() { return restrictions_; }

    // DD2 装饰物 / MASK 战争迷雾
    const DoodadTable& doodads() const { return doodads_; }
    DoodadTable& mutableDoodads() { return doodads_; }
    const FogMask& fog() const { return fog_; }
    FogMask& mutableFog() { return fog_; }
    const SpriteTable& sprites() const { return sprites_; }
    SpriteTable& mutableSprites() { return sprites_; }

    // WAV 声音 / SWNM 开关名 / MBRF 任务简报
    const StringIndexTable& sounds() const { return sounds_; }
    StringIndexTable& mutableSounds() { return sounds_; }
    const StringIndexTable& switchNames() const { return switchNames_; }
    StringIndexTable& mutableSwitchNames() { return switchNames_; }
    const TriggerSection& briefing() const { return briefing_; }
    TriggerSection& mutableBriefing() { return briefing_; }

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
    bool PlaceUnit(int tx, int ty, u16 type, u8 owner, u32 resource, bool force = false);
    // 删除像素落点所在格的全部单位，返回删除数量
    int RemoveUnitsAtTile(int tx, int ty);
    // 删除越界与重复 serial 的单位（类型合法性需单位属性表，暂不处理）
    int RemoveInvalidUnits();
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
    PlayerRoster players_;
    UpgradeTechRestrictions restrictions_;
    DoodadTable doodads_;
    FogMask fog_;
    SpriteTable sprites_;
    StringIndexTable sounds_{512};
    StringIndexTable switchNames_{256};
    TriggerSection briefing_;
    std::vector<MpqFileEntry> extraFiles_;
    bool opened_ = false;
    bool dirty_ = false;
};

}  // namespace scmd
