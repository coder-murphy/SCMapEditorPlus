# 后续任务（TODO）

> 更新：2026-11-22。优先级：P0 编辑核心 → P1 数据完整性 → P2 工具 → P3 验证/逆向。
> 勾选状态以代码与回归结果为准；完成后请同步对应的 `doc/` 业务文档。
>
> **暂停点（2026-11-24）**：已完成 UNIT 0..227 碰撞盒、MPQ 文件管理（167）、
> 装饰物窗口（0x8004）、Locations 三种描绘模式与 SP 对齐方格、192 动画预览与条件输入、
> 169/175 尺寸对话框对齐（尺寸预设 / 地形集切换）、全对话框按钮语义审计与
> `--dialog-audit-test` 自动化回归（含基准快照 diff）；四配置构建通过，全量回归通过。
> 下一步见文末「建议顺序」。

## 已完成（近期）

- [x] 地形刷调色板窗口（8 列网格、滚动、鼠标/键盘选择、吸管同步）
- [x] `--extract-ts` 从 StarDat/BrooDat 导出 8 套地形集（32 文件 / 13 517 280 B）
- [x] MPQ `FIX_KEY` 密钥公式与未加密扇区表读取修复
- [x] 地形集查找回退：`--ts` → `exe\ts` → 工程根 `ts`
- [x] 地形集 `ts/` 入库，构建后自动复制到输出目录（产物脱离游戏目录运行）
- [x] `StarDat.mpq` / `BrooDat.mpq` 入库到 `mpq/`，可离线重导出地形集
- [x] 核心能力 C ABI 封装（`SCMDCore.dll`）与示例 CLI（`scmd_cli.exe`）

## P0 编辑核心

- [x] **单位放置参数与单位面板**
  - 新增 195 单位面板（类型 / 玩家 / 资源，含常用类型预设），入口：窗口 → 单位面板（0x8FFD）。
  - 放置时使用参数并写回 UNIT；180 号对话框可继续修改。
  - 已接 0x8032/0x80E9/0x80EA 与重叠/自动连接/矿基距离/剪贴板装饰物的菜单状态；
    “任意放置”已影响可行走校验。
  - 回归：`--unit-param-test`。
- [x] **180 单位属性状态位回填 / 写回**
  - 修正 UNIT 字段语义：+12 是“特殊属性有效掩码”，+26 才是激活状态位，
    +24 是机库/载弹数量（原先误当作队列数）。
  - 180 打开时按 +26 回填 5 个复选框：无敌=bit4、幻影=bit3、钻地=bit1、隐形=bit0、升起=bit2；
    确定后写回 +26，并把勾选位并进 +12（否则游戏会忽略未标记为有效的状态）。
  - “子弹”字段写回 +24；打开时从 +24 回填。
  - 回归：`--dialog-audit-test` 新增 `unitProps180`
    （种子 0x11 → 显示无敌+隐形；改为 0x0F + 子弹 9）。
- [x] **放置规则细化**
  - [x] 附属单位自动连接（0x80B8）：Comsat/Silo→Command Center、Machine Shop→Factory、
    Control Tower→Starport、Physics Lab/Covert Ops→Science Facility；
    光标落在宿主右侧 2×2 附属槽内时吸附到槽左上角。
    ID 已按 staredit wiki / scmscx 资料校正：115 Control Tower、116 Science Facility、
    117 Covert Ops、118 Physics Lab、120 Machine Shop。
  - [x] 矿物与基地距离限制（0x80BF）：开启后矿物/气矿必须距出生点 ≤12 格，否则拒绝并写入输出面板。
  - [x] 建筑正常规则（0x8032）：对已知 Terran 建筑按碰撞盒检查占地瓦片可通行，
    并与其它已知建筑做矩形相交检测（数据来源：staredit wiki List of units）。
  - [x] 地面/空中重叠（0x8015/0x8109）：按碰撞盒做同层相交检测；空中单位清单见代码，
    默认都拒绝同层重叠，勾选后放行；地面与空中可同格。
    碰撞盒覆盖 UNIT 0..227（staredit wiki 数据），出生点 214 作为标记不参与碰撞；
    建筑正常规则覆盖 106..175（不含 128/129 水晶）。
  - 回归：`--unit-param-test` 新增 `cc/addon/tower/farRejected/nearMineral/addonOk/towerOk/mineralOk/overlapRejected/stack/air`。
  - 代码：`src/ui/main_window.cpp`、`src/units/`
- [x] **选择与剪贴板**
  - 框选工具（0x8FFB / 数字键 4）、全选/取消、复制/剪切/粘贴（0x80E6/80E7/80E8/809A）。
  - 内部剪贴板可跨地图；粘贴替换目标区单位并重新分配 serial。
  - 重叠所选单位（0x811A）、删除非法单位（0x8105，越界/重复 serial）。
  - 选项 → 剪切 复制 粘贴 → 包括装饰物（0x811C）：复制/剪切/粘贴同步处理选区内 DD2。
  - 回归：`--clip-test`（新增 `doodadNo/doodadPaste/doodadCut`）。
- [x] **撤销范围**
  - `EditCommand` 增加 TRIG/MRGN/STR 序列化快照；触发编辑器、字符串编辑与地点编辑已包进事务。
  - 回归：`--undo-test` 新增 `metaUndo/metaRedo`。
