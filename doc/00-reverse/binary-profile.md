# ScmDraft2 二进制画像

对象：`E:\Games\PCGames\SC1BWAI\[CN]ScmDraft2.exe`（x86 汉化版）

## 基本属性（VERIFIED）

| 项 | 值 |
|---|---|
| 文件大小 | 1 289 216 B |
| 格式 | PE32, machine 0x014C (i386), 4 个节 |
| 节布局 | `.text` va 0x1000 / vsize 0x7904F / raw 0x79200 / 熵 6.53 |
| | `.rdata` va 0x7B000 / vsize 0x28B9C |
| | `.data` va 0xA4000 / vsize 0x12994 |
| | `.rsrc` va 0xB7000 / vsize 0x8B9A8 / 熵 7.20 |
| overlay | 无 |
| 加壳 | 无（`.text` 熵与 vsize≈rawsize 均正常） |

## 编译器判定（VERIFIED）

- 调试目录 type=2 (CodeView)，记录为 `RSDS`，69 字节，内含原始 PDB 路径：
  `s:\ScmDraft 2.0 B\Release\ScmDraft 2.0 B.pdb`
- 无 Delphi 特征（无 `TPF0` / `DVCLAL` / `TForm`）
- 无 MFC 特征（无 `Afx`）、无 Qt、无 .NET（无 `mscoree`）
- 结论：**MSVC C++ / Win32 API / CRT 静态链接**，原始工程名 `ScmDraft 2.0 B`，Release 配置。

## 导入表（VERIFIED，共 11 个 DLL / 255 个函数）

| DLL | 函数数 |
|---|---|
| KERNEL32.dll | 104 |
| USER32.dll | 93 |
| GDI32.dll | 26 |
| SHELL32.dll | 10 |
| ADVAPI32.dll | 8 |
| WININET.dll | 4 |
| comdlg32.dll | 4 |
| ole32.dll | 3 |
| COMCTL32.dll | 1 |
| DSOUND.dll | 1 |
| WINMM.dll | 1 |

> 导入表里没有 RichEdit。程序通过资源对话框 + 字符串 `RichEdit20A`（UTF-16）使用 Rich Edit 2.0 控件。

## 对逆向工作的含义

- 未加壳、无混淆、导入表干净 → 静态反编译（IDA / Ghidra）可直接得到函数级伪代码。
- 无 RTTI 名称（MSVC Release），类名/函数名不可恢复，只能靠导入表 + 字符串 + 资源 ID 定位。
- `s:\...\ScmDraft 2.0 B.pdb` 说明原工程目录名，但 PDB 未随程序发布，符号不可得。

## 复现方法

用自写的 PE 解析脚本读取（不执行目标程序）：DOS 头 `0x3C` → `PE\0\0` → 可选头数据目录
（index 1 导入表、index 2 资源表、index 6 调试目录），再按 RVA→文件偏移映射读取。
