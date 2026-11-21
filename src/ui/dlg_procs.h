#pragma once
#include <string>
#include <windows.h>

#include "ui/dlg_types.h"

namespace scmd {

// 25 个对话框的处理过程（与 dialog_registry.cpp 的注册表一一对应）
INT_PTR CALLBACK ProcAbout(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 关于框（103）
INT_PTR CALLBACK ProcSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 基本设置（155）
INT_PTR CALLBACK ProcCompatWarning(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 兼容性警告（163）
INT_PTR CALLBACK ProcInputCount(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 输入数量（188）
INT_PTR CALLBACK ProcOverlapUnits(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 重叠单位（191）
INT_PTR CALLBACK ProcRenameSwitch(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 重命名开关（179）
INT_PTR CALLBACK ProcStringEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 编辑字符串（189）
INT_PTR CALLBACK ProcNewMap(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 新建地图（169）
INT_PTR CALLBACK ProcResizeMap(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 重设地图大小（175）
INT_PTR CALLBACK ProcTriggerEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 触发编辑器（181）
INT_PTR CALLBACK ProcEditLocation(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 编辑 Location（171）
INT_PTR CALLBACK ProcUnitProps(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 设置单位属性（180）
INT_PTR CALLBACK ProcUnitNames(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 单位名字（172）
INT_PTR CALLBACK ProcUnitSpFlags(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // SP 单位属性（164）
INT_PTR CALLBACK ProcGridSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 方格设置（166）
INT_PTR CALLBACK ProcCustomBrush(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 自定义刷子（182）
INT_PTR CALLBACK ProcBackupViewer(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 查看备份数据（173）
INT_PTR CALLBACK ProcUpdater(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 在线升级（174）
INT_PTR CALLBACK ProcIscriptTest(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // Iscript 测试（192）
INT_PTR CALLBACK ProcMpqList(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // MPQ 文件管理（167）
INT_PTR CALLBACK ProcTabHost(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 标签容器（183）
INT_PTR CALLBACK ProcListViewHost(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 列表容器（184）
INT_PTR CALLBACK ProcListEditorA(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 列表编辑 A（185）
INT_PTR CALLBACK ProcListEditorB(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 列表编辑 B（186）
// 185/186 的实际实现（触发条件/行动列表），由 dlg_misc 的两个处理过程转发
INT_PTR HandleTriggerListEditor(HWND dlg, UINT msg, WPARAM wParam);
INT_PTR CALLBACK ProcRichText(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 富文本（187）
INT_PTR CALLBACK ProcTriggerCondition(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);  // 条件编辑（193）
INT_PTR CALLBACK ProcTriggerAction(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);     // 行动编辑（194）

// 对话框返回值缓存
int LastInputValue();                 // 188 / 191
const wchar_t* LastEditedString();    // 189
const GridSettings& LastGridSettings();   // 166
const MapSizeParams& LastMapSize();       // 169
const ResizeParams& LastResizeParams();   // 175
const UnitProps& LastUnitProps();         // 180
unsigned LastSpFlags();                   // 164

// 对话框输入初值（打开前由调用方写入）
void SeedGridSettings(const GridSettings& seed);
bool LastGridSettingsAccepted();
void SeedUnitProps(const UnitProps& seed);
void SeedEditedString(const std::wstring& text);
void SeedRichText(const std::wstring& text);
const wchar_t* LastRichText();

}  // namespace scmd
