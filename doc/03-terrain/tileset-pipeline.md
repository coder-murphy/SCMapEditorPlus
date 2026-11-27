# 瓦片管线与崖壁族

## 渲染管线（VERIFIED）

```
MTXM 值 v
  group = (v & 0x7FF0) >> 4
  index = v & 0x0F
CV5[group]        52 字节；16 个大瓦片引用位于 u16[10..25]（字节偏移 20）
  mega = cv5[group].tiles[index]
VX4[mega]         32 字节 = 16 个 u16 引用
  ref = vx4[mega][k]; vrid = ref >> 1; hflip = ref & 1
VR4[vrid]         64 字节 = 8×8 调色板索引
WPE               调色板，**每色 4 字节**（用 3 字节会渲染成坏瓦片）
```

16 个小块在 32×32 像素里的排布：`k = row*4 + col`，每块 8×8。

## ERA 与地形集

| ERA | 地形集 | 所在 MPQ |
|---|---|---|
| 0 | badlands | StarDat.mpq |
| 1 | platform（编辑器显示 Space） | StarDat.mpq |
| 2 | install（编辑器显示 Installation） | StarDat.mpq |
| 3 | ashworld | StarDat.mpq |
| 4 | jungle | StarDat.mpq |
| 5 | desert | BrooDat.mpq |
| 6 | ice | BrooDat.mpq |
| 7 | twilight | BrooDat.mpq |

> MPQ 内部文件名用 `tileset\\platform.*`、`tileset\\install.*`，与编辑器显示名不一致。

## 地形集数据（随仓库提供）

8 套地形集四件套已入库到工程根目录 `ts/`（32 个文件，13 517 280 字节），
构建时由 `SCMDPlus.vcxproj` 的 `CopyTilesets` 目标复制到 `bin\<Platform>\<Config>\ts`，
因此编译产物不需要星际安装目录即可运行。

运行时的地形集目录查找顺序：

1. `--ts=<目录>`
2. `<exe目录>\ts`（构建产物自带；单独拷贝 exe 与 `ts/` 目录即可运行）
3. `<exe目录>\..\..\..\ts`（工程根目录，VS 内调试通用）

## 重新导出地形集（可选，--extract-ts）

游戏 MPQ 已随仓库放在工程根目录 `mpq/`，无需安装游戏即可离线重导出：

```
SCMDPlus.exe --extract-ts="<工程根目录>\ts" --game-dir="<工程根目录>\mpq"
```

- 从 `StarDat.mpq` 导出 badlands / platform / install / ashworld / jungle，
  从 `BrooDat.mpq` 导出 desert / ice / twilight，共 8 × 4 = 32 个文件；
- 输出名与 `MapDocument::kEraPrefix` 对齐：`StarDat.mpq_badlands.cv5` 等；
- 结果写入 `%TEMP%\scmdplus_mpq.txt`：`ts=OK files=32 failed=0 bytes=13517280`。
- `--game-dir` 也可指向任意装有 `StarDat.mpq` / `BrooDat.mpq` 的游戏目录。

实测导出结果：32 个文件、13 517 280 字节；用 `(2) Friends In Jungle.scx` 验证
`--brush-test` 得到 4317 个可绘制瓦片，`--dump-render` 与 `--edit-test` 均通过。

## CV5 类型字段

`u16[0]`：bit0 = 可通行；其余位是类型。实测（`Type() = u16[0] >> 1`）：
`2` 虚空（raw 4）、`3` 甲板（raw 7）、`10` 边缘/崖壁（raw 0x14 = 20）、
`21` 其它非行走地物。注意：文档旧写法中的“类型 20”指 **raw 值 20**，移位后是 10。

## 崖壁族（Space 地形集，VERIFIED）

- 崖壁组区间 `g302..g357`，CV5 首字 raw 0x14（`Type() = 10`），**全部不可行走**。
- 每组只有 3 个有效大瓦片（index 0/1/2）；index 3 及以后指向空瓦片。
  **几何由 group 决定，index 只提供纹理变体。**
- 平台边缘是 **3 圈**结构，每圈再按「甲板在哪一侧」分 N/E/S/W：

| 圈 | N | E | S | W |
|---|---|---|---|---|
| 1（贴甲板） | 308,309 | 334,336 | 310,311,316,317 | 339,341 |
| 2 | 306,307 | 335,337 | 318,319 | 338,340 |
| 3 | 304,305 | 354,330 | 320,321 | 325,357 |

- 组沿边界按位置**循环**铺设，例如南向边界沿 +x 为 g128→g129→g132→g133 一类的 4 格周期。
  只用两种瓦片交替会得到马赛克锯齿 —— 这就是最初生成地图“没有悬崖边缘”的原因。
