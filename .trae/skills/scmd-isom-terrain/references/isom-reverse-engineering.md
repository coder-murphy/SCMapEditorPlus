# ISOM / CV5 逆向详细记录

原版样本：`F:\Games\单机游戏\starcraft\[CN]ScmDraft2.exe`（PE32，ImageBase 0x400000，约 2.0.8.0）。以下 VA 均相对该 exe。

## 1. 原版关键地址

| VA | 含义 |
| --- | --- |
| `0x47C878` | 地形组表：8 个地形集 × 14 个 dword（`era*0x38`）。每项是该地形槽的基准 CV5 组（偶数）。 |
| `0x413920` | 取 `&组表[era]` 的小函数（`imul eax,eax,0x38; add eax,0x47C878`）。 |
| `0x470000+` 附近 | CV5/VX4/VR4/WPE 载入后，运行时构建的 52 字节地形参数记录。 |
| `0x420c90` | 由 ISOM 单元计算瓦片索引并打包字典键：5 个字段各 6 位（类型 + 4 邻接）。 |
| `0x420e50` | 更新瓦片循环；`0x420e98` 调 `0x432390` 查字典。 |
| `0x432390` | 字典查表（`std::map<u32,...>`，map 对象位于 tileset 对象 `+0x48`）。返回节点 `+0x10` 的值。 |
| `0x431d60` | `gen_mega_tile_index`：按 mask 选 mega 子索引，5% 概率用稀有位。 |
| `0x431ec0` | 取默认 mega/子索引。 |
| `0x433580` | 用 CV5 记录构建字典：读取记录的 `word2..word5` 与低字节类型，打包 30 位键。 |
| 字符串 | `Tileset Indexed`、`Set Subtile Type`、`ScmDraft Terrain Palette`。 |

字典 map 节点（24 字节）：`+0 left, +4 right, +8 parent, +0xC key(u32), +0x10 value(u32), +0x14 color, +0x15 isnil`。

## 2. CV5 记录（52 字节）

```
+0   u16 word0   TILE 类型（与 isomap PARAM 的 type 一致，如 Jungle 8=Jungle）
+2   u16 word1   标志（walkable 等）
+4   u16 word2   左邻接 ID
+6   u16 word3   上邻接 ID
+8   u16 word4   右邻接 ID
+A   u16 word5   下邻接 ID
+20  16 × u16    megatile 引用（0 = 空槽）
```

- 偶/奇组（`g, g+1`）记录通常相同：`g` 写偶数列，`g+1` 写奇数列。
- 过滤规则：`1 <= type <= 0x23` 且四个邻接 `<= 0x3F`；其余为装饰/特殊组。
- `type` 字段高位是样式标记：`0x10` DIFFUSE、`0x20` CLIFF、`0x30` SIDE（取自 SHAPE_TABLE 的模板值）。
- 例：Jungle `g8` = `type=8, abut=(8,8,8,8)`；`g2` = `type=2, abut=(1,1,1,1)`；过渡 `g180` = `type=0x17, abut=(1,1,0x33,0x33)`。

## 3. ISOM 单元与瓦片索引组装

- CHK ISOM 单元 8 字节 = 4 × u16（左/上/右/下），每字 `pos:4 | isom:12`。
- 位置位表：偶数单元 `{0x8,0xA,0x0,0x2}`，奇数单元 `{0x4,0xC,0xE,0x6}`。
- 只有 `(cx ^ cy)` 为偶数的单元参与计算。单元 `(cx,cy)` 的四个方向写入周围 2×2 个单元：
  - `from=0` → `(cx-1,cy-1)`，`from=1` → `(cx,cy-1)`，`from=2` → `(cx,cy)`，`from=3` → `(cx-1,cy)`。
- 一个偶数瓦片单元最终由两个来源拼成：
  - 自身单元 `from=2` 写 `left/top`（`x_abut[2] / y_abut[2]`）；
  - 右下对角单元 `from=0` 写 `right/bottom`（`x_abut[0] / y_abut[0]`）。
