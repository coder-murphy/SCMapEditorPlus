# UI 清单与业务映射

## 状态

已从 `[CN]ScmDraft2.exe` 的 `RT_DIALOG` 导出 **25 个对话框 / 324 个控件**，
落到 `src/res/dialogs.rc`（`DIALOGEX`，ID 保持原值），由 `resources.rc` include。
明细报告见 `doc/00-reverse/dialogs-report.md`。

代码入口：`src/ui/dialogs.h/.cpp`（表 + `ShowRecoveredDialog`），
菜单 `帮助 → 恢复的界面...` 打开 155 号做冒烟验证。

## 映射表

| ID | 常量 | 标题 | 用途 | 业务域 |
|---|---|---|---|---|
| 103 | `IDD_DLG_103` | Scmdraft2.0.8.0[CsgA]NaKa汉化版 | 关于框 | `src/ui/` |
| 155 | `IDD_DLG_155` | Scmdraft 2.0基本设置 | 全局设置（MPQ 列表、撤销次数、默认资源） | `src/app/` |
| 163 | `IDD_DLG_163` | 兼容性警告 | 启动兼容性提示 | `src/ui/` |
| 164 | `IDD_DLG_164` | SP单位属性 | 单人单位属性 | `src/units/` |
| 166 | `IDD_DLG_166` | 方格设置 | 网格/地形刷设置 | `src/terrain/` |
| 167 | `IDD_DLG_167` | Scmdraft 2.0.8.0 [CsgA]NaKa汉化版 | 版本信息 | `src/ui/` |
| 169 | `IDD_DLG_169` | 新建地图 | 新建：尺寸/地形集/玩家数 | `src/map/` |
| 171 | `IDD_DLG_171` | 编辑Location | 地点（区域）编辑 | `src/triggers/` |
| 172 | `IDD_DLG_172` | 单位名字 | 单位名称表 | `src/units/` |
| 173 | `IDD_DLG_173` | 查看备份数据 | 备份数据查看 | `src/tools/` |
| 174 | `IDD_DLG_174` | 在线升级 | 更新检查 | `src/tools/` |
| 175 | `IDD_DLG_175` | 重设地图大小 | 重设地图尺寸 | `src/map/` |
| 179 | `IDD_DLG_179` | 重命名开关 | 开关重命名 | `src/tools/` |
| 180 | `IDD_DLG_180` | 设置单位属性 | 单位属性编辑 | `src/units/` |
| 181 | `IDD_DLG_181` | 触发编辑器 | 触发器编辑主界面 | `src/triggers/` |
| 182 | `IDD_DLG_182` | 自定义刷子 | 自定义地形刷 | `src/terrain/` |
| 183 | `IDD_DLG_183` | 对话框 | 通用容器对话框 | `src/misc/` |
| 184 | `IDD_DLG_184` | 对话框(无标题) | 通用容器对话框 | `src/misc/` |
| 185 | `IDD_DLG_185` | 对话框(无标题) | 通用容器对话框 | `src/misc/` |
| 186 | `IDD_DLG_186` | 对话框(无标题) | 通用容器对话框 | `src/misc/` |
| 187 | `IDD_DLG_187` | 对话框 | 通用容器对话框 | `src/misc/` |
| 188 | `IDD_DLG_188` | 输入数量 | 数值输入 | `src/ui/` |
| 189 | `IDD_DLG_189` | 编辑字符串 | 字符串表编辑 | `src/triggers/` |
| 191 | `IDD_DLG_191` | 重叠单位 | 重叠单位处理 | `src/units/` |
| 192 | `IDD_DLG_192` | Iscript测试 | Iscript 测试工具 | `src/tools/` |

## 验证方式（VERIFIED）

1. `MSBuild /t:Rebuild` 通过（304 个函数）；
2. 解析产物 `bin\Win32\Release\SCMDPlus.exe` 的 `.rsrc`：**DIALOG = 25**，
   ID 与上表一致；MENU/ACCELERATOR/VERSION/MANIFEST 各 1；
