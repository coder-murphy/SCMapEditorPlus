# SCMD Plus 3.0

星际争霸 1 地图编辑器（Win32 / C++17 / VS2022），业务结构参照 ScmDraft2 的功能划分。

## 目录约定

- `src/`  源码，**按业务域分目录**，一个业务域一个子目录，互不跨层引用。
- `doc/`  文档，**与 src 同构**：`doc/01-archive/` 对应 `src/archive/`。
- `doc/00-reverse/` 逆向产出（二进制画像、资源清单），不属于任何业务域。
- `src/res/` 资源脚本（菜单、加速键、图标、25 个对话框）。
- `ts/` 地形集四件套（8 套 × cv5/vx4/vr4/wpe），随仓库提供；构建后自动复制到输出目录。
- `mpq/` `StarDat.mpq` / `BrooDat.mpq`（约 90 MB），仅用于离线重导出地形集，不参与构建与运行。

## 业务域

| 目录 | 职责 |
|---|---|
| `src/app/` | 入口、生命周期、单例装配 |
| `src/platform/` | Win32 薄封装（窗口、文件对话框、注册表设置） |
| `src/common/` | 基础类型、日志、字符串编码 |
| `src/archive/` | MPQ 容器读写 |
| `src/chk/` | CHK 段解析与序列化 |
| `src/terrain/` | 地形集、瓦片渲染、崖壁铺设 |
| `src/units/` | 单位记录模型 |
| `src/players/` | 玩家/势力/归属 |
| `src/triggers/` | 触发器、地域、字符串 |
| `src/map/` | 地图文档、校验、撤销栈 |
| `src/ui/` | 主窗口、对话框、命令分发 |
| `src/api/` | C ABI 导出层（`SCMDCore.dll`），供 CLI / WPF / Python 调用 |
| `src/cli/` | 示例命令行工具（`scmd_cli.exe`）

## 构建

用 VS2022 打开 `SCMDPlus.sln`，配置 `Debug|Win32` 或 `Release|Win32`（与星际 1 的 32 位工具链对齐）。

构建会把 `ts/` 复制到 `bin\<Platform>\<Config>\ts`，因此产物无需星际安装目录即可运行；
只有重新导出地形集时才需要 `--extract-ts`，MPQ 已随仓库放在 `mpq/`（见 `doc/03-terrain/tileset-pipeline.md`）。

输出目录同时生成：

- `SCMDPlus.exe` — Win32 GUI 编辑器；
- `SCMDCore.dll` / `SCMDCore.lib` — C ABI 核心库；
- `scmd_cli.exe` — 示例 CLI。

## C ABI 与 CLI

公开头文件为 `src/api/scmd_core.h`：opaque 句柄 + UTF-8 字符串 + `__cdecl`，
调用约定与结构体布局适合 C# P/Invoke（`CallingConvention.Cdecl`、`UnmanagedType.LPUTF8Str`）。

```
scmd_cli info <map.scx> [--ts <dir>]
scmd_cli render <map.scx> <out.bmp> [--scale 2|4|8|16|32] [--ts <dir>]
scmd_cli new <out.scx> <width> <height> [--era 0..7] [--ts <dir>]
scmd_cli extract-ts <game-dir> <out-dir>
```

WPF 等外部程序使用时，把对应位数的 `SCMDCore.dll`（`bin\Win32\...` 或 `bin\x64\...`）放到自身输出目录，
并保证地形集位于 `ts/` 或传入 `--ts` 对应目录。WPF 默认 AnyCPU 在 64 位系统上以 x64 运行，应使用 x64 版本。

## 当前状态

已完成地图读写（MPQ / scenario.chk 全段往返）、地形集渲染与地形/单位/装饰物/SP 单位编辑、
战争迷雾、地点描绘、触发器/字符串/玩家势力/升级限制、MPQ 附加文件管理与备份恢复；
25 个对话框全部接入，四配置（Debug/Release × Win32/x64）构建通过，
主要数据层与菜单命令均有 `--*-test` 无头回归。
对话框控件/交互快照基准见 `doc/05-ui/dialog-*-baseline.txt`，
本地/CI 用 `tools/dialog-ui-check.ps1` 同时校验（`-Mode update` 可重建基准；
存在 `.<环境标签>.txt` 变体时优先使用）。CI 以 Debug/Release × Win32/x64 矩阵运行。
已验证的格式事实记录在 `doc/` 下，代码里以 `// VERIFIED:` 标注出处。
