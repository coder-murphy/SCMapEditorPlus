#pragma once
#include <windows.h>

namespace scmd {

using DialogProcFn = INT_PTR(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

struct DialogEntry {
    int id;
    const wchar_t* caption;
    const char* status;      // "impl" 已实现 / "todo" 待接入
    bool childPanel;         // true = 内嵌子面板，不能独立弹出
    DialogProcFn proc;
};

const DialogEntry* FindDialog(int id);
int DialogCount();

// 按注册表打开对话框（替代原先单一的 GenericDialogProc）
bool ShowDialog(int id, HWND owner);

// 最近一次 DialogBoxParamW 失败的 Win32 错误码（0 表示无错误）
int LastDialogError();

}  // namespace scmd
