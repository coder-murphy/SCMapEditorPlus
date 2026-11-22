#pragma once

namespace scmd {

class MapDocument;

// Map Revealer（小雷达）单位类型与“全地图放置”网格步长（瓦片）。
// 单位视野约 10 格；16 格步长可覆盖整图，256×256 时为 16×16 个/玩家。
constexpr int kMapRevealerUnitType = 101;
constexpr int kMapRevealerGridStep = 16;
// 星际单位数上限附近，放置时超过即停止
constexpr int kMapUnitSoftLimit = 1650;

// player: 0..7，-1 表示所有玩家（0..7）
// 返回新增 / 删除的单位数量
int PlaceMapRevealers(MapDocument& doc, int player);
int RemoveMapRevealers(MapDocument& doc, int player);

// 设置整图迷雾位；player = -1 时设置全部 8 位
// 返回是否有任意格发生变化
bool SetFogForPlayers(MapDocument& doc, int player, bool fogged);

// 矿物外形随机：176..178 之间随机，返回类型发生变化的数量
int RandomizeMineralTypes(MapDocument& doc);

}  // namespace scmd
