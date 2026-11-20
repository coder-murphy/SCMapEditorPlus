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

## CV5 类型字段

`u16[0]`：bit0 = 可通行；其余位是类型。实测：`2` 虚空、`3` 甲板、`20` 边缘/崖壁、
`21` 其它非行走地物。

## 崖壁族（Space 地形集，VERIFIED）

- 崖壁组区间 `g302..g357`，类型 20，**全部不可行走**。
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

本项目在 `src/terrain/edge_placer.cpp` 保留了上表的组族常量；
历史可用的 Node 实现见 `E:\Games\PCGames\SC1BWAI\tools\mapgen\`（同一套规则）。

## C++ 实现状态（P1-4，VERIFIED）

| 模块 | 内容 |
|---|---|
| `Tileset::Load(dir, prefix)` | 读取 `<dir>\\<prefix>.cv5/.vx4/.vr4/.wpe`；ERA→前缀映射在 `map_document.cpp` |
| `Tileset::TileBlock(v, out[1024])` | 严格按上面管线：group/index → CV5 u16[10..25] → VX4 `ref>>1`/`ref&1` → VR4 8×8 |
| `TerrainRenderer::Render` | 2/4/8/16/32 px/瓦片，最近邻采样，输出 32 位 BGRA |
| 主窗口 | 打开后自动 8 px/瓦片渲染；滚轮 2/4/8/16/32 缩放；`BitBlt` 呈现，`WM_ERASEBKGND` 抑制闪烁 |

### 验证

`SCMDPlus.exe --open="<地图>" --ts="<ts目录>" --dump-render="<out.bmp>"` 导出 24 位 BMP，
与 Node 参考实现逐瓦片比对：

| 地图 | 结果 |
|---|---|
| (8)Space Circuit.scx（platform） | 1536×1536，抽样 225 个瓦片中心像素**全部一致**，86 种颜色 |
| (8)Mangosteen v1.0.scx（badlands） | 1536×1536，抽样 225 个瓦片中心像素**全部一致**，126 种颜色 |

GUI 回归：4 张地图标题正确（含 ERA 与单位数）；连续滚轮缩放后进程存活。

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
- 滚轮 2/4/8/16/32 缩放；滚动条与 `WM_HSCROLL`/`WM_VSCROLL` 已接通；左上角 HUD 显示当前工具与刷子值。

验证：无头回归中 `painted=1`、保存后重开 `repaint=3/3`（3 格全部写回成功）。