3. 产物中能以 UTF-16 命中「触发编辑器」「新建地图」「编辑Location」「重设地图大小」等标题；
4. 程序启动正常，窗口标题 `SCMD Plus 3.0`。

## 进展（本轮：25 个全部实现）

**注册表**：`src/ui/dialog_registry.cpp` 25 条全部 `impl`；`ProcPlaceholder` 已删除。
处理过程按业务域分文件：

| 文件 | 对话框 |
|---|---|
| `ui/dlg_basic.cpp` | 103 关于、155 基本设置、163 兼容性警告 |
| `ui/dlg_input.cpp` | 188 输入数量、191 重叠单位、179 重命名开关 |
| `ui/dlg_string.cpp` | 189 编辑字符串 |
| `ui/dlg_map.cpp` | 169 新建地图、175 重设地图大小 |
| `ui/dlg_triggers.cpp` | 181 触发编辑器、171 编辑 Location |
| `ui/dlg_units.cpp` | 164 SP 单位属性、180 设置单位属性、172 单位名字 |
| `ui/dlg_terrain.cpp` | 166 方格设置、182 自定义刷子 |
| `ui/dlg_tools.cpp` | 173 备份、174 升级、192 Iscript |
| `ui/dlg_misc.cpp` | 167 MPQ 列表、183 标签容器、184 列表容器、185/186 列表编辑、187 富文本 |

返回值通过 `dlg_types.h` 的结构体与 `LastInputValue()` / `LastEditedString()` 取回。

## 主菜单与加速键（P3-10，VERIFIED）

来源：`[CN]ScmDraft2.exe` 的 `RT_MENU 109`（8 888 B）与 `RT_ACCELERATOR 109/176/177/178`，
导出到 `src/res/menu.rc` / `src/res/accel.rc`，由 `resources.rc` include。

### MENU 资源格式（MENUEX 变体）

- 头 8 字节：`u16 version=1`、`u16 offset=4`、`u32 helpId`
- 每项：`u32 type`、`u32 state`、`u32 menuId`、`u16 resInfo`、文本（0 结尾 UTF-16）
- `menuId == 0xFFFF` 为弹出菜单；`resInfo & 0x01` 的项在文本后多 4 字节
- `resInfo & 0x80` = 当前层最后一项；弹出项带 0x80 时，其子菜单结束后还要关闭父层
- 每项按 4 字节对齐
- 资源尾部有 2 460 字节英文调试菜单残留（本地化后未清理的死数据），解析到顶层闭合即停

统计：**10 个顶层菜单 / 224 项 / 18 个分隔符**
（文件 编辑 选项 显示选项 剧本设置 高级 触发 窗口 帮助 调试）。

### 命令 ID

保留原值（0x8000 段为主，另有 0x68=关于、0x69=退出）：

| 功能 | ID |
|---|---|
| 新建 / 打开 / 关闭 | 0x8005 / 0x801B / 0x8007 |
| 保存 / 另存为 | 0x8087 / 0x8088 |
| 撤销 / 恢复 | 0x807F / 0x8080 |
| 编辑层 → 地形 | 0x803A |
| 触发 → 编辑触发 | 0x810F |
| 帮助 → 关于 / 文件 → 退出 | 0x68 / 0x69 |

`command_ids.h` 已改用这些原值；菜单项全部导入（未实现的命令统一走
`Log("[ui] command ...")`）。原菜单没有“恢复的界面”入口，运行时追加到「帮助」子菜单
（0x8FFF，仅本工程调试用）。

### 加速键

| 表 | 条数 | 用途 |
|---|---|---|
| 109 | 34 | 主菜单（Ctrl+N/O/S/Z/Y、Ctrl+T/D/F/L/U/R 层切换、Alt+U/F/G/L/E 网格、Esc/Del/Enter） |
| 176 | 2 | 通用对话框：Enter=确定、Esc=取消 |
| 177 / 178 | 7 / 6 | 列表框编辑 / 列表对话框 |

