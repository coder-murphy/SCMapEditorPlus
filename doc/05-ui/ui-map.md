# UI 清单与业务映射

## 状态

已从 `[CN]ScmDraft2.exe` 的 `RT_DIALOG` 导出 **25 个对话框 / 324 个控件**，
落到 `src/res/dialogs.rc`（`DIALOGEX`，ID 保持原值），由 `resources.rc` include。
明细报告见 `doc/00-reverse/dialogs-report.md`。

代码入口：`src/ui/dialogs.h/.cpp`（表 + `ShowRecoveredDialog`），
菜单 `Tools → Recovered UI (ScmDraft2)...` 打开 155 号做冒烟验证。

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

## 验证（VERIFIED）

自动化脚本：`SCMDPlus.exe --dialog=<id>` 逐个启动 → 枚举该进程的 `#32770` 窗口 → 校验标题 → `WM_CLOSE` → 检查退出码。

- **22 / 25 独立可开且标题正确**：103,155,163,164,166,167,169,171,172,173,174,175,179,180,181,182,183,187,188,189,191,192
- **184 / 185 / 186 不可独立弹出**：模板样式 `0x50080048` 含 `WS_CHILD`，是嵌在触发编辑器里的子面板；
  已加 `childPanel` 标记，`ShowDialog` 记日志并返回 false
- 构建：343 个函数通过

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

1. 184/185/186 接进 181 触发编辑器（`CreateDialogParamW` 到父窗口）；
2. 各对话框控件 ID 语义化命名（当前保留原程序数值，便于与二进制对照）；
3. 位图 134/151/152 尚未导出。
