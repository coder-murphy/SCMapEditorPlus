# CHK 段结构

## 顶层布局（VERIFIED）

```
[4 字节段名][u32 小端长度][数据]
```

段名区分大小写且**含尾部空格**（如 `DIM `、`ERA `、`STR `）。读到未知段必须原样保留，
否则地图会在编辑器里丢数据。

## 常见段（按实测地图出现顺序）

```
TYPE VER  IVE2 VCOD IOWN OWNR ERA  DIM  SIDE MTXM PUNI UPGR UNIT PTEC ISOM TILE
DD2  THG2 MASK STR  MRGN SPRP FORC WAV  UNIS UPGS TECS COLR PUPx PTEx UNIx UPGx
TECx TRIG MBRF UPRP UPUS SWNM
```

| 段 | 内容要点 |
|---|---|
| `DIM ` | u16 宽 + u16 高（单位：瓦片） |
| `ERA ` | u16 地形集 0..7 |
| `MTXM` | u16 瓦片索引数组，长度 = 宽×高×2 |
| `TILE` | 旧版瓦片段，内容与 MTXM 相同（写盘时两者保持一致） |
| `ISOM` | 编辑器用等距地形；长度必须 = `8 × (宽/2 + 1) × (高 + 1)`，缺失会被当成“受保护的残图” |
| `MASK` | 每格 1 字节可通行掩码 |
| `UNIT` | 36 字节/条，见 `doc/04-units/` |
| `MRGN` | 地点（区域）矩形，见下方注意项 |
| `STR ` | 字符串表，供触发器/地点名使用 |

## 两条踩坑记录（VERIFIED）

1. **MRGN 不要从模板继承。** 模板里多余的地点记录会在 ScmDraft2 里显示成一片游离的矩形框；
   自动生成时应把 `MRGN` 置为空段。
2. **ISOM 长度必须精确。** 只替换 `MTXM` 而漏写/写错 `ISOM`，编辑器会认为地图被剥离过。

## 只改地形的最小改动集

替换 `MTXM`（并同步 `TILE`），其余段原样透传 —— 这样玩家配置、触发器、单位都不会被破坏，
实测可被 ScmDraft2 正常打开。

## 文档层实现状态（P0-2）

`src/map/map_document.cpp` 的 `Open` 已闭环（VERIFIED，4 张真实地图）：

```
MpqArchive::Open -> ReadScenarioChk -> ChkDocument::Parse
  -> DIM  (宽/高，16..256 校验)
  -> ERA  (0..7，越界按 0)
  -> MTXM (缺失时退回 TILE)
  -> UNIT (36 字节/条 -> std::vector<UnitRecord>)
```

主窗口在载入成功后刷新标题栏：

```
SCMD Plus 3.0 - <文件名>  [<宽>x<高>  ERA <n> <地形集>  <单位数> 个单位]
```

实测结果：

| 地图 | 标题信息 |
|---|---|
| (8)Space Circuit.scx | 192x192 ERA 1 Space 144 个单位 |
| (8)Astro World v1.3.scx | 192x192 ERA 1 Space 407 个单位 |
| (8)Mangosteen v1.0.scx | 192x192 ERA 0 Badlands 304 个单位 |
| (6)Neo China.scx | 192x64 ERA 4 Jungle 186 个单位 |

调试入口：`SCMDPlus.exe --open="<地图>"` 启动即载入（窗口保持打开）。

未做：`Save`（P1-5）、MTXM 与 UNIT 的编辑与校验（P2）。

## 保存（P1-5，VERIFIED）

`MapDocument::Save(path)` 的流程：

```
MTXM（原图有则写 MTXM，否则写 TILE）
UNIT（36 字节/条；偏移 10 与 28..35 保留为 0）
  -> ChkDocument::Serialize()
  -> MpqArchive::WriteArchive（(listfile) + staredit\\scenario.chk）
```