- [x] **刷子体系完善**
  - 182 自定义刷子：添加/删除、打开/保存 `.scb`、按名称/尺寸排序、单格/多格过滤、双击应用。
  - 多格图案刷：按选区生成图案、居中落笔、HUD 显示名称与尺寸、进入撤销栈。
  - 调色板筛选：可走性 / 类型 / 组区间（10 列网格）。
  - 回归：`--brush-test`、`--brush-pattern-test`。
- [x] **崖壁边缘自动铺设**
  - `EdgePlacer::StampOutward`：从甲板做 4 邻域 BFS，向虚空外侧压印 1..3 圈；
    按“甲板在哪一侧”选 N/E/S/W 组族，组沿边界坐标循环、index 取纹理变体（0..2）。
  - 入口：调试 → 自动铺设崖壁（0x8FFE，运行时追加）；整次铺设进入撤销栈。
  - 回归：`--edge-test`（合成 Space 甲板，检查 1..3 圈齐全、甲板不被改写、重复调用不再改动）。
- [x] **缩放档位**
  - 选项 → 地图显示比率：25% / 33% / 50% / 66% / 100% / 150% / 200% / 300% / 400%
    （32 px/瓦片为 100%，即 8/11/16/21/32/48/64/96/128 px）。
  - 滚轮缩放步进同步扩展为同一序列；菜单勾选随当前比例回显。
  - 全图 DIB 约 256 MB 上限：超出时提示并保持原比例；打开/新建/重设尺寸时自动下调到可用档位。
  - 渲染器已放宽到 1..128 px/瓦片并用最近邻采样，修复 33%/66%/150%–400% 等非整除档位
    渲染失败导致视图清空的问题。
  - 回归：`--menu-cmd-test` 新增 `zoom`（配合 `--open`）；`--tileset-render-test` 增加
    11/21/48/128 px 与 32 px 参考图的逐像素比对（`zoom=1`）。

## P1 地图数据完整性

- [x] **玩家/势力/队伍模型**
  - 新增 `PlayerRoster`：OWNR/SIDE 12 槽、FORC 20 B（8 玩家→4 队伍 + 名称 STR + 标志）、COLR 8。
  - 196 玩家/势力设定（0x8042）：控制者/种族/队伍/颜色、Force 名称 STR 与标志；编辑进入撤销栈。
  - 回归：`--player-test`（解析、修改、撤销/重做、保存重开、UPGR 等段保持）。
- [x] **升级/科技限制面板**
  - `UpgradeTechRestrictions`：UPGR(46)/PTEC(24)/PUPx(61)/PTEx(44)，12 玩家 + 全局默认 + useDefaults。
  - 197 面板（0x8043/0x8045）：原版/资料片切换、全局/玩家、升级最大/初始等级、科技可用/初始拥有。
  - 编辑进入撤销栈；回归：`--restriction-test`。
- [x] **装饰物 / 战争迷雾数据层**
  - DD2（8 B/条：类型/坐标/玩家/启用）与 MASK（每格 1 B，bit0..7 对应玩家 1..8）。
  - 装饰物层绘制品红色空心标记；迷雾层用 AlphaBlend 半透明覆盖，左键单击/拖动切换。
  - 选项 → 战争迷雾 → 默认为未探索/已探索（0x80D3/0x80D4）整图填充。
  - DD2/MASK 只重写被修改的段，并纳入撤销栈；回归：`--decor-test`。
- [x] **装饰物窗口（0x8004）与允许任意放置（0x8010）**
  - 窗口 → 装饰物窗口打开独立工具窗口：类型 0..65535、玩家 1..8、禁用标志；
    列表汇总当前地图已使用的 DD2 类型（类型 × 条数），单击填入类型。
  - 装饰物层左键在瓦片中心放置、右键删除该格全部记录；整次操作进入撤销栈。
  - 默认只允许放在可行走地形；选项 → 装饰物 → 允许任意放置（0x8010）开启后跳过校验，
    菜单勾选与窗口复选框双向同步。
  - 回归：`--doodad-test`（放置/字段写回/禁用/阻挡拒绝/任意放置/删除/撤销重做/保存重开）；
    `--menu-cmd-test` 新增 `doodadAny`。
  - 代码：`src/ui/doodad_palette.*`、`src/ui/main_window.cpp`、`src/map/map_document.*`。
- [x] **SP 单位（THG2）数据层与绘制**
  - `SpriteTable` 10 B/条（类型/坐标/玩家/未用/标志），长度异常整段原样保留。
  - SP单位层与显示选项打开时绘制青色标记：bit12 为空心圆，否则空心方框。
  - 数据只在地图修改后写回 THG2；回归：`--sprite-test`。
- [x] **SP 单位属性（164）**
  - 窗口 → SP单位窗口（0x80A4）或 SP 单位层双击标记打开；列表对应 THG2 记录。
  - 可编辑玩家、Bit0..11、Flipped（bit13）、禁用（bit15）、普通SP/单位SP（bit12）与未用字节。
  - 关闭即应用，整次编辑进入撤销栈（THG2 快照）；资源中置灰的 1039/1040 已启用。
  - 回归：`--sprite-test` 覆盖字段修改、撤销重做与保存重开。