`MainWindow` 启动时 `LoadAcceleratorsW(IDR_ACCEL_MAIN)`，消息循环先
`TranslateAcceleratorW`；Ctrl+Z / Ctrl+Y 因此改由加速键派发到 `Undo()/Redo()`。

### 验证

```
SCMDPlus.exe --menu-test
exit=0
menuTop=10  menuItems=224  menuSeps=18  accel=34
idFileNew=0x8005  idEditUndo=0x807f  idHelpAbout=0x68  esc=1
labels=文件|编辑|选项|显示选项|剧本设置|高级|触发|窗口|帮助|调试
```

GUI 集成（`--open=<map>`）：

- 主窗口菜单 10 项，标题与上表一致；「帮助」子菜单 9 项，末项为「恢复的界面...」
- `WM_COMMAND 0x68` → 关于框（标题 `SCMD Plus 3.0`）
- `WM_COMMAND 0x810F` → 触发编辑器 181，内含 3 个 `#32770` 子面板

## 验证（VERIFIED）

自动化脚本：`SCMDPlus.exe --dialog=<id>` 逐个启动 → 枚举该进程的 `#32770` 窗口 → 校验标题 → `WM_CLOSE` → 检查退出码。

- **22 / 25 独立可开且标题正确**：103,155,163,164,166,167,169,171,172,173,174,175,179,180,181,182,183,187,188,189,191,192
- **184 / 185 / 186 为子面板**：模板样式 `0x50080048` 含 `WS_CHILD`，已用
  `CreateDialogParamW` 嵌入 181 触发编辑器（见下节），`ShowDialog` 仍拒绝独立弹出
- 构建：343 个函数通过

## 触发编辑器子面板（P2-9，VERIFIED）

`ui/dlg_triggers.cpp` 在 181 的 `WM_INITDIALOG` 里创建三个子对话框：

| 资源 | 处理过程 | 用途 |
|---|---|---|
| 184 | `ProcListViewHost` | 触发列表视图（`SysListView32`） |
| 185 | `ProcListEditorA` | 条件编辑（新建/编辑/复制/删除/上移/下移） |
| 186 | `ProcListEditorB` | 动作编辑（同上） |

- 位置：181 的「触发」分组框（1113）内部，`MapDialogRect` 换算成像素后 `SetWindowPos`，
  尺寸随 DPI/字体变化。
- 默认 `mode=0`：只显示原始触发列表（182）与左侧树（1110），三个子面板隐藏。
- 「修改」(1101) 循环切换 `0 → 184 → 185 → 186 → 0`，面板显示时隐藏被遮住的 182/1110。
- 状态挂在 `GWLP_USERDATA`，`WM_DESTROY` 释放。

验证脚本（`--dialog=181` → `EnumChildWindows`）：

```
child dialogs (#32770) = 3            # 3 个面板均已创建，尺寸 546x345
mode0 visible=0                        # 默认不显示
after click 1..4 visible=1,1,1,0        # 每次恰好一个面板可见，第 4 次回到列表
exited = True  exit code = 0
```


## 本轮修掉的三个真实 bug

1. **103 打开失败（错误 1814）**：`resource.h` 的中文注释在 RC 的 CP936 解码下吞掉换行，导致第一条
   `#define IDD_DLG_103` 失效，RC 把该对话框编成**字符串名资源**，`MAKEINTRESOURCEW(103)` 因此找不到。
   处理：`resource.h` 注释改为纯 ASCII（保留 BOM）；重建后 25 个 ID 全为序号资源。
2. **187 富文本对话框**：模板含 `RichEdit20A`，需先注册窗口类 → `App::Run` 中 `LoadLibraryW(L"riched20.dll")`。
3. **103 的图标静态控件**：其文本是图标资源名 `"130"`，本工程不导出该图标，创建时会失败 → 从模板移除该控件
   （仅影响关于框图标显示）。

## 构建注意

沙箱对 `E:\GameProjects` 覆盖写受限时，用新输出目录构建，例如：

```
MSBuild SCMDPlus.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 ^
        /p:TrackFileAccess=false /p:IntDir=<新目录>\ /p:OutDir=<新目录>\
```

