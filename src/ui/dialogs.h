#pragma once
#include <windows.h>

namespace scmd {

// 兼容入口：按 ID 打开从 ScmDraft2 回收的对话框。
// 实际分派在 dialog_registry（每个 ID 一个处理过程），见 src/ui/dialog_registry.h
bool ShowRecoveredDialog(int dialogId, HWND owner);

}  // namespace scmd