- [x] **Locations 描绘模式与 SP 对齐方格（0x8013/0x80EB/0x80EC/0x80B7）**
  - MRGN 按实图校正为 20 字节：u32 像素坐标（right/bottom 开区间）+ u16 stringId + u16 flags；
    171 编辑、地点绘制与命中全部改为像素，旧“8 字节保留区”误读已移除。
  - 地点层空白处拖拽创建新地点：整齐的描绘（32 px）、按照方格描绘（方格设置与偏移）、
    任意描绘（像素精度）；拖拽时显示虚线预览，松开后追加 MRGN 并进入撤销栈。
  - SP 单位层左键放置 / 右键删除（类型/玩家/标志沿用最近一次 164 编辑）；
    选项 → SP单位 → 对齐方格（0x80B7）开启后吸附到瓦片中心。
  - 回归：`--loc-draw-test`（MRGN 格式、三种模式、SP 自由/对齐、撤销重做、保存重开）；
    `--location-test` 适配新布局；`--menu-cmd-test` 新增 `locMode/spAlign`。
  - 代码：`src/triggers/trigger_model.*`、`src/ui/main_window.cpp`、`src/ui/dlg_triggers.cpp`。
- [x] **显示战争迷雾在探索地区（0x80F4）**
  - 与显示选项 0x8113、编辑层子菜单 0x80CE 合并为同一个 `showFog_` 开关，
    三个菜单项的勾选状态同步；回归 `--menu-cmd-test` 新增 `fogExplored`。
- [x] **小雷达/迷雾开关**
  - 剧本设置 → 全地图放置/删除小雷达：UNIT 101 `Map Revealer`，16 格网格，
    支持 Player 1..8 与所有玩家；重复位置跳过，接近 1650 单位上限时停止。
  - 禁止/允许迷雾：按玩家清除/设置 MASK 对应位；所有玩家整字节填 0x00/0xFF。
  - 两个命令都进入撤销栈；回归：`--radar-test`。
- [x] **Location 171 → MRGN**
  - 171 面板按 MRGN 记录初始化：四边像素坐标（right/bottom 开区间）、高程标志（flags 低 6 位）、名称与 STR 下标。
  - 调用点：地点层双击矩形，或“选项 → Locations → 修改名字”（0x8108，取光标所在地点）。
  - 写回 MRGN（20 字节：u32×4 + stringId + flags）；名称写入 STR（空名复用/追加下标），整次编辑进入撤销栈。
  - 回归：`--location-test`。
- [x] **声音、任务简报、开关命名**
  - 编辑声音（0x8047）→ 198：WAV 512 槽，路径写入 STR，支持设置/清除。
  - 任务简报（0x8110）→ 199：列出 MBRF 中已有文本/目标/传输行动，更新写回 STR。
  - 开关命名：179 列表载入 SWNM 256 项，设置/还原；触发编辑器“开关命名”按钮直接打开。
  - WAV/SWNM/MBRF 数据层接入保存与撤销；回归：`--scenario-text-test`。
- [x] **显示选项细化**
  - 已实现：所有颜色（按单位类型散列着色）、隐藏小雷达（小地图见 P2）、
    颜色循环（单位标记色相随时间旋转）、死亡效果（删除单位后短暂红色 X）、
    特殊状态（`validState != 0x0018` 的单位加黄色圆环）、
    Zerg地皮（CV5 `flags & 0x0040` 的 4×4 creep 图案组，铺在虫族基地/地刺/孢子/下沉建筑下；
    7 套地形集为组 1、13 个非空槽，Installation 当前无 creep 组）。
  - 回归：`--menu-cmd-test` 的 `effects` 覆盖颜色循环 / 死亡效果 / 特殊状态 / Zerg地皮勾选回显；
    `--tileset-render-test` 增加 creep 组检测（`creep=7/7`）。
- [x] **平滑重设尺寸**
  - 175 勾选“平滑边缘”时，新增的行/列按最近的原始瓦片延伸；未勾选仍填 0。
  - MTXM 平移、DIM 更新、越界单位丢弃与迷雾搬移保持不变。
  - 回归：`--resize-test` 新增 `smooth`。
- [x] **新建 / 重设尺寸对话框对齐（169 / 175）**
  - 169：左列表为边长预设（64/96/128/192/256），选中同步宽/高；右列表为 8 种地形集；
    “创建地图”读取 ERA，`CreateNew` 写 `ERA ` 并加载对应地形集。
  - 175：打开前按当前地图宽/高/ERA 预填（`SeedResizeParams`）；左列表为当前地形集，
    右列表选择目标地形集；应用时先 `Resize`，必要时 `SetEra` 重载地形集。
  - `MapDocument` 增加 `SetEra` 与 `tilesetDir_`（Open/CreateNew 记录，Close 清空）。
  - 回归：`--resize-test` 新增 `eraSwitched/eraTileset/reopenEra`；
    端到端验证 128×128 ERA 4 → 96×96 ERA 7（窗口标题与地形集同步）。
- [x] **地图片级工具**
  - 转换地形（0x8101）：删除框选范围内（无选区则光标所在格）的 DD2 装饰物记录，
    MTXM/THG2 与其余段保持不变；进入撤销栈。
  - 矿物外形随机（0x810D）：矿物单位（UNIT 类型 176..178）随机重排三种外形，
    serial/资源/坐标/所属玩家不变；进入撤销栈。
  - 保存地图图像（0x8069）：地形集 8 px/瓦片渲染整图，另存 24 位 BMP。
  - 回归：`--map-tools-test`（转换/撤销/重做、矿物随机、内存渲染、保存重开与段保持）。