- **必须向虚空外侧压印。** 崖壁组不可行走，向内铺会吃掉甲板；3 格宽的窄口会被直接切断。
  向外压印时可行走区域逐格不变，连通性不受影响。

## 参考实现

本项目在 `src/terrain/edge_placer.cpp` 实现了 `EdgePlacer::StampOutward`：
甲板 4 邻域 BFS 得距离 1..3，按最近甲板方向选组族，组沿边坐标循环、index 取纹理变体；
只改写虚空侧，重复调用幂等。入口为「调试 → 自动铺设崖壁」（0x8FFE），
回归 `--edge-test` 检查 1..3 圈齐全与甲板不变。

历史上另有一份 Node 实现（同一套规则），仅是规则来源记录，不参与当前构建。

## C++ 实现状态（P1-4，VERIFIED）

| 模块 | 内容 |
|---|---|
| `Tileset::Load(dir, prefix)` | 读取 `<dir>\\<prefix>.cv5/.vx4/.vr4/.wpe`；ERA→前缀映射在 `map_document.cpp` |
| `Tileset::TileBlock(v, out[1024])` | 严格按上面管线：group/index → CV5 u16[10..25] → VX4 `ref>>1`/`ref&1` → VR4 8×8 |
| `Tileset::HasCreep/CreepTile` | 扫描 CV5：type=0 且 `flags & 0x0040` 的组为 creep；取 4×4 槽位。7 套地形集均为组 1、13 个非空槽；Installation 无 creep 组 |
| `TerrainRenderer::Render` | 1..128 px/瓦片，最近邻采样，输出 32 位 BGRA（支持 33%/66%/150%.. 等非整除档位） |
| 主窗口 | 打开后自动 8 px/瓦片渲染；滚轮 2/4/8/11/16/21/32/48/64/96/128 缩放；`BitBlt` 呈现，`WM_ERASEBKGND` 抑制闪烁 |

### 验证

`SCMDPlus.exe --open="<地图>" --dump-render="<out.bmp>"`（地形集默认取输出目录或工程根的 `ts/`；`--ts=` 仅用于显式覆盖）导出 24 位 BMP，
与 Node 参考实现逐瓦片比对：

| 地图 | 结果 |
|---|---|
| (8)Space Circuit.scx（platform） | 1536×1536，抽样 225 个瓦片中心像素**全部一致**，86 种颜色 |
| (8)Mangosteen v1.0.scx（badlands） | 1536×1536，抽样 225 个瓦片中心像素**全部一致**，126 种颜色 |

GUI 回归：4 张地图标题正确（含 ERA 与单位数）；连续滚轮缩放后进程存活。

### 8 套地形集逐套渲染回归（P3-17，VERIFIED）

```
SCMDPlus.exe --tileset-render-test="<输出目录>"
```

对 ERA 0..7 各建一张 16×16 合成地图，MTXM 取 `group = ((x+y*16)*37) % GroupCount`、
`index = (x+y) % 16`，覆盖地形集的组与全部 16 个索引；按 8 px/瓦片渲染，
并与**独立参考解码**（直接读 CV5/VX4/VR4/WPE，不经过 `Tileset`/`TerrainRenderer`）
逐像素比对。每套导出一张 128×128 的 BMP，哈希必须互不相同。

实测（`pass=8/8`）：

| ERA | 地形集 | groups | megas | vr | 颜色数 |
|---|---|---|---|---|---|
| 0 | badlands | 1665 | 4844 | 26247 | 49 |
| 1 | platform | 1513 | 3055 | 18787 | 88 |
| 2 | install | 1265 | 1431 | 7259 | 102 |
| 3 | ashworld | 1262 | 3497 | 18078 | 87 |
| 4 | jungle | 1578 | 5046 | 23818 | 105 |
| 5 | desert | 1520 | 5810 | 32158 | 106 |
| 6 | ice | 1415 | 4518 | 30376 | 83 |
| 7 | twilight | 1494 | 5232 | 28124 | 101 |

诊断写入 `%TEMP%\scmdplus_mpq.txt`，每套一行
`era=... files=1 created=1 groups=... render=1 dims=1 pixels=1 colors=... unique=1 bmp=1 hash=...`。

### 已知限制

- 整图渲染：192×192 图在 32 px/瓦片时约 113 MB，缩放会卡；后续改为按可视区域分块。
- 地形编辑（刷子写 MTXM）尚未接入，见 P2-8。

## 地形刷（P2-8，VERIFIED）

