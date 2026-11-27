---
name: scmd-isom-terrain
description: ScmDraft2 ISOM/CV5 terrain reverse engineering and fix workflow for this repo. Use when the user asks about 等距地形, ISOM, CV5 瓦片标识, 过渡瓦片, 地形刷 or terrain group bugs. Not for unrelated UI work.
---

# ScmDraft2 ISOM 地形逆向与修复

本仓库的地形刷（标准/等距地形）复刻 ScmDraft2 的 ISOM 管线。改动 `isom_terrain.*`、`tileset.*` 或排查“过渡缺失、空洞、错组、刷子异常”时，按本文事实与验证流程执行。深入地址与数据表见 `references/isom-reverse-engineering.md`。

## 核心事实（必须遵守）

1. **ISOM 字布局**：CHK 中每个 ISOM 坐标是 u16，低 4 位是位置位（pos），高 12 位才是 ISOM 值。
   - 任何解码前先 `word >> 4`；`UpdateIsom` 写回时 `isom << 4 | pos`。
   - 曾因直接用整字解码导致过渡键全部错乱、地图出现矩形空洞。
2. **ISOM 网格**：单元数 `(w/2+1) × (h+1)`，每单元 4 个坐标（左/上/右/下）；只有 `(cx ^ cy)` 为偶数的单元参与瓦片索引计算。
   - 单元 `(cx,cy)` 对应瓦片对 `(2cx, cy)` 与 `(2cx+1, cy)`，组号分别为 `group` 与 `group+1`。
3. **CV5 记录就是“瓦片标识”**：每条 52 字节，`word0 = TILE 类型`，`word2..word5 = 左/上/右/下四个邻接 ID`；偏移 20 起是 16 个 megatile 引用（0 = 空槽）。
   - 过渡瓦片与纯地形瓦片结构完全相同，只是组号不同。
   - 偶数组是基准组，奇数组是同一标识的配对组（记录相同）。
4. **字典键**：`类型 + 四向邻接`，每字段 6 位、共 30 位，打包进 u32（见 `PackTileKey`）。
   - 上下邻接不进键；竖直衔接由 mega index（`upAbut`）处理。
   - 绝不能把邻接压成 2 位：悬崖/邻接值可达 0x3F，会串组。
5. **字典来源**：从当前地形集 CV5 全量构建，不要从地图样本投票学习（样本覆盖不全）。
   - 过滤：`type` 为 1..0x23，四向邻接都 `<= 0x3F`；跳过装饰/特殊组。
   - 每个偶数组写入 `dict_[key] = {group, megaMask}`；`megaMask` 由 CV5 的 16 个引用是否非 0 得到。
6. **ERA 参数与形状表**：来自 isomap 库移植（`ParamFor`、`Shapes`）。
   - 类型字段高位是样式标记：`0x10` DIFFUSE、`0x20` CLIFF、`0x30` SIDE（低 4 位为槽号/参数）。
   - `edgeStart` 之后按 `(isom - edgeStart)` 分成 `边号*14 + 形状号`。
7. **落笔语义**：单次等距落笔只改光标顶点周围 2×2 个菱形（与笔刷尺寸无关），随后 `ResolveLinks` 只沿连接修正邻居 ISOM。

## 代码地图

- `src/terrain/isom_terrain.cpp|h`：`Learn`（建字典+自检）、`KeyFor`/`PackTileKey`、`Brush`、`IsometricLink`、`MakeTileIndex`、`UpdateTile`、`FlushTiles`。
- `src/terrain/tileset.cpp|h`：`GroupTileIndex`（读 CV5 记录头）、`HasSubtile`、`MegaMaskOf` 依赖的 52 字节记录布局。
- `src/ui/main_window.cpp`：`StampIsometricAt`（首次落笔 `Learn`，之后 `Brush` + `FlushTiles` + 写 ISOM）。
- `src/app/app.cpp`：`--terrain-blend-test` 输出 `roundTrip=不一致/缺失`。

## 标准验证

1. 构建（x64 Release）：
   `D:\Apps\VS2022\MSBuild\Current\Bin\MSBuild.exe src\SCMDPlus.vcxproj /p:Configuration=Release /p:Platform=x64 /m /v:minimal`
2. 真实地图回归（Jungle/Badlands 等官方图）：
   `SCMDPlus.exe --open=<地图> --ts=<地形集目录> --terrain-blend-test=<输出.scx>`
   读取 `%TEMP%\scmdplus_mpq.txt`（UTF-16），关注：
   - `roundTrip=缺失/不一致`：用 CV5 字典重算整图后，与原 MTXM 组号不符的格子数。
   - 官方地图期望 **缺失 0**；少量“不一致”通常是同标识的外观变体组或自定义组。
   - `invalid=0`：所有瓦片子索引在 CV5 中有效。
3. 全量回归：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\regression.ps1`（四配置 35 项）。
4. 与原版逐字节对照时注意 DPI：原版 ScmDraft2 是 DPI-unaware，125% 缩放下合成鼠标坐标会被系统缩放（例：142,300 变成 114,240）。自动化探针不要启用 PerMonitor DPI，或直接给同一进程发消息并在日志中核对坐标。

## 常见坑

- 任何 `Cell(...)[...]` 当 ISOM 值使用前忘记 `>> 4`。
- 用 2 位打包邻接，导致不同悬崖 ID 落到同一个字典键。
- 只在“地图中见过的组合”里建字典，导致新区域查不到组而留空洞。
- 把空槽 CV5 记录（type 0 / 超大邻接值 / 全 0）收进字典造成误匹配。
- `group+1` 的 mega 子索引必须对两个组都有效；无效时回退到共同有效子索引。

详细逆向记录（原版地址、表结构、调色板排布、调试脚本思路）见 `references/isom-reverse-engineering.md`。