- [x] **纠正装饰物错误（0x8103）**
  - 删除坐标越界的 DD2 记录（x ≥ 宽×32 或 y ≥ 高×32），并把 enabled 规范为 0/1。
  - 有修复才进入撤销栈；无问题时不产生历史。
  - 回归：`--map-tools-test` 新增 `fix`。
- [x] **最近编辑的地图（0x810C）与本地设置**
  - 文件 → 最近编辑的地图：光标处弹出 MRU（最多 8 条），去重置顶，可直接打开。
  - `%APPDATA%\SCMDPlus\settings.ini`（UTF-8）保存 MRU 与撤销步数；启动时载入，
    撤销步数应用到编辑历史。
  - 回归：`--recent-test`（配合 `--settings-dir=<临时目录>`）。
- [x] **多窗口（近似 MDI，多进程）**
  - 新建窗口（0x80ED）启动自身副本，每个窗口独立进程、独立地图；
    微型窗口（0x80EE）、水平/垂直排列（0x80EF/0x80F0）通过 EnumWindows 枚举本窗口类后重排；
    关闭窗口（0x80F1）关闭当前实例（走 MaybeSave）。
  - 这不是经典 MDI 子窗口模型；如需共享进程内撤销/剪贴板，后续再重构。
  - 回归：`--menu-cmd-test` 新增 `window`（排列命令在单窗口下安全执行）。

## P2 工具与设置

- [x] **基本设置持久化（155）**
  - `settings.ini` 保存 MPQ 列表、星际目录、默认水晶/气矿、撤销次数；
    对话框载入/保存/重置，MPQ 列表支持添加/删除/上下移。
  - 退出时保存视图状态：缩放、方格（开关/尺寸/颜色）、编辑层、显示选项；
    启动时恢复（无头回归不保存）。
  - 放置矿物（176..178）或气矿（188）且资源量为 0 时使用默认资源。
  - `settings.ini` 兼容带 UTF-8 BOM 的写法（避免首个键被 BOM 污染而丢失）。
  - 回归：`--recent-test` 新增 `settings` 与 `remove`（失效最近项移除）；
    `--view-test` 校验启动时缩放/编辑层/方格/显示选项的恢复。
- [x] **对话框按钮语义审计（P3）**
  - 155：默认水晶/默认气矿/撤销次数改绑标签对应的 1010/1011/1012（原实现错位到
    SP单位/Location编号）；新增“应用设定”（写入 settings.ini 但不关闭）；
    自定义刷子文件列表（浏览/添加/删除/上移/下移）持久化为 `brush0..`，
    启动时加载进 182 的自定义刷子列表；未建模的 7 个控件置灰。
  - 166：第二组“静态方格”（1049..1053）置灰（本工程仅一套网格）。
  - 172：units.dat 属性编辑控件置灰，保留只读查看。
  - 188：“所有单位”勾选后数量固定 255 并禁用输入。
  - 189：“一直显示”控制行号列显隐；行号随正文实时更新。
  - 174：下载按钮保留“未配置更新源”提示，属有意空实现。
  - 回归：`--recent-test` 增加 brushFiles 持久化；
    GUI 探针覆盖 155/166/172/188/189 与 182 启动加载（`brush0=probe [1x1]`）。
- [x] **对话框 UI 自动化回归（`--dialog-audit-test`）**
  - 逐个创建 28 个独立对话框并校验初始化出子控件（184/185/186 为 181 的内嵌子面板）。
  - 断言：155 控件映射/置灰/“应用设定”不关闭；166/172 置灰；188“所有单位”；
    189 行号与显隐；169 尺寸预设 + 地形集；175 预填 + 重设结果；182 添加刷子并应用。
  - “应用设定”写入临时设置目录 `%TEMP%\scmdplus_dialog_audit`，不污染用户配置。
  - 输出控件快照 `%TEMP%\scmdplus_dialog_snapshot.txt`：每个对话框的控件 ID/类/
    可用状态/自身可见性/文本/列表项（列表与下拉最多 20 项），共 414 个控件。
  - 报告写入 `%TEMP%\scmdplus_mpq.txt`（`createAll/snapshot/snapshotControls/.../audit=1`）。
  - 基准对比：`--dialog-snapshot-baseline=<path>` 保存 UTF-16LE 基准；
    `--dialog-snapshot-diff=<path>` 对比基准，差异写入
    `%TEMP%\scmdplus_dialog_snapshot.diff.txt`（LCS 行级 `-/+`），
    报告 `snapshotMatch/snapshotDiffLines`，不一致时退出码 2。
  - 快照前把内存设置重置为固定默认，并把 exe 目录前缀归一化为 `<exe>`；
    同一基准在四配置下均 `snapshotMatch=1`。
  - 基准入库：`doc/05-ui/dialog-snapshot-baseline.txt`（UTF-8 BOM / LF，
    617 行 / 21 KB）；`tools/dialog-ui-check.ps1` 提供 check/update 两种模式，
    `.github/workflows/dialog-ui.yml` 在 windows-latest 构建 Debug Win32 后自动校验，
    失败时上传快照 diff 作为 artifact。
  - 四配置运行均 `audit=1`（快照约 39 KB / 414 控件）。