未修改的文档保存后 CHK **逐字节相同**（3 张地图实测 0 差异），说明段顺序与段内容都被完整保留。
编辑操作需调用 `MarkDirty()`，保存成功后清空该标记。

- `UNIT` 类型 101 为 `Map Revealer`（小雷达）：剧本设置 →“全地图放置小雷达”
  按 16 格网格写入 UNIT（同一玩家同位置不重复），删除命令按玩家移除该类型。

## 玩家 / 势力四段（P1，VERIFIED）

实现：`src/players/player_model.h/.cpp`，`MapDocument` 在 Open 时解析、修改后 Save 回写。

| 段 | 长度 | 结构 |
|---|---|---|
| `OWNR` | 12 | 每个槽 1 字节控制者：0 未启用 / 1 电脑(游戏) / 2 人类 / 3 Rescue Passive / 4 未使用 / 5 电脑 / 6 人类(开放槽) / 7 中立 / 8 关闭槽 |
| `SIDE` | 12 | 每个槽 1 字节种族：0 虫 / 1 人 / 2 神 / 3 未知(独立) / 4 未知(中立) / 5 用户选择 / 6 随机 / 7 不活动 |
| `FORC` | 20 | `u8[8]` 玩家→Force；`u16[4]` Force 名称 STR 下标；`u8[4]` Force 标志 |
| `COLR` | 8 | 前 8 名玩家的颜色 0..15 |

- 12 个 OWNR/SIDE 槽位 = 8 名玩家 + 4 个中立/关闭槽；颜色和 Force 分配只覆盖前 8 名。
- `MapDocument::Save` 仅在 `PlayerRoster::Modified()` 时重写四段；未修改地图保持原始字节透传。
- `UPGR`（1748 B）、`PTEC`（912 B）、`PUPx`/`PTEx` 暂未建模，按未知段原样保留。

无头回归（真实地图）：

```
SCMDPlus.exe --open="<地图>" --player-test="<输出.scx>"
open=OK parsed=1 undo=1 redo=1 saved=1 reopened=1 fields=1 preserved=1
```

`preserved=1` 表示 UPGR/DIM/MTXM 等未编辑段在修改玩家配置后仍逐字节一致。

## 升级/科技限制四段（P1，VERIFIED）

实现：`src/players/restriction_model.h/.cpp`。四段共用布局：

```
u8[12][N] 玩家 primary     （升级最大等级 / 科技可用）
u8[12][N] 玩家 secondary   （升级初始等级 / 科技初始拥有）
u8[N]     全局 primary
u8[N]     全局 secondary
u8[12][N] 玩家 useDefaults （1 = 使用全局默认）
```

| 段 | N | 长度 | 说明 |
|---|---|---|---|
| `UPGR` | 46 | 1748 | 原版升级限制 |
| `PTEC` | 24 | 912 | 原版科技限制 |
| `PUPx` | 61 | 2318 | 资料片升级限制 |
| `PTEx` | 44 | 1672 | 资料片科技限制 |

- 打开时四段独立解析，长度不匹配的段原样透传。
- 197 对话框按地图是否存在 BW 段自动选择数据版本，也可手动切换。
- 仅修改过的段在保存时重写；其余段保持原字节。

无头回归：

```
SCMDPlus.exe --open="<地图>" --restriction-test="<输出.scx>"
open=OK parsed=1 undo=1 redo=1 saved=1 reopened=1 fields=1 sizes=1 preserved=1
```

测试会同时创建 UPGR/PTEC/PUPx/PTEx，校验长度、字段、撤销/重做，以及 OWNR/DIM/MTXM 不变。

## 触发相关三段（P2-9，VERIFIED）

实现：`src/triggers/trigger_model.h/.cpp`，由 `MapDocument` 在 `Open` 时解析、`Save` 时回写。
统计口径：1297 张真实地图的段长度与内容。

