# UNIT 段记录布局

36 字节/条（VERIFIED，与星际 1.16 地图一致）。

| 偏移 | 类型 | 含义 |
|---|---|---|
| +0 | u32 | 序列号 |
| +4 | u16 | x（像素；1 瓦片 = 32 像素） |
| +6 | u16 | y |
| +8 | u16 | 单位类型 |
| +10 | u16 | 未使用（原样保留，实测可能非零） |
| +12 | u16 | 有效状态（0x0018 常规） |
| +14 | u16 | 有效属性（0x0012 资源 / 0x0002 出生点） |
| +16 | u8 | 所有者（11 = 中立） |
| +17 | u8 | HP% |
| +18 | u8 | 护盾% |
| +19 | u8 | 能量% |
| +20 | u32 | 资源量（VERIFIED：直读值；普通矿常见 1500/3000，气矿 5000，无限矿 50000） |
| +24 | u16 | 堆叠数 |
| +26 | u16 | 队列数 |
| +28 | 8 B | 保留区（原样保留，不能置零） |

## 常用单位类型

| ID | 含义 |
|---|---|
| 176 / 177 / 178 | 矿（三种朝向） |
| 188 | 气矿 |
| 214 | 出生点 |

## 放置约定（VERIFIED，来自原版地图实测）

- 出生点、矿：锚在瓦片左边缘、垂直居中 → `(32*tx, 32*ty + 16)`
- 气矿：锚在瓦片角 → `(32*tx, 32*ty)`
- 单位坐标必须落在可行走格上，否则地图会出现“单位在虚空里”的问题，需在生成后统一校验。

## 生成后固定校验项

1. 可行走格数与几何模型一致；
2. 所有单位（含矿/气）落在可行走格；
3. 从 1 号出生点洪泛，能到达全部出生点；
4. 容器用 `SFmpq.dll`（32 位）过一遍。

## 解析实现状态（P0-2）

`MapDocument::Open` 已按上表偏移解析 `UNIT` 段到 `std::vector<UnitRecord>`，
并在标题栏显示单位总数。`resource`（+20）为直读资源量；P2-7 的写回已实现。

P3-11d 补充：`+10` 与 `+28..+35` 保留区改为原样读写。此前保存会把它们置零，
带自定义单位属性的地图会因此丢数据。

未做：放置/删除工具的坐标一致性检查（P2-7 已实现可行走校验）。

## 编辑能力（P2-7，VERIFIED）

`MapDocument` 新增三个原语：

| 方法 | 行为 |
|---|---|
| `PlaceUnit(tx, ty, type, owner, resource)` | 先校验目标格可行走（`Tileset::IsWalkable`），再按锚点规则写入；`serial` 取现有最大值 +1 |
| `RemoveUnitsAtTile(tx, ty)` | 反解像素坐标到格（`x/32`、`y/32`），删除该格全部单位，返回删除数 |
| `UnitCountAtTile(tx, ty)` | 统计该格单位数 |

锚点规则（与生成器一致）：

| 类型 | 坐标 |
|---|---|
| 矿物 176/177/178 | `(tx*32, ty*32+16)` |
| 气矿 188 | `(tx*32, ty*32)` |
| 其它（含出生点 214） | `(tx*32, ty*32+16)` |

地形集未加载时 `IsWalkableTile` 返回 true（无法校验就不阻断），加载后严格校验。

无头回归（`--edit-test`，Space Circuit）：

```
base=144 placed=3 removed=1 after=146 expected=146
painted=1 repaint=3/3 blockedRejected=1 saved=1 reopened=1 countOk=1
```

保存后 CHK 从 369 947 → 370 019 字节，差值 **72 = 2 条 × 36 字节**，与"加 3 删 1"完全吻合；
SFmpq.dll 读取 PASS。

GUI 实测：切到单位工具后在中心枢纽点击，标题从 `144 个单位` 变为 `145 个单位，已修改`。

## units.dat 属性表（VERIFIED）

`arr\units.dat` 是**列式存储**（同一字段的 228 个值连续排列），不是记录数组。
标准 BrooDat 版本 19 876 B / 228 项，`mpq/BrooDat.mpq` 单块 implode（类型 0x08）
解出。各列偏移：

| 偏移 | 类型 | 字段 | 示例校验 |
|---|---|---|---|
| 0x0000 | u8 | Graphic（图像 ID） | Marine = 78 |
| 0x09A8 | u8 | ShieldEnable | Marine 0、Probe 1 |
| 0x0A8C | u16 | ShieldAmount | Probe 20、Archon 350 |
| 0x0C54 | u32 | HP（低 24 位 / 256） | Marine 40、Goliath 125 |
| 0x10C8 | u8 | MovementType | |
| 0x1704 | u16 | WeaponGround | |
| 0x18CC | u16 | WeaponAir | |
| 0x1B78 | u32 | SpecialFlags | |
| 0x1FEC | u16 | Sight | |
| 0x21B4 | u8 | Size | |
| 0x2298 | u8 | Armor | Goliath 1 |
| 0x3124 | u16 | Width（像素） | Marine 8 |
| 0x32EC | u16 | Height（像素） | Marine 25 |
| 0x34B4 | u16 | CircleWidth | |
| 0x367C | u16 | CircleHeight | |
| 0x3844 | u16 | Portrait | |
| 0x3A0C | u16 | Minerals | Marine 50 |
| 0x3BD4 | u16 | Gas | |
| 0x3D9C | u16 | BuildTime | Marine 360 |
| 0x4AF8 | u8 | BroodWar | |

护盾值只在 `ShieldEnable` 置位时有效；未启用的单位该列是垃圾数据。

实现：`UnitDataTable`（`src/units/unit_data.*`）按上表解析，`LoadAuto` 依次尝试
星际目录 / exe 目录 / 工程根的 `arr\units.dat`，再退到 `mpq/BrooDat.mpq`。
无头回归 `--unit-data-test`：`units=228 marine=40/0/0 8x25 goliath=125/0/1
probe=20/20 reaver=100/80 archon=10/350`。
