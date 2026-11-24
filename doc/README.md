# doc —— 按业务域组织

与 `src/` 同构：一个业务域一个目录，一个目录一份主文档。

| 目录 | 对应源码 | 内容 |
|---|---|---|
| `00-reverse/` | —（逆向产出，不属于业务域） | 二进制画像、资源清单 |
| `01-archive/` | `src/archive/` | MPQ 容器格式与已验证规则 |
| `02-chk/` | `src/chk/` | CHK 段结构 |
| `03-terrain/` | `src/terrain/` | 瓦片管线、崖壁族 |
| `04-units/` | `src/units/` | 单位记录布局 |
| `05-ui/` | `src/ui/` | 对话框/菜单清单与功能映射 |
| `06-platform/` | `src/platform/` | Windows 兼容性问题 |
| `07-editing/` | `src/map/edit_history` | 撤销/重做（快照式） |
| `TODO.md` | — | 后续任务与优先级（P0–P3） |

标注约定：

- **VERIFIED** —— 在本机实测过（有数据、有复现步骤），可直接作为实现依据。
- **TODO** —— 尚未验证，不要当结论用。

原始程序的完整 PE 资源反解产物在仓库根目录 `reverse/scmdraft2/`
（`raw/` 原始字节 + `converted/` 位图/图标/字符串/版本 + `resources.csv`），
可用 `tools/dump_original.ps1` 重新生成。

测试入口：`SCMDTests.exe`（无头，`src/tests/`）承载模型/格式/渲染回归；
UI 探针仍由 `SCMDPlus.exe --*-test` 运行，两者由 `tools/regression.ps1` 统一调度。

AI 技能：`.trae/skills/starcraft-map-assets/` 封装原版 ScmDraft2 解包、星际 MPQ/地形集
提取与资源组织流程；后续开发地图编辑器时优先加载该技能。