| 方法 | 行为 |
|---|---|
| `MapDocument::PaintTile(tx, ty, value)` | 写 MTXM 的 u16 小端值，置脏 |
| `MapDocument::PaintRect(x0, y0, x1, y1, value)` | 矩形批量刷 |
| `TileAt(tx, ty)` | 读当前值（吸管用） |

主窗口交互：

- `1` 地形刷 / `2` 单位 / `3` 吸管；左键应用，右键删除该格单位，`Delete` 删除光标处单位；
- 拖动可连续绘制；每格修改后**只重绘该格**（`PaintTileToBuffer` 直接写 DIB 区域并 `InvalidateRect`），不整图重渲；
- 滚轮 2/4/8/11/16/21/32/48/64/96/128 缩放；滚动条与 `WM_HSCROLL`/`WM_VSCROLL` 已接通；左上角 HUD 显示当前工具与刷子值。

验证：无头回归中 `painted=1`、保存后重开 `repaint=3/3`（3 格全部写回成功）。

## 地形刷选择（调色板窗口）

「窗口 → 刷子窗口」（0x8114）打开独立调色板，用来选择 `brushTile_`：

- `BrushPalette::BuildEntries` 遍历 CV5 组，跳过越界大瓦片与全透明块；
  同一组内指向同一 mega 的索引只保留第一个（同组变体仍保留）。
- 10 列网格、40 px 单元；预览走 `Tileset::TileBlock` + WPE 着色，只绘制可见行。
- 顶部筛选栏：可走性（全部/可走/阻挡）、类型（从 CV5 动态收集）、组区间
  （0-511 / 512-1023 / 1024-1535 / 1536-2047）。
- 点击或方向键选中后向主窗口发送 `WM_APP+0x41`，主窗口写回 `brushTile_` 并切回地形层。
- 吸管选中的瓦片会同步高亮；同组同 mega 的其它索引视为同一块。
- 地图打开 / 新建 / 关闭时自动刷新；未载入地形集时显示空状态。

回归：

```
SCMDPlus.exe --open=<map> --brush-test
entries=4317 unique=1 window=1 click=1 paint=1 readback=1 filters=1 file=1
```

`filters=1` 覆盖可走/阻挡/类型/组区间四类过滤；`file=1` 为 `.scb` 刷子文件往返。

## 自定义刷子与图案刷

182 对话框（编辑层 → 复制/剪切/粘贴/刷子）把选区保存为多格图案，支持 `.scb` 打开/保存；
双击应用后，主窗口以光标为中心压印图案，整笔作为一次撤销记录。
无头回归：`--brush-pattern-test`。

## ScmDraft2 等距地形放置数据（RE 发现）

从 `[CN]ScmDraft2.exe`（PE32，image base 0x400000）提取到的完整地形类型表与等距铺设规则：

### 名称表

- 8 个地形集名：文件偏移 0x7B038，每个 15 字节定长：
  Badlands / Space Platform / Installation / Ash World / Jungle World /
  Desert World / Ice World / Twilight World。
- 地形类型名：文件偏移 0x7B0B0，8 × 13 × 30 字节定长（末尾空槽为 `Undefined`）。
- 状态栏 `Isometrical Terrain | Jungle World Dirt` 即由“地形集名 + 类型名”拼接。

### 组号表

VA 0x47C878 处是 8 × 14 个 u32（第 14 项保留为 0），代码 0x413920 按
`&table[tileset * 0x38]` 取到每个地形集的 14 项；值就是 CV5 组号。
名称与组号按槽位一一对应：

| 地形集 | 类型 = CV5 组（偶数列） |
|---|---|
| Badlands | Dirt 2 / Mud 20 / High Dirt 4 / Water 6 / Grass 8 / High Grass 10 / Structure 18 / Asphalt 16 / Rocky Ground 12 |
| Space Platform | Space 2 / Low Platform 14 / Rusty Pit 18 / Platform 4 / Dark platform 16 / Plating 6 / Solar Array 12 / High Platform 8 / High Plating 10 / Elevated Catwalk 20 |
| Installation | Substructure 2 / Floor 6 / Roof 8 / Substructure Plating 4 / Plating 10 / Sunstructure Panels 14 / Bottomless Pit 12 |
| Ash World | Magma 14 / Dirt 2 / Lava 4 / Shale 10 / Broken Rock 16 / High Dirt 6 / High Lava 8 / High Shale 12 |
| Jungle World | Water 6 / Dirt 2 / Mud 26 / Jungle 8 / Rocky Ground 10 / Ruins 14 / Raised Jungle 12 / Temple 16 / High Dirt 4 / High Jungle 18 / High Ruins 20 / High Raised Jungle 22 / High Temple 24 |
| Desert World | Tar 6 / Dirt 2 / Dried Mud 26 / Sand Dunes 8 / Rocky Ground 10 / Crags 14 / Sandy Sunken Pit 12 / Compound 16 / High Dirt 4 / High Sand Dunes 18 / High Crags 20 / High Sandy Sunken Pit 22 / High Compound 24 |
| Ice World | Ice 6 / Snow 2 / Moguls 26 / Dirt 8 / Rocky Snow 10 / Grass 14 / Water 12 / Outpost 16 / High Snow 4 / High Dirt 18 / High Grass 20 / High Water 22 / High Outpost 24 |
| Twilight World | Water 6 / Dirt 2 / Mud 26 / Crushed Rock 8 / Crevices 10 / Flagstones 14 / Sunken Ground 12 / Basilica 16 / High Dirt 4 / High Crushed Rock 18 / High Flagstones 20 / High Sunken Ground 22 / High Basilica 24 |