- [x] **快照基准按环境分组**
  - 基准命名：`doc/05-ui/dialog-snapshot-baseline.<环境标签>.txt`，存在时优先于通用基准。
  - 环境标签默认 `win<主版本>.<构建号>`（如 win10.19045），可用 `-EnvTag` 覆盖；
    字体只影响像素、不影响快照文本，故不纳入标签。
  - `tools/dialog-ui-check.ps1 -Mode update -PerEnvironment` 生成/更新本环境基准；
    check 模式自动选择，未找到环境基准时回退通用基准。
  - 实测：testenv 变体生成 → 选中命中 → 改一行失败（exit=1，diff 2 行）→
    删除变体后回退通用基准通过。
- [x] **扩展按钮路径覆盖（167/173/179/198/199）**
  - 构造临时 32×32 MapDocument，模拟 167 添加/改名/删除附加文件、
    179 设置/还原开关名、198 设置/清除声音路径、199 更新简报文本；
    173 生成临时备份后校验分组列表与备份列表，结束时清理备份与文档。
  - 回归：`--dialog-audit-test` 新增 `mpq167/switch179/sound198/brief199/backup173`；
    四配置均通过。
- [x] **交互操作后的控件状态快照（167/179/198/199）**
  - 每次交互前后用 `SnapshotOf` 生成控件状态，写入
    `%TEMP%\scmdplus_dialog_interaction.txt`（before/after/LCS diff）；
    要求 8 对快照都有差异：167 添加/改名/删除、179 设置/还原、198 设置/清除、199 更新。
  - 报告新增 `interactionPairs=8 interactionSnapshot=1`。
  - 顺带修复 167 文件名框初始文本为 "0" 的问题（现清空，避免误建同名条目），
    并同步更新仓库控件基准。
  - 四配置均通过。
- [x] **交互快照纳入基准对比**
  - 交互快照复用控件快照机制：`--dialog-interaction-baseline=<path>` 保存、
    `--dialog-interaction-diff=<path>` 对比；差异写入
    `%TEMP%\scmdplus_dialog_interaction.diff.txt`，不一致时审计失败（exit 2）。
  - 仓库基准：`doc/05-ui/dialog-interaction-baseline.txt`（UTF-8 BOM / LF，约 11 KB）；
    `tools/dialog-ui-check.ps1` 一次检查两份基准，并同样支持环境变体；
    CI 失败时上传两份 diff。
  - 四配置均 `snapshotMatch=1 interactionMatch=1`；
    篡改交互基准一行可得到 `interactionDiffLines=16`、exit 2。
- [x] **181/193/194/196/197 交互路径与基准**
  - 181：新建/复制/修改/删除触发（断言 BlockCount 与列表项）；
    193/194：改字段并确定，通过新增 `LastTriggerCondition/Action` 访问器断言结果；
    196：切换玩家/控制者/队伍；197：切换升级项/玩家/使用全局默认。
  - 交互基准从 8 对扩到 20 对（约 53 KB / 1864 行），四配置 `interactionMatch=1`。
  - 修复：181 触发列表与 185/186 条件/行动列表原为 owner-draw 且无 `LBS_HASSTRINGS`，
    运行时 `LB_ADDSTRING` 文本不生效（列表显示乱码）；改为标准 ListBox 样式。
    181 列表项改用 `std::wstring` 拼接，避免 `wsprintfW` 的 `%s` 宽窄歧义。
  - 回归：`trigger181/cond193/act194/player196/restriction197`；
    交互快照连续两次运行逐字节一致。
- [x] **181 玩家触发树筛选**
  - 左侧树节点改为「全 / 1..8」；选中玩家后列表只显示 `effectivePlayers` 含该玩家的触发。
  - 列表行与触发下标分离：ListBox itemdata 保存真实触发下标，
    新建/复制/删除/上移/下移仍作用于真实触发。
  - 切换筛选时保持当前触发（不可见则选第一条）；新建触发自动带上当前筛选玩家。
  - 回归：`--dialog-audit-test` 的 `trigger181` 增加 P2=0 / P1=1 / 全=1。
- [x] **193/194 类型下拉联动**
  - 条件（193）与行动（194）对话框的「类型」下拉与「类型号」编辑框双向同步：
    选下拉回填类型号（条件 1..23、行动 1..57），改类型号切换下拉；
    下拉末项「自定义」保留手填值，类型号 0 或越界时回落到「自定义」。
  - 确定时以下拉为准（非自定义），避免手填与下拉不一致。
  - 回归：`cond193`（选索引 3→类型 4、填 22→索引 21、填 99→自定义 23、确定后 type=22）；
    `act194`（选索引 2→类型 3、填 57→索引 56、填 99→自定义 57、确定后 type=1）。
    四配置 `cond193=1 act194=1`，双基准 `snapshotMatch=1 interactionMatch=1`。
- [x] **189 编辑字符串预览框**
  - 右侧只读框（1079）随正文实时刷新：0x02 显示为换行（游戏内换行）、
    0x07+色号显示为 `[颜色N]`、其余控制字符显示为 `<0xNN>`；行号列同步把 0x02 计为一行。
  - 回归：`string189` 增加“a 0x02 b 0x07 0x04 c 0x01” → 预览 `a<换行>b[颜色4]c<0x01>`、行号 1/2；
    创建时预览随初值填充，控件快照基准中 189 的 `#1079` 由默认 "0" 变为空。
  - 四配置 `string189=1`，双基准 `snapshotMatch=1 interactionMatch=1`。
