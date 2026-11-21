#pragma once

// 菜单命令 ID：全部取自 [CN]ScmDraft2.exe 的 RT_MENU 109（见 res/menu.rc）
namespace scmd {

enum CommandId : int {
    kFileNew = 0x8005,
    kFileOpen = 0x801B,
    kFileClose = 0x8007,
    kFileSave = 0x8087,
    kFileSaveAs = 0x8088,
    kFileExit = 0x0069,

    kEditUndo = 0x807F,
    kEditRedo = 0x8080,

    kLayerTerrain = 0x803A,     // 编辑 → 编辑层 → 地形

    kTriggerEditor = 0x810F,    // 触发 → 编辑触发

    kHelpAbout = 0x0068,        // 帮助 → 关于...

    // 本工程自有的调试入口（原程序无此菜单项，运行时追加）
    kDebugDialogs = 0x8FFF,
};

}  // namespace scmd
