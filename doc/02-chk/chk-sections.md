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
