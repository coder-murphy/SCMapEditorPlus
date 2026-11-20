#pragma once

// 菜单/工具栏命令 ID，与 res/resource.h 保持一致
namespace scmd {

enum CommandId : int {
    kFileNew = 40001,
    kFileOpen = 40002,
    kFileSave = 40003,
    kFileSaveAs = 40004,
    kFileExit = 40005,

    kEditUndo = 40101,
    kEditRedo = 40102,

    kViewTerrain = 40201,
    kViewUnits = 40202,

    kTerrainEdit = 40301,

    kUnitsList = 40401,
    kTriggersList = 40501,
    kPlayersProps = 40601,

    kToolsValidate = 40701,
    kToolsDialogs = 40702,

    kHelpAbout = 40901,
};

}  // namespace scmd
