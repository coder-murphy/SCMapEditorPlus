# Windows 10/11 的 riched32 空壳问题

## 现象

编辑器启动即弹：

```
[CN]ScmDraft2.exe - 无法找到入口
无法定位程序输入点 RichEdit10ANSIWndProc 于动态链接库 C:\Windows\SYSTEM32\RICHED32.DLL 上。
```

## 原因（VERIFIED）

| 文件 | 大小 | 导出 |
|---|---|---|
| `C:\Windows\SysWOW64\riched32.dll` | **8 704 B**（Win7 为 32 768 B） | **0 个**（连导出目录都没有） |
| `C:\Windows\SysWOW64\riched20.dll` | 526 336 B，v5.31 | 含 `RichEdit10ANSIWndProc`、`RichEditANSIWndProc` |
| 游戏目录旧 `Riched20.dll` | 315 392 B | 只有 7 个，**缺** `RichEdit10ANSIWndProc` |

Win7 上 `riched32.dll` 把 `RichEdit10ANSIWndProc` 转发给 riched20；Win10 起该 DLL 变成空壳，
任何对它的静态导入都会让加载器直接报错。

## 已采用的修复

1. 在程序目录放一个 1.5 KB 的转发 DLL `riched32.dll`，只导出
   `RichEdit10ANSIWndProc -> RICHED20.RichEdit10ANSIWndProc`；
2. 把旧的本地 `Riched20.dll` 改名为 `Riched20.dll.bak`，让转发落到系统的 5.31。

> 第 2 步是必需的：程序目录优先于 System32，旧的 `Riched20.dll` 挡在转发链前面且缺该导出。

验证方式：用 32 位 PowerShell（与编辑器同位数、走同一套加载器）加载转发 DLL 并
`GetProcAddress("RichEdit10ANSIWndProc")`，应返回非 0，且 `Modules` 里出现
`C:\Windows\SYSTEM32\RICHED20.DLL 5.31`。

## 对本项目的约束

- **不要**静态导入 `riched32.dll` 的任何符号。
- 需要富文本时直接用 `riched20.dll`（`RichEdit20A` / `RichEdit20W` 窗口类）或
  `msftedit.dll`（`RichEdit50W`），并在代码里做 `LoadLibrary` 失败兜底。
- 发布时不要把系统 riched20.dll 打包进程序目录（会造成版本劫持）。