本轮产物位于 `bin2\Win32\Release\SCMDPlus.exe`（`bin\Win32\Release\SCMDPlus.exe` 被系统句柄占用，
无法覆盖；关闭占用后把 bin2 的产物复制过去即可）。

## 后续

1. ~~185/186 的列表内容接到 `TriggerSection` / `LocationTable` / `StringTable`~~（P3-11d 已完成）；
2. 主菜单中尚未实现的命令逐项接线（保存/关闭、层切换、网格尺寸已完成，其余继续）；
3. 已用控件语义别名接入触发编辑器；其余控件保留原程序数值，便于与二进制对照；
4. ~~位图 134/151/152 尚未导出~~（P3-12 已导出到 `src/res/assets/`）。

## 文件菜单闭环（P3-11）

文件菜单的 5 个命令已全部接线，并补了脏标记与标题显示：

| 命令 | ID | 行为 |
| --- | --- | --- |
| 新建 | 0x8005 | 询问保存 → `MapDocument::CreateNew(64×64, ERA 0)`，未命名、脏标记置位 |
| 打开 | 0x801B | 询问保存 → 文件对话框 → `OpenMap` |
| 关闭 | 0x8007 | 询问保存 → `Close()` 释放文档与地形集，清空标题 |
| 保存 | 0x8087 | 有路径直接写，否则转到另存为 |
| 另存为 | 0x8088 | `SaveFileDialog` 选路径 → `Save()`，成功后同步路径与标题 |
| 退出 | 0x0069 | 询问保存后销毁窗口 |

- 未命名地图在标题中显示为“SCMD Plus 3.0 - 未命名  [64x64 …，已修改]”。
- `WM_CLOSE` 与“退出”共用 `MaybeSave()`（是/否/取消）。
- `MapDocument` 新增 `CreateNew` / `Close`，`IsOpen()` 改由 `opened_` 判定（未保存的新图也算已打开）；
  `Save()` 成功后回填 `path_` 并清脏。

无头回归 `--file-test=<输出.scx>`：

```
created=1 dirtyNew=1 unnamed=1 saved=1 cleanAfterSave=1 pathSaved=1
reopened=1 sizeOk=1 painted=16/16 closed=1 freshOk=1
```


## 编辑层 / 方格 / 显示选项（P3-11b）

「编辑 → 编辑层」「选项 → 方格」「显示选项」三组菜单已接线，勾选状态随命令同步：

- **编辑层**（0x803A/3B/3E/3C/3D/3F）：地形→地形刷，单位/SP单位→放单位，装饰物/迷雾/地点→仅查看。
  Ctrl+T/D/F/L/U/R 与 F1 之外的数字键 1/2 也可切换。
- **方格**：微小 8 / 较小 16 / 普通 32 / 较大 64 / 极大 128（单位：源像素，随缩放换算到屏幕），
  禁止关闭；自定义打开 166「方格设置」，支持 X/Y 尺寸与偏移。
- **方格颜色**：黑/灰/白/绿/红/蓝，其他调用系统取色器。
- **显示选项**：单位、装饰物、SP单位、所有颜色、Zerg地皮、特殊状态、隐藏小雷达、颜色循环、
  死亡效果、战争迷雾、显示Location 均可勾选；其中单位与 Location 立即影响绘制
  （Location 按 MRGN 矩形画绿框并标序号），其余为状态位。
  原资源里被灰掉的 4 项（所有颜色/单位/装饰物/SP单位）在创建时解除灰显。

无头回归 `--menu-cmd-test`：

```
layerInit=1 layerUnits=1 layerLoc=1 layerBack=1
gridLarge=1 gridOff=1 gridBack=1 gridRed=1
unitsOff=1 unitsOn=1 fogOn=1
```


## 核心对话框接线（P3-11c）