### `MRGN` 地点

**20 字节/条**，字段布局：

```
u16 left, top, right, bottom, stringId, flags, 然后 8 字节保留区
```

- 仅 6 个已知字段、**8 字节保留区常为非零内容**，必须原样 `memcpy` 保留，不能置零。
- 按此结构重建：1268 / 1297 张地图与原始字节完全一致（其余为长度非 20 倍数的段，直接按原始
  字节透传/不解析）。
- `flags` 低 6 位为高程：`0x01` 凹陷 / `0x02` 平地 / `0x04` 高地 /
  `0x08` 低空 / `0x10` 普空 / `0x20` 高空；其它位原样保留。
- 写回：171 编辑地点（双击地点矩形或 0x8108）→ MRGN + STR，整次进入撤销栈；
  回归 `--location-test` 校验字段、保留区、其它地点与未编辑段。

### `DD2` / `THG2` / `MASK`

**DD2 装饰物**：8 字节/条

```
u16 type, u16 x, u16 y, u8 player, u8 enabled（0 启用 / 1 禁用）
```

- 图形本身已烘进 MTXM，DD2 只保存编辑器用的类型/位置/归属。
- 实现：`src/map/decor_model.h` 的 `DoodadTable`，按 8 字节往返；
  长度异常时整段原样保留。
- 编辑 → 转换地形（0x8101）：删除选区（无选区则光标格）内的 DD2 记录，
  MTXM/THG2 不变；记录全部删除后保存时移除 DD2 段，不写零长段。
- 回归：`--decor-test`、`--map-tools-test`。

**THG2 SP 单位/精灵**：10 字节/条

```
u16 type, u16 x, u16 y, u8 player, u8 unused, u16 flags（bit12 = 按精灵绘制）
```

- 实现：`SpriteTable` 按 10 字节解析/序列化；长度异常时原样保留。
- SP 单位层与显示选项打开时绘制青色标记（bit12 空心圆，否则空心方框）。
- 164 面板字段映射：Bit0..11 → flags 低 12 位；Flipped → bit13；禁用 → bit15；
  普通SP/单位SP → bit12；编辑框 1037 → `unused` 字节。
- 回归：`--sprite-test`。

**MASK 战争迷雾**：`u8[width * height]`

- bit0..bit7 对应玩家 1..8；1 = 该玩家视野中被迷雾覆盖，0 = 可见。
- 段长度不足时缺失部分按 `0xFF` 处理；多余字节忽略。
- 实现：`FogMask`，只改被修改过的格并保留其它玩家位；整图默认
  0xFF（未探索）/ 0x00（已探索）。显示时按 bit0 做半透明覆盖。
- 剧本设置 →“禁止迷雾/允许迷雾”按玩家清除/设置对应位；所有玩家整字节填 0x00/0xFF。
- 回归：`--decor-test` 校验 DD2/MASK 逐字节往返、修改、撤销重做与保存重开。

### `WAV` / `SWNM` / `MBRF`

**`WAV ` 声音**：`u32[512]`，每项是声音路径在 STR 中的下标，0 表示空槽。

**`SWNM` 开关名**：`u32[256]`，每项是开关名称的 STR 下标，0 表示默认名。

- 实现：`StringIndexTable`（固定长度，异常长度原样保留）。
- 198 编辑 WAV、179 编辑 SWNM；写入时复用或追加 STR 条目。
- 只在修改后重写对应段；回归 `--scenario-text-test`。
- 可选段（DD2/MASK/THG2/WAV/SWNM）序列化为空时，保存会直接移除该段，
  避免出现零长段；撤销/重做恢复空快照同样按“段不存在”写回。

**`MBRF` 任务简报**：2400 B/条，结构同 TRIG，条件区只有第一条为类型 13。

