#pragma once
#include <windows.h>

namespace scmd {

// 把对话框居中到父窗口（父窗口为空时居中到屏幕）
void CenterDialog(HWND dlg);
// 读取控件文本为 min..max 的整数，非法返回 defValue
int ReadClampedInt(HWND dlg, int controlId, int defValue, int minValue, int maxValue);
// 写百分数（0..100）
void SetPercent(HWND dlg, int controlId, int value);

// 列表操作（ListBox）
void ListAddString(HWND dlg, int listId, const wchar_t* text);
void ListRemoveSelection(HWND dlg, int listId);
void ListMoveSelection(HWND dlg, int listId, int delta);
int ListSelectedIndex(HWND dlg, int listId);

}  // namespace scmd
