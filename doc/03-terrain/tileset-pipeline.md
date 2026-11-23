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

