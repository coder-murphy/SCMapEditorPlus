#pragma once

// 菜单命令 ID：全部取自 [CN]ScmDraft2.exe 的 RT_MENU 109（见 res/menu.rc）
namespace scmd {

enum CommandId : int {
    kFileNew = 0x8005,
    kFileOpen = 0x801B,
    kFileRecentMaps = 0x810C,       // 文件 → 最近编辑的地图（MRU 弹窗）
    kFileClose = 0x8007,
    kFileSave = 0x8087,
    kFileSaveAs = 0x8088,
    kFileSaveImage = 0x8069,
    kFileExit = 0x0069,

    kEditUndo = 0x807F,
    kEditRedo = 0x8080,
    kEditCut = 0x80E6,
    kEditCopy = 0x80E7,
    kEditPaste = 0x80E8,
    kEditSelectAll = 0x809A,
    kEditDelete = 0x809B,
    kEditDeselect = 0x809E,
    kEditProperties = 0x809C,
    kEditSelectTool = 0x8FFB,   // 本工程新增：框选工具
    kEditOverlapUnits = 0x811A,
    kDeleteInvalidUnits = 0x8105,
    kFixDoodadErrors = 0x8103,      // 选项 → 地图加载设置 → 纠正装饰物错误
    kEditConvertTerrain = 0x8101,   // Edit → 转换地形（选中的装饰物转为纯地形）
    kRandomizeMinerals = 0x810D,    // 剧本设置 → 矿物外形随机变化

    // 剧本设置
    kScenarioResize = 0x8106,
    kScenarioDescription = 0x8041,
    kScenarioForces = 0x8042,
    kScenarioUnits = 0x8044,
    kScenarioUpgrades = 0x8043,
    kScenarioTech = 0x8045,
    kScenarioStrings = 0x8046,
    kScenarioSounds = 0x8047,
    kScenarioBriefing = 0x8110,   // 触发 → 编辑任务简报
    kScenarioGotoStart = 0x80C1,
    // 剧本设置 → 小雷达 / 迷雾（玩家 1..8 为连续 ID）
    kPlaceRadarP1 = 0x801F,       // .. P8 = 0x8026
    kPlaceRadarAll = 0x80C5,
    kRemoveRadarP1 = 0x8074,      // .. P8 = 0x807B
    kRemoveRadarAll = 0x80C6,
    kDisableFogP1 = 0x8028,       // .. P8 = 0x802F
    kDisableFogAll = 0x80F7,
    kAllowFogP1 = 0x804C,         // .. P8 = 0x8053
    kAllowFogAll = 0x80F8,

    // 窗口
    kWindowTerrain = 0x8003,
    kWindowDoodads = 0x8004,
    kWindowBrush = 0x8114,
    kWindowSpUnits = 0x80A4,    // 窗口 → SP单位窗口（164）
    kWindowUnitPanel = 0x8FFD,  // 本工程新增：单位放置参数面板
    kWindowNew = 0x80ED,        // 新建窗口（多进程实现）
    kWindowMinimize = 0x80EE,   // 微型窗口
    kWindowTileHorizontal = 0x80EF,
    kWindowTileVertical = 0x80F0,
    kWindowClose = 0x80F1,      // 关闭窗口
    kLayerBrush = 0x810E,       // 编辑层 → 复制/剪切/粘贴/刷子

    // 选项 → 单位（放置规则）
    kPlaceBuildNormal = 0x8032,
    kPlaceUnitGrid = 0x80B5,
    kPlaceGroundOverlap = 0x8015,
    kPlaceAirOverlap = 0x8109,
    kPlaceUnitAnywhere = 0x80E9,
    kPlaceBuildingAnywhere = 0x80EA,
    kPlaceAutoAddon = 0x80B8,
    kPlaceMineralDistance = 0x80BF,
    kDoodadAnywhere = 0x8010,       // 选项 → 装饰物 → 允许任意放置
    kClipboardDoodads = 0x811C,

    // 选项 / 高级 / 帮助
    kOptionsEditorSettings = 0x80C4,
    kAdvancedCheckUpdate = 0x8071,
    kAutoCheckUpdate = 0x80A8,       // 高级 → 自动检查更新（无更新源，仅记录开关）
    kAdvancedBackup = 0x80F9,
    kAdvancedDefaultEditor = 0x806B, // 设置为默认地图编辑器
    kAdvancedShortcut = 0x806C,      // 创建开始菜单快捷方式
    kEnableSound = 0x8072,           // 选项 → 启用声音