- 类型优先级：`if (tm.type[from] > maxCenterType || t.type <= maxCenterType) t.type = tm.type[from]`。
- `proj`（投影邻接）写到相邻单元的 `downAbut/upAbut`。

## 4. 字典键与 mega 选择

- 键：`type(6) | left(6)<<6 | top(6)<<12 | right(6)<<18 | bottom(6)<<24`，共 30 位；**不含**上下邻接。
- 选组：`dict[key].group`，写入 `(group<<4)|mega` 与 `((group+1)<<4)|mega`。
- mega 选择（`gen_mega_tile_index`）：
  - `upAbut == 0` 或首行：统计 mask 低位连续段 `often`，再统计下一段 `seldom`；95% 取 `rand % often`，5% 取 `often+1 + rand % seldom`。
  - 否则沿用上方瓦片 `MTXM & 0x0F`。
- 若目标组没有该 mega，回退到两组共有的有效子索引。

## 5. 原版 “Tileset Indexed” 调色板

- 布局：**16 列 = 组的 16 个子瓦片槽，行 = 组号连续排列**；空槽（CV5 引用 0）画纯黑，因此整表看起来有“空洞”。
- 实测 Badlands（`StarDat.mpq_badlands.cv5`）：
  - `group 0` 全 16 空 → 整行黑；
  - `22..43` 只有 sub 0 有效 → 每行 1 个瓦片 + 15 黑（对应截图顶部左侧亮条 + 大块黑）；
  - `22..120` 的 sub 7/13/14/15 全空 → 竖向黑条；
  - `496..1023` 连续空组（603 个）→ 大块黑。
- Jungle：`28..35` 只有 sub 0；sub 7/13/14/15 在 0..120 组中几乎全空。与用户截图完全对应。
- 说明：显式放置（Tileset Indexed/Subtile）与等距地形共用同一 `group<<4|sub` 索引空间，过渡瓦片无特殊结构。

## 6. 本次根因与修复

两个叠加根因：

1. `MakeTileIndex`/`IsometricLink`/`ResolveLinks` 把 ISOM 整字（含 pos 位）当 ISOM 值解码 → 过渡键随机化。
2. 字典键把邻接压成 2 位（`& 3`）→ 悬崖/过渡邻接串组。

修复（提交 `541a4dc`）：

- 所有解码处 `>> 4`；写回仍 `<< 4`。
- 字典键改为 6 位字段打包（`PackTileKey`）。
- 字典改由 CV5 记录全量构建（`Learn` → `Tileset::GroupTileIndex`），去掉“地图样本投票”和“放宽键”。
- 增加 `roundTrip` 自检：用字典重算整图与原 MTXM 组号比较；同标识多组视为外观变体，不算错误。
- `--terrain-blend-test` 输出 `roundTrip=不一致/缺失`。

修复前 Jungle 图 `roundTrip=4348 缺失`；修复后 `3 缺失 / 183 不一致`（后者多为自定义组）。Badlands 官方图 `0 缺失 / 90 变体`。

## 7. 调试与验证经验

- 临时逆向脚本位于 `%TEMP%\scmd_re`（`dis_range.py`、`find_callers.py`、`find_refs_data.py`、`mpq_dump.py`、`chk_diff.py` 等），依赖 Python 3.12 + capstone + pefile；临时目录可能被清理，需要时重建。
- `mpq_dump.py` 的扇区大小应按 MPQ 规则 `512 << (blockSizeShift - 3)`，而不是 `1 << shift`。
- 原版地图 `.scm` 的 MPQ 块表可能是保护/加密变体，Python 读不到时用本编辑器 `--open` + `--save-to` 规范化后再解析 CHK。
- 与原版做鼠标自动化对照时注意 DPI：原版为 DPI-unaware，125% 缩放下系统会把合成坐标缩放 0.8（142,300 → 114,240）。探针与目标使用同一 DPI 上下文，或用日志核对实际收到的坐标。
- 修改原版游戏目录地图后必须还原（用同版本副本按哈希核对）。