- 行动 1..9：等待 / WAV / 文本 / 任务目标 / 头像 / 传输等。
- 199 仅编辑已有文本（3）/ 目标（4）/ 传输（8）行动的字符串，不改变结构。
- 行动字符串写回 STR；保存时按 TRIG 同样的 2400 B 结构重建。

### `STR ` 字符串表

```
u16 声明数量 + 若干以 0 结尾的字节串
```

- 关键坑：实测存在**“声明 1024 条、实际字节只够若干条”**的写法。
  因此解析时保留原始 `declaredCount_` 与末尾残余字节 `trailing_`，序列化时按“
  原始数量字段（新增条目超过时取较大者）+ 已解析字符串 + 残余字节”写回。
- 1272 / 1297 张地图重建后逐字节一致。

### `TRIG` 触发（P3-11d，语义已解析）

- 实测 1297 张地图的 TRIG 长度**全部是 2400 的整数倍**，分布 0 / 2400 / 4800 / … / 5 786 400。
- 单条触发 2400 B 的字段布局（VERIFIED，2 张含触发地图逐字节重建一致）：

```
+0     16 × 条件（20 B）
       u32 locationId / u32 player / u32 amount / u16 unitType
       u8 comparison / u8 type / u8 resourceType / u8 property / u8 reserved[2]
+320   64 × 行动（32 B）
       u32 locationId / stringId / wavId / time / player / target
       u16 setting / u8 type / u8 amount / u8 property / u8 reserved[3]
+2368  u32 executionFlags（bit0 仅执行一次 / bit2 保留触发 / bit3 禁用）
+2372  27 B 生效玩家（每玩家 1 字节）+ 1 B currentAction
```

- 条件/行动都以 `type == 0` 表示空槽，运行时遇到第一个空槽即短路；
  编辑器列表也只显示到第一个空槽为止。
- 条件类型 1..23、行动类型 1..57 的编号与 TrigEditPlus 的声明顺序一致，
  中文名称表在 `src/ui/dlg_triggers.cpp`。
- 未修改的段仍按字段重建为原始字节；`--section-test` 的 `trigEq=1` 保持不变。

### 回归入口

```
SCMDPlus.exe --open="<地图>" --section-test
```

输出写入 `%TEMP%\scmdplus_mpq.txt`：`trigBlocks / mrgn / str / trigEq / mrgnEq / strEq`，
三个 `Eq` 全为 1 且退出码 0 才算通过。4 张基准地图（Space Circuit / Astro World /
Mangosteen / Neo China）实测全部通过。

触发语义回归（需要含 TRIG 的地图，如 `(1) easy.scx` / `(2) Friends In Jungle.scx`）：

```
SCMDPlus.exe --open="<地图>" --trig-test="<输出.scx>"
blocks=2->3  cond=1 act=1  lossless=1
parse=1 condOk=1 actOk=1 flagOk=1 playerOk=1
saved=1 reopened=1 reopenFields=1 reopenLossless=1
```

测试会修改首条触发的条件数量、行动数量、执行标志与生效玩家，复制一条触发后
保存并重开校验。

## MPQ 附加文件（自定义 BIN 等）

`.scm/.scx` 除 `staredit\scenario.chk` 外可能还带自定义文件（BIN、瓦片、音频等）。
旧版 `MapDocument::Save` 只写 CHK，会把这些条目静默丢弃。

- 打开时按 `(listfile)` 枚举条目，跳过 `(listfile)/(attributes)/(signature)/(user data)`
  与 `scenario.chk`，其余读入 `extraFiles_`；
- 保存时把 `extraFiles_` 原样写回，并重建覆盖全部条目的 `(listfile)`；
- 读取失败的条目会记录日志，不会阻断保存。

无头回归（先用 `scenario.chk` + `custom\probe.bin` 合成一个种子 MPQ）：

```
SCMDPlus.exe --open="<地图>" --extra-test="<输出.scx>"
opened=1 extras=1 saved=1 readBack=1 same=1 bytes=256
```

