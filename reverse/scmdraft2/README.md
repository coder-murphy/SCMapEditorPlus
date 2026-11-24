# 原版程序反解产物（Scmdraft 2）

- 来源：`F:\Games\单机游戏\starcraft\[CN]ScmDraft2.exe`
- 大小：1289216 B
- SHA-256：`7B17D5AA71B9024438A54963870561DB0854270567A170FF9900CDE772B7802D`
- 版本：0, 8, 0, 0（Scmdraft 2 / Stormcoast Fortress）
- 原始文件名：Scmdraft.exe
- 导出汇总：resources=42 raw=42 converted=7

## 目录

- `raw\<类型>\<名称>_<语言>.bin`：PE 资源原始字节（menu/dialog/accelerator/rcdata 等）；
  自定义 `BIN` 类型输出到 `raw\type_BIN\`（避免被 `.gitignore` 的 `bin/` 规则忽略）
- `converted\bitmap\*.bmp`：位图（DIB + BMP 文件头）
- `converted\icon\*.ico` / `converted\cursor\*.cur`：图标/光标组（合并 RT_ICON / RT_CURSOR）
- `converted\string\*.txt`：字符串表（每块 16 条，`id<TAB>文本`）
- `converted\version\*.txt`：版本资源里可读的 Unicode 字符串（原始 BIN 仍保留）
- `converted\manifest\*.xml`：清单
- `resources.csv`：全部资源清单（类型/名称/语言/大小/路径）

## 重新生成

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dump_original.ps1
```

可读的菜单/对话框/加速键 `.rc` 文本已在 `src/res/`（menu.rc / dialogs.rc / accel.rc）。