- [x] **155 基本设置剩余 7 项接线**
  - 原先置灰的 7 个原版选项全部接线并持久化到 settings.ini（语义为本工程解释）：
    启动最大值（主窗口 `SW_MAXIMIZE` 启动）、地图载入最大值（开图自动适配缩放，
    关闭则沿用保存缩放）、最多单位（默认 3400，放置超限拒绝）、SP单位（默认 1700）、
    Location编号（默认 500，描绘新地点超限拒绝）、触发使用基本名称（181 列表只显示
    「触发 N」）、单一文件（本工程保存本就是单文件，仅持久化）；上限值 0 = 不限制。
  - 「重置默认」同步重置这 7 项。
  - 回归：`settings155` 改为断言 7 控件可用、默认值、应用写回与重置恢复；
    控件快照基准中 155 的 7 行由 disabled/0 更新为 enabled/默认值。
  - 四配置 `settings155=1`，双基准 `snapshotMatch=1 interactionMatch=1`。
- [x] **166 第二套静态方格**
  - 「方格设置」的两组控件全部接线：主方格（1044..1048，虚线）与静态方格
    （1049..1053「启动方格」，实线）各自独立的 X/Y 尺寸与偏移，叠加绘制。
  - 静态网格开关与尺寸随 settings.ini（`view.staticGrid*`）持久化并随会话恢复；
    偏移与主网格一致不持久化。
  - 回归：`grid166` 改为断言 10 个控件全部可用、种子回填、确定后 `LastGridSettings`
    写回 staticEnabled/staticCellX/staticCellY/staticOffsetX；
    控件快照基准中 166 的 5 行由 disabled/0 更新为 enabled/32。
  - 四配置 `grid166=1`，双基准 `snapshotMatch=1 interactionMatch=1`。
