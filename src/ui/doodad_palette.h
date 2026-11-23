#pragma once

#include <utility>
#include <vector>

#include <windows.h>

#include "common/types.h"

namespace scmd {

class DoodadTable;

// 装饰物窗口（窗口 → 装饰物窗口，0x8004）：
// 选择 DD2 类型 / 玩家 / 禁用标志，并切换「允许任意放置」（0x8010）。
// 同时列出当前地图中已使用的类型，便于复用。
class DoodadPalette {
public:
    // 参数变化（类型/玩家/禁用）发给 owner
    static constexpr UINT kMsgParamsChanged = WM_APP + 0x42;
    // 「允许任意放置」勾选变化，wParam = 1/0
    static constexpr UINT kMsgAllowAnywhere = WM_APP + 0x43;

    bool Create(HINSTANCE instance, HWND ownerWnd);
    bool Show(HINSTANCE instance, HWND ownerWnd, const DoodadTable* table, u16 type,
              u8 ownerId, bool disabled, bool allowAnywhere);
    void Refresh(const DoodadTable* table, u16 type, u8 ownerId, bool disabled);
    void SetAllowAnywhere(bool allow);
    void Close();

    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }
    unsigned lastError() const { return lastError_; }
    u16 type() const { return type_; }
    u8 owner() const { return owner_; }
    bool disabled() const { return disabled_; }

private:
    enum : int {
        kTypeEdit = 2001,
        kOwnerCombo = 2002,
        kDisabledCheck = 2003,
        kAnywhereCheck = 2004,
        kTypeList = 2005,
        kStatusText = 2006,
    };

    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void RebuildTypeList();
    void SyncControls();
    void NotifyParams();
    void SelectTypeFromList();

    HINSTANCE instance_ = nullptr;
    HWND ownerWnd_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND typeEdit_ = nullptr;
    HWND ownerCombo_ = nullptr;
    HWND disabledCheck_ = nullptr;
    HWND anywhereCheck_ = nullptr;
    HWND typeList_ = nullptr;
    HWND statusText_ = nullptr;

    const DoodadTable* table_ = nullptr;
    std::vector<std::pair<u16, int>> types_;   // 类型 → 记录数（升序）
    u16 type_ = 0;
    u8 owner_ = 0;
    bool disabled_ = false;
    bool allowAnywhere_ = false;
    bool updating_ = false;    // 程序性设置控件时抑制回发
    unsigned lastError_ = 0;
};

}  // namespace scmd