| 菜单命令 | ID | 对话框 / 行为 |
| --- | --- | --- |
| 文件 → 新建 | 0x8005 | 169 新建地图（16..256，默认 64）→ `CreateNew` |
| 剧本设置 → 变更地图尺寸 | 0x8106 | 175 → `MapDocument::Resize`（MTXM 平移、DIM 更新、越界单位丢弃） |
| 剧本设置 → 地图描述 | 0x8041 | 187 富文本，读写 STR[2] |
| 剧本设置 → 编辑字符串字符串 | 0x8046 | 189，读写 STR[1]（地图名） |
| 剧本设置 → 势力设定 | 0x8042 | 183 标签容器 |
| 剧本设置 → 单位设定 | 0x8044 | 172 单位名字 |
| 剧本设置 → 升级/科技设置 | 0x8043/0x8045 | 184 列表容器 |
| 剧本设置 → 到开始位置 | 0x80C1 | 视口居中到首个出生点（214） |
| 编辑 → 属性 | 0x809C | 180，按光标所在单位回填并写回 HP/护盾/能量/资源 |
| 编辑层 → 复制/剪切/粘贴/刷子 | 0x810E | 182 自定义刷子 |
| 窗口 → 刷子窗口 | 0x8114 | 182 |
| 选项 → 编辑器设定 | 0x80C4 | 155 |
| 高级 → 检查更新 / 打开备份数据 | 0x8071 / 0x80F9 | 174 / 173 |
| 帮助 → 关于 | 0x0068 | 103（替换原 MessageBox） |

- 新增 `LastDialogResult()`，调用方用 `IDOK` 判断是否应用对话框结果。
- 166/180/187/189 支持打开前写入初值（`SeedGridSettings` / `SeedUnitProps` /
  `SeedRichText` / `SeedEditedString`）。
- `StringTable::Set` 允许扩写到指定下标；`Save` 在没有 MRGN/STR 段时按需新建。
- 尚无专属对话框的项（编辑声音、装饰物窗口、在线帮助等）记录日志并提示音。

## 触发内容编辑（P3-11d）

181 触发编辑器已从占位列表改为读写 `MapDocument::triggers()`：

| 区域 | 行为 |
| --- | --- |
| 182 触发列表 | 每条显示序号、生效玩家、条件/行动数、禁用标记 |
| 184 详情列表 | 报表视图：「类别 / 名称 / 内容」，逐条列出当前触发的条件与行动 |
| 185 条件列表 | 显示条件类型、玩家、数量、单位、地点（MRGN 名称）、比较方式 |
| 186 行动列表 | 额外显示字符串文本（STR 内容）、数量与地点 |
| 新建/复制/删除/上移/下移 | 直接修改 `TriggerSection`，越界自动裁剪，并标记脏数据 |
| 修改按钮 | 循环切换 原始列表 → 184 → 185 → 186 |
| 编辑条件/行动 | 新增 193/194 两个对话框，可修改全部字段（类型号支持 >23/>57 的自定义值） |

- 位置名称取自 `MRGN.stringId` → `STR`，空名称回退为 `#下标`；
- 子面板改动后通过 `WM_APP+181` 通知 181 统一刷新，避免多处状态分叉；
- 对话框通过 `SetDialogDocument()` 拿到当前地图，关闭后清空。

GUI 自动化验证（`(2) Friends In Jungle.scx`）：

```
triggers=13  conds=1  acts=1  detail rows=2
mode visible: 184/185/186 每次恰好一个
行动 1→2→1，条件 1→2→1，触发 13→14→13
编辑条件组合框 24 项，编辑行动组合框 58 项
关闭后标题含「已修改」
```

无头回归 `--trig-test=<out.scx>`：

```
blocks=2->3  cond=1 act=1  lossless=1
parse=1 condOk=1 actOk=1 flagOk=1 playerOk=1
saved=1 reopened=1 reopenFields=1 reopenLossless=1
```

### 回归（P3-11c）

无头回归 `--resize-test`（192×192 → 96×96，偏移 1,1）：

```
dimOk=1 tileMove=1 borderZero=1 unitsInBounds=1 units=144->33
dirty=1 saved=1 reopened=1 reopenDim=1 reopenTile=1
```