- [x] **触发编辑器真实模态路径回归（崩溃排查）**
  - 用户报告 IDE 构建的旧产物（`Debug\SCMDPlus.exe`，166 改动前 + 增量链接）打开触发编辑器
    崩溃（0xc0000409 快速失败 → 0xc0000005）。审计 `open()` 用 `CreateDialogParamW` +
    近乎空文档，覆盖不到真实路径（`DialogBoxParamW` 模态 + 真实地图数据）。
  - 新增无头回归 `--trigger-modal-test`：满载数据文档（3 触发 × 16 条件 + 64 行动、
    地点、字符串）+ 后台线程驱动交互（新建/选择/184/185/186 面板切换/删除/关闭），
    走 `ShowRecoveredDialog(181)` 真实模态路径；结果写 `%TEMP%\scmdplus_modal181.txt`。
  - 排查工具：`tools/sym_resolve.cpp`（DbgHelp 把模块内偏移解析成符号，用于事件日志崩溃定位）。
  - 当前正式构建 Debug/Release Win32 均 `modal181=1`，审计全绿；旧产物崩溃未在现行代码复现，
    判定为过期增量构建问题——请使用 `bin\Win32\Release\SCMDPlus.exe` 运行，避免 IDE 的
    `Debug\` 旧输出目录。
- [x] **触发编辑器长地点名栈溢出崩溃（已修复）**
  - 用户提供崩溃地图 `(2)Challenger.scm`：64 个地点，某个地点名长达 **2048 字符**。
    181 初始化时 185/186 面板刷新条件/行动列表，`ConditionText` 等用固定栈缓冲
    （`wchar_t buf[256]`）+ 无边界 `wsprintfW` 拼接地点名 → 栈缓冲溢出 →
    /GS 0xc0000409 → 0xc0000005 崩溃。审计的合成文档地点名极短，从未触发。
  - 排查过程：harness 支持真实地图（`--trigger-modal-test=<地图>`）→ 阶段打点
    （追加写 `%TEMP%\scmdplus_modal181_stage.txt`）定位到 184/185/186 面板创建 →
    VEH 记录异常 RIP → 数据概况打点发现 `maxLocName=2048`。
  - 修复：`LocationName` 显示统一截断（>24 字符取前 21 + `…`，与 `StringPreview` 风格一致）。
  - 回归：崩溃地图 Debug/Release Win32 `--trigger-modal-test` 全链路（新建/选择/
    面板切换/删除/关闭）通过；四配置审计全绿，双基准一致。
  - 工具沉淀：`tools/sym_resolve.cpp`（PDB 符号解析）；复现地图存 `tools/repro/`。
- [x] **主窗口布局重排：日志移到底部，右栏放微缩地图 + 场景树**
  - 客户区分三块：上部左=地图视口；上部右=侧栏（上「微缩地图」、下「场景树」）；
    下部=日志面板。原先日志与微缩地图叠加在地图上、互相遮挡的问题消除。
  - 地图绘制、坐标换算、滚动条与滚轮缩放都限制在视口内：
    `MapViewRect` / `SideRect` / `MinimapRect` / `SceneTreeRect` / `LogRect` 统一布局；
    `TileCoordsFromClient` / `PixelCoordsFromClient` 在视口外返回无效；
    右栏/日志区滚轮不再缩放地图；`GotoStartPosition` 与小地图点击按视口尺寸居中。
  - 场景树（TreeView 子窗口）显示 地图 → 单位 / SP单位 / 装饰物 / 地点 / 触发 / 玩家
    （明细最多 200 条）；双击单位/SP单位/装饰物定位到该格，双击地点打开 171，
    双击触发打开 181；打开/新建/关闭/保存/提交编辑后自动刷新。
  - 无头回归：`--menu-cmd-test` 改为使用临时 settings 目录并重置默认视图
    （新增 `MainWindow::ReloadViewState`），避免用户 settings.ini 导致断言漂移。
  - 四配置构建通过，审计 `snapshotMatch=1 interactionMatch=1`，`--menu-cmd-test` exit=0。
- [x] **CI 四配置矩阵**
  - `.github/workflows/dialog-ui.yml` 改为矩阵：Debug/Release × Win32/x64 四个并行 job，
    各自构建后运行 `tools/dialog-ui-check.ps1`（共用仓库内两份基准）。
  - 失败时按 `dialog-snapshot-diff-<platform>-<config>` 分别上传两份 diff artifact。
  - 本地已用同一脚本验证四配置 `snapshotMatch=1 interactionMatch=1`；
    GitHub 托管 runner 尚未实际执行。
- [x] **MPQ 文件管理（167）**
  - 文件 →「MPQ 文件管理...」（运行时追加 0x8FFC）打开 167；列出当前地图除 scenario.chk
    以外的附加文件（名称与大小）。
  - 导入：本地文件写入地图（名称取文件名框，空则用本地文件名）；导出：写出选中条目；
    编辑：按文件名框改名；添加：新建空条目；删除：移除选中条目。
  - `MapDocument` 新增 `ReadExtraFile` / `SetExtraFile` / `RemoveExtraFile`（名称不区分大小写，
    拒绝 `(listfile)`/`scenario.chk`）；改动标记地图为脏，保存时随 `(listfile)` 一起写出。
  - 回归：`--extra-test` 新增 `add/replace/reject/remove/count`。
- [x] **备份查看/恢复（173）**
  - 覆盖保存前自动备份到 `%APPDATA%\SCMDPlus\backups\<地图key>\<时间戳>.scx`，
    按 155 的“最大备份数量”裁剪；`source.txt` 记录原始路径。
  - 173 列出地图组与时间戳备份，支持恢复、删除单个、删除地图组、清空全部。
  - 自动备份：155 的“启动自动保存 + 时间间隔(分钟)”开启后，主窗口每分钟检查一次，
    地图有未保存修改时按间隔生成备份（不覆盖原文件）。
  - 155 的“启动备份 / 最大备份数量”持久化。
  - 无头回归：`--backup-test`（4 次备份裁剪到 3、破坏后恢复逐字节一致、清理）。
- [x] 系统集成：设置默认地图编辑器（0x806B，HKCU 关联 .scx/.scm）、
  创建开始菜单快捷方式（0x806C，IShellLink 写入当前用户 Programs）。
- [x] **在线升级（174）与自动检查更新（0x80A8）**
  - 174 显示版本 3.0.0.0 与更新源状态；无更新源时明确提示“无法下载”，不再写占位 TODO。
  - 0x80A8 为持久化开关（`settings.ini` 的 `autoCheckUpdate`）；启动时若开启，
    向输出面板写入“未配置更新源”的提示。
  - 回归：`--recent-test` 新增 `autoUpdate`；`--menu-cmd-test` 新增 `autoUpdate` 勾选回显。
- [x] 启用声音（0x8072）：开关持久化；关闭后编辑器不再通过 MessageBeep 提示。
- [x] **Iscript 测试（192）**
  - 自动加载 `scripts\iscript.bin`（星际目录 / exe / 仓库 `mpq/StarDat.mpq`）。
  - IscriptID（1085）驱动动画下拉与操作码反汇编；GRP 文件名（1086）显示帧数/尺寸，
    并按模拟到的帧号解码统计不透明像素。
  - Play 调用 `IscriptFile::Simulate` 线性模拟 1000 步（playfram/wait/goto/end），
    显示 ops/frames/waits/loops 与终止状态。
  - 右侧静态区（1132）作为 GRP 动画预览画布：子类化绘制 + 90 ms 定时器循环解码帧，
    灰度调色板、透明像素填深色背景、等比缩放；Play 跳到模拟末帧；
    摘要/模拟信息移到 1095 多行编辑框。
  - 条件输入：Angle / Enemy position 滑杆（0..255）、Enemy in Range / Unit is Cloaked /
    Goliath Range 复选框；Play 用 `IscriptSimContext` 驱动
    trgtrangecondjmp / trgtarccondjmp / pwrupcondjmp / curdirectcondjmp 条件跳转，
    并把命中的 playfram 序列填入 Active Images 列表（单击跳帧）。
  - 回归：`--iscript-test` 增加 `sim/simOps/simFrames/simWaits/simEnded`，并新增合成
    iscript 的 `condSkip/condTake`；`--dialog=192` 验证对话框可开/可关。
- [x] 小地图渲染（右下角，显示地形缩略图、单位点与当前视口）
  - 显示选项 → 隐藏小雷达（0x80B4）控制显隐；编辑层子菜单“显示战争迷雾”（0x80CE）
    与 0x8113 合并为同一开关。
  - 回归：`--menu-cmd-test` 新增 `minimapOff/minimapOn/fogAlias/miniNav`。
  - 左键点击小地图可定位主视图。
- [x] MPQ 压缩写出：单块 zlib（0x02 + raw deflate）压缩，压缩后不划算时回退未压缩。
  - `real_edit.scx` 保存后 217 504 B → 57 808 B，本工程读回一致。
  - 回归：`--mpq-compress-test=<输出mpq>`（可压缩/不可压缩条目都逐字节读回，体积缩小）。

## P3 验证与逆向

- [x] **8 套地形集逐套渲染回归**
  - 新增 `--tileset-render-test=<输出目录>`：为 ERA 0..7 各建一张 16×16 合成地图
    （覆盖全部组与 16 个索引），8 px/瓦片渲染后与独立解码的参考图逐像素比对，
    每套导出一张 BMP，哈希互不相同。回归：`pass=8/8`。
- [x] **调试菜单（基础项）**
  - 允许调试（0x80DE）打开 HUD 调试行；显示数据 普通/精确/最终（0x80FC-FE）互斥切换，
    分别显示光标格/像素与 MTXM/选区与图像尺寸。
  - 显示重叠标记（0x80E1）用红框标出多单位同格；显示对比数据（0x80E0）在编辑事务中
    显示地形改动格数与单位数量变化；显示单位冲突大小（0x811D）画 3×3 虚线框；
    显示 CPU 测试数据（0x80F2）与显示图像大小（0x810A）显示渲染耗时与位图尺寸；
    清除屏幕（0x80E2）强制重绘。
  - 回归：`--menu-cmd-test` 新增 `debug/debugData/overlap/imageSize/compare/conflict`。
  - 清除缓存（0x80E3-E5）：本工程无增量缓存，等价于释放整图位图并重建视图；
    三条命令都写入输出面板。回归：`--menu-cmd-test` 新增 `cache`。
  - 显示图像大小（禁用模式，0x810B）：无需开启「允许调试」也显示位图尺寸。
  - 输出面板（0x80F3 / 0x805A-E / 0x8062-67 / 0x8068）：底部信息区，
    支持显示开关、8/10/12/14/16 号字体、简化/正常/高级/调试详细度与清除；
    打开/新建/保存/关闭地图会写入摘要，回归：`--menu-cmd-test` 新增 `output`。
- [x] **Iscript / Grp 调试（0x811E）**
  - 新增 `IscriptFile`（`src/units/iscript.*`）：解析 (id, headerOffset) 索引表、"SCPE" 头、
    type→动画槽数量、u8 操作码与操作数；支持无 SCPE 的别名条目与指向共享头的动画偏移。
  - 新增 205「Iscript / Grp 调试」：自动从星际目录/exe 目录/仓库 `mpq/StarDat.mpq` 读取
    `scripts\iscript.bin`，也可手动载入；按 Iscript ID 列出动画与反汇编操作码。
  - `GrpFile` 解析 GRP：8 字节帧表项（x/y/w/h + u32 帧数据偏移）、行偏移表与
    从右往左的 RLE；支持帧像素解码为 BGRA（灰度或 768/1024 B 调色板）。
  - 实测数据：342 个条目 / 34 880 B、1 627 个可用动画、12 774 条指令；示例 GRP
    `unit\terran\tank.grp` 为 51 帧、128×128，逐帧解码 51/51。
  - 回归：`--iscript-test=<iscript.bin> [--grp=<grp>]`；
    `--grp-test=<grp> [--pal=<pal>] [--grp-bmp=<bmp>]`（首次运行脚本从仓库 MPQ 抽取样本）。
- [x] **BIN 资源用途确认与默认数据接线**
  - 反汇编确认：148 = 默认 VCOD（1040 B）；183 = 默认 TRIG（3×2400 B）；
    193 = 默认 TRIG（4×2400 B）；170 = 单位/精灵名称树（u16 偏移表 + 路径字符串）。
  - 148/183/193 以 RCDATA 内嵌到 `SCMDPlus.exe`；新建地图写 VCOD，并按
    169「默认触发组」选择不写 / 3 条 / 4 条 TRIG。
  - 回归：`--file-test` 新增 `vcod=1040 trig3=7200/3 preset0=1 trig4=9600/4`。
- [x] **单位属性表（units.dat）**
  - 新增 `UnitDataTable`（`src/units/unit_data.*`）：按列式布局解析 228 项、19 876 B
    的标准 BrooDat 版本，字段含 HP/护盾/护甲/矿/气/建造时间/视野/尺寸/碰撞圈/
    头像/移动类型/对地武器/对空武器/特殊标志/BroodWar。
  - 自动加载顺序：星际目录、exe 目录、工程根的 `arr\units.dat` → 同名 MPQ 中的
    `arr\units.dat`（实测取自 `mpq/BrooDat.mpq`，单块 implode 类型 0x08）。
  - 护盾仅在该单位 `ShieldEnable` 置位时读取，Marine 显示 SH 0、Goliath 护甲 1。
  - 172「单位名字」列表改为填充全部单位属性（`#id HP SH A 矿 气 时 WxH`），
    192「Iscript 测试」按单位 ID 显示属性并把 units.dat 的 Graphic 自动填入图像 ID。
  - 回归：`--unit-data-test`（units=228 与 Marine/Goliath/Probe/Reaver/Archon 抽样）。
- [x] 文档旧设备路径与游戏目录引用已改为「可选逆向来源」，常规构建不依赖游戏
- [x] `accel.rc` 中 `.` 加速键的 RC4204 警告确认与处理
  - 原条目 `"."` 无法映射到虚拟键，RC 会忽略并报警；改为 `0xBE`（VK_OEM_PERIOD），
    保持“删除”快捷键功能。

## 建议顺序

TODO 中列出的功能项均已处理。可继续对比原版补齐的缺口：
1. 172 units.dat 字段编辑
2. 163 兼容性提示、174 在线升级（当前无更新源）
3. 帮助菜单外链（主页/在线帮助/论坛/错误与建议/捐款）
4. 多窗口 MDI 行为差异