    kLayerTerrain = 0x803A,     // 编辑 → 编辑层 → 地形
    kLayerDoodads = 0x803B,
    kLayerFog = 0x803E,
    kFogDefaultUnexplored = 0x80D3,   // 战争迷雾：默认为未探索
    kFogDefaultExplored = 0x80D4,     // 战争迷雾：默认为已探索
    kLayerLocations = 0x803C,
    kLocRename = 0x8108,        // 选项 → Locations → 修改名字（打开 171）
    kLocDrawNeat = 0x8013,      // 选项 → Locations → 整齐的描绘
    kLocDrawGrid = 0x80EB,      // 选项 → Locations → 按照方格描绘
    kLocDrawAnywhere = 0x80EC,  // 选项 → Locations → 任意描绘
    kLayerUnits = 0x803D,
    kLayerSpUnits = 0x803F,
    kSpAlignGrid = 0x80B7,      // 选项 → SP单位 → 对齐方格

    // 选项 → 方格
    kGridTiny = 0x80AF,
    kGridSmall = 0x800E,
    kGridNormal = 0x800D,
    kGridLarge = 0x80A5,
    kGridHuge = 0x80AA,
    kGridCustom = 0x80CF,
    kGridOff = 0x80D6,
    // 选项 → 地图显示比率（25%..400%，32 px/瓦片为 100%）
    kZoom25 = 0x80DA,
    kZoom33 = 0x80DB,
    kZoom50 = 0x80D9,
    kZoom66 = 0x80DD,
    kZoom100 = 0x80D8,
    kZoom150 = 0x8116,
    kZoom200 = 0x8117,
    kZoom300 = 0x8118,
    kZoom400 = 0x8119,

    // 选项 → 方格 → 颜色
    kGridColorBlack = 0x8036,
    kGridColorGray = 0x80B1,
    kGridColorWhite = 0x8037,
    kGridColorGreen = 0x8038,
    kGridColorRed = 0x8039,
    kGridColorBlue = 0x80B2,
    kGridColorOther = 0x80D0,

    // 显示选项
    kDispAllColors = 0x8054,
    kDispUnits = 0x8018,
    kDispDoodads = 0x8019,
    kDispSpUnits = 0x801A,
    kDispZergGround = 0x8034,
    kDispSpecialStates = 0x80B3,
    kDispHideMinimap = 0x80B4,
    kDispColorCycle = 0x80D1,
    kDispDeathFx = 0x80F6,
    kDispFog = 0x8113,
    kDispFogExplored = 0x80F4,  // 选项 → 战争迷雾 → 显示战争迷雾在探索地区（与 0x8113 同义）
    kLayerFogToggle = 0x80CE,   // 编辑层子菜单内的“显示战争迷雾”（与 0x8113 同义）
    kDispLocations = 0x8115,

    kTriggerEditor = 0x810F,    // 触发 → 编辑触发

    // 调试菜单
    kDebugEnable = 0x80DE,      // 允许调试
    kDebugDataNormal = 0x80FC,  // 显示数据(普通模式)
    kDebugDataExact = 0x80FD,   // 显示数据(精确模式)
    kDebugDataFinal = 0x80FE,   // 显示数据(最终模式)
    kDebugOverlaps = 0x80E1,    // 显示重叠标记
    kDebugCompare = 0x80E0,     // 显示对比数据
    kDebugUnitConflict = 0x811D,// 显示单位冲突大小
    kIscriptGrpDebug = 0x811E,  // Iscript / Grp 调试（205）
    kDebugCpu = 0x80F2,         // 显示CPU测试数据
    kDebugClearScreen = 0x80E2, // 清除屏幕
    kDebugImageSize = 0x810A,   // 显示图像大小
    kDebugImageSizeAlways = 0x810B, // 显示图像大小(禁用模式：不依赖允许调试)
    kDebugClearDoodads = 0x80E3,    // 清除装饰物缓存
    kDebugClearUnits = 0x80E4,      // 清除单位缓存
    kDebugClearDuplicates = 0x80E5, // 清除重复缓存

    // 选项 → 输出
    kOutputShow = 0x80F3,       // 显示输出面板
    kOutputFont8 = 0x805A,      // .. 16 = 0x805E
    kOutputFont10 = 0x805B,
    kOutputFont12 = 0x805C,
    kOutputFont14 = 0x805D,
    kOutputFont16 = 0x805E,
    kOutputVerbOff = 0x8067,    // 关闭显示
    kOutputVerbSimple = 0x8065, // 简化模式
    kOutputVerbNormal = 0x8062, // 正常模式
    kOutputVerbHigh = 0x8063,   // 高级模式
    kOutputVerbDebug = 0x8064,  // 调试模式
    kOutputClear = 0x8068,      // 清除当前信息

    kHelpAbout = 0x0068,        // 帮助 → 关于...

    // 本工程自有的调试入口（原程序无此菜单项，运行时追加）
    kMpqFileManager = 0x8FFC,   // 文件 → MPQ 文件管理（167）
    kDebugDialogs = 0x8FFF,
    kAutoEdgeCliffs = 0x8FFE,   // 调试 → 自动铺设崖壁（向虚空外侧压印 1..3 圈）
};

}  // namespace scmd