### 等距铺设规则

0x419010（地形层鼠标/绘制处理）与 0x431D60（默认子索引）给出的行为：

1. 偶数列（`tx & 1 == 0`）使用组 `G`，奇数列使用 `G + 1`；地图数据中两组严格
   按列交替，构成 2×2 拼块。
2. 奇数列若左格（`tx - 1`）的组正好是 `G`，则沿用左格的子索引；否则随机。
3. 随机子索引优先在 0..5 中取（`rand() & 7` 且 < 6，且 CV5 槽非 0），
   失败后在 6..15 中重试；所有槽都为空时返回 0。
4. 本工程的“矩形地形”模式不做列交替，直接使用所选组的第一个有效子索引。

CV5 组号的低 4 位即子索引；地图数据验证：Jungle 水（组 6）出现在偶数列、
组 7 出现在奇数列，且奇数列子索引与左格完全一致（样本 17036/17037 吻合）。

实现位置：表数据在 `src/terrain/terrain_types.cpp`；等距模式下地图有 ISOM 段时走
`src/terrain/isom_terrain.*`（见下节），否则回退到 `MainWindow::TerrainValueFor`
的“组内随机/沿用”旧规则；默认/随机子索引在
`Tileset::FirstValidSubtile / RandomValidSubtile`。回归：`--terrain-type-test=<out.scx>`。

## 等距地形（ISOM）绘制管线

原版 ScmDraft2 的等距地形不是“同组换子索引”，而是维护 `ISOM` 段的四条边值，
再由硬编码表把“四边形状 + 邻接关系”映射到 CV5 组。本工程按同样管线实现，
但把硬编码字典改为**从当前地图样本学习**，因此不依赖外部数据文件：

1. **ISOM 段**：`u16[(w/2+1)*(h+1)*4]`，每个 2×1 菱形单元 4 个值
   （左/上/右/下）：bit0 编辑位、bit1-3 位置、bit4-14 地形类型 ID、bit15 skip。
   打开地图时按精确长度校验，匹配才启用；保存时原样写回（含绘制修改）。
2. **ERA 参数表**：`src/terrain/isom_terrain.cpp` 内置 8 个地形集的
   `center[13]`（中心 ISOM/类型/邻接/样式）与 `edge[12]`（低地/高地/悬崖邻接），
   以及 14 种边缘形状模板（`SHAPE_TABLE`），取自原版 `PARAM_TABLE`。
3. **画笔**：等距模式每次落笔固定为光标顶点周围的 2×2 个 ISOM 菱形
   （原版 0x419010 的 isometric 分支只按单个顶点刷一笔，尺寸循环仅用于方块模式）；
   先写入 4 个菱形朝向顶点的边值，再从顶点向外做连接解析
   （`IsometricLink` / `MatchShape` / `SearchBrushLink`），得到高低地/悬崖过渡。
4. **生成 MTXM**：对每个脏单元计算 `ISOM_TILE`（type + 4 个水平邻接 + 上下邻接），
   查字典得到偶数 CV5 组，奇数格用 `组 + 1`；子索引按 CV5 有效掩码随机，
   悬崖列沿用上一行同一列的子索引（与原版 `gen_mega_tile_index` 一致）。
5. **字典学习**：遍历地图所有菱形单元，用同一套 `map_isom_tile` 逻辑计算键，
   以 MTXM 偶数格组号投票得到 `键 → 组`；另存“忽略上下邻接”和“仅按类型”的
   放宽表，地图样本不足时依次回退。
6. **撤销/保存**：`EditCommand` 增加 ISOM 前后快照；`MapDocument` 保存时写回
   `ISOM` 段；改尺寸时 ISOM 按菱形网格平移（越界取最近边缘值）。



