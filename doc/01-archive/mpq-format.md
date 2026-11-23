# MPQ 容器（.scm / .scx）

星际 1 地图本体就是一个 MPQ 归档，只有两个条目：

```
(listfile)
staredit\scenario.chk
```

## 已验证规则（VERIFIED）

| 项 | 结论 |
|---|---|
| 哈希槽位 | 用 **hash 类型 0**：`hashString(name, 0) & (hashSize - 1)`，**不是**标准 MPQ 的 hashA(类型1) |
| locale | 必须为 `0x0000`（不是常见的 `0xFFFF`） |
| `(listfile)` | 必须存在，否则地图不被正确识别 |
| 压缩 | 支持 zlib(`0x02`) 与 PKWARE implode(`0x08`)，两种都要实现 |
| 扇区表 | 仅加密文件用 `key-1` 解密；未加密文件直接读取 |
| 扇区 i | 用 `key+i` 解密 |
| 扇区表第 0 项 | 必须等于扇区表自身的字节数 |

### 用错哈希类型的症状

用 hashA(类型1) 计算槽位时：`SFileOpenArchive` 成功，但 `SFileOpenFileEx` 找不到文件，
ScmDraft2 报 **"Error reading scenario.chk from MPQ"**。

## 独立校验方法（黄金标准）

用游戏目录 `Libs\SFmpq.dll`，在 **32 位** PowerShell 下依次调用：

```
SFileOpenArchive -> SFileOpenFileEx("staredit\\scenario.chk") -> SFileGetFileSize -> SFileReadFile
```

三步全过、且读出的 `DIM ` 段尺寸正确，才算容器合格。64 位进程无法加载该 DLL，必须用
`%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe`。

## 加密 key 的推导

```
key = hashString(basename, 3)
if (flags & FLAG_ADJUSTED) key = (key + blockPos) ^ fileSize
```

> 2026-11 从 `StarDat.mpq` / `BrooDat.mpq` 提取地形集时实测：地形集四件套全部带
> `0x00020000`（FIX_KEY）标志，key 必须再与 `fileSize` 异或；只做 `+ blockPos`
> 会得到错误的扇区表（表现为 `sector-bounds` 或非法的压缩类型字节）。

## C++ 实现状态（src/archive）

已实现（VERIFIED，4 张真实地图与 Node 参考实现逐字节一致）：

| 能力 | 说明 |
|---|---|
| `Open` | 解析头、解密哈希表（hash("\(hash table)",3)）与块表（hash("\(block table)",3)） |
| `ReadFile` | hash 类型 1/2 匹配；单块（0x01000000）与多扇区两种布局；扇区表 key-1、第 i 扇区 key+i |
| 解压 | zlib(0x02)、PKWARE implode(0x08)、未压缩(0x00) |
| 便捷接口 | `ReadScenarioChk`（4 种条目名）、`FileExists`、`EntryCount`、`LastError` |

实测样本（CHK 大小 / DIM 均与参考值一致）：

| 地图 | 大小 | 尺寸 |
|---|---|---|
| (8)Space Circuit.scx（zlib） | 369 947 | 192×192 |
| (8)Astro World v1.3.scx（implode） | 391 875 | 192×192 |
| (8)Mangosteen v1.0.scx（implode） | 388 281 | 192×192 |
| (6)Neo China.scx（implode） | 161 973 | 192×64 |

### 两个实现坑（都是实测踩出来的）

1. **0x02 扇区的负载是带 2 字节 zlib 头的**（`78 9C`），直接当 raw deflate 解会失败。
   判定方法：`(b0 & 0x0F) == 8 && ((b0 << 8 | b1) % 31) == 0`，命中则跳过 2 字节；失败时回退尝试裸流。
2. **PKWARE 距离码表展开后是 64 个符号**（`{2,20,53,230,247,151,248}` 按"高 4 位=重复次数-1、低 4 位=码长"展开），
   不是 7 个；把上限写成 7 会在遇到长距离匹配时误判为非法码。

### 自检入口

```
SCMDPlus.exe --mpq="<地图路径>"
```

结果写到 `%TEMP%\scmdplus_mpq.txt`：`entries` / `chk` 字节数 / `dim`；失败时带 `stage`。

### 写出（P1-5，VERIFIED）

`MpqArchive::WriteArchive(path, files)` 的布局：

```
header(32) | 文件数据（16 字节对齐） | 哈希表(1024 × 16) | 块表(n × 16)
```

- 默认块标志 `0x80000000`（仅"存在"）→ 未压缩、未加密，因此**没有扇区表**；
- 可压缩条目（512 B ≤ 大小 ≤ 16 MB）尝试 DEFLATE：压缩后至少小 16 B 才写压缩块，
  标志为 `0x80000000 | 0x01000000 | 0x00000200`（存在 / 单块 / 压缩），
  数据为 `0x02`（zlib 类型字节）+ raw deflate，同样**没有扇区表**；
- 哈希槽位 `hash(name, 0) & 1023`，冲突线性探测；`locale` 与 `platform` 写 0；
- 两张表分别用 `hash("(hash table)", 3)` / `hash("(block table)", 3)` 加密；
- 必须包含 `(listfile)` 条目。
- 编辑器保留原图的附加文件（`(listfile)` 中除 CHK/内部条目外的全部条目），保存时原样写回；
  文件 →「MPQ 文件管理...」（167）可列出/导入/导出/改名/添加/删除，改动随保存写入。
  名称匹配不区分大小写，`(listfile)` 与 `scenario.chk` 不允许被覆盖。

验证（`SCMDPlus.exe --open="<src>" --save-to="<dst>"`）：

| 地图 | CHK 往返 | SFmpq.dll（32 位） |
|---|---|---|
| (8)Space Circuit.scx | 369 947 B，**0 字节差异** | PASS |
| (8)Astro World v1.3.scx | 391 875 B，**0 字节差异** | PASS |
| (6)Neo China.scx | 161 973 B，**0 字节差异** | PASS |

另有：保存前后渲染结果 BMP 的 SHA256 完全相同。

### 尚未实现

- 压缩类型 0x01（huffman）与 0x10（bz2）——星际 1 地图中未见，暂不处理。

### 调试提取

`SCMDPlus.exe --mpq=<档案> --mpq-extract=<条目> --extract-to=<输出>`
可取出单个条目（如 `arr\flingy.dat`），用于离线逆向；属于无头工具模式，不写 settings。

## 参考资料

本目录规则均来自本项目 `tools/mapgen/` 下的生成器与校验脚本实测，非推测。
