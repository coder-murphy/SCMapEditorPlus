#pragma once

#include <vector>

#include <windows.h>

#include "common/types.h"

namespace scmd {

class Tileset;

// 地形刷调色板：列出当前地形集里可绘制的瓦片，点击选中后通知主窗口。
// 独立工具窗口（不属于主窗口客户区），可在「窗口 → 刷子窗口」打开。
class BrushPalette {
public:
    // 选中瓦片时发给 owner 的消息：wParam = MTXM 值
    static constexpr UINT kMsgSelected = WM_APP + 0x41;

    struct Entry {
        u16 group = 0;
        u16 index = 0;
        u16 value = 0;   // (group << 4) | index
        u16 mega = 0;    // CV5 指向的大瓦片
    };

    // 过滤条件：walkable -1 全部 / 0 阻挡 / 1 可走；type -1 全部
    struct Filter {
        int walkable = -1;
        int type = -1;
        int groupMin = 0;
        int groupMax = 0x7FF;
    };

    // 枚举可绘制瓦片；同一组内指向同一大瓦片的索引只保留第一个。
    static std::vector<Entry> BuildEntries(const Tileset& ts, const Filter& filter = Filter());

    bool Create(HINSTANCE instance, HWND owner);
    void Show(HINSTANCE instance, HWND owner, const Tileset* ts, u16 selected);
    // 地图/地形集变化后重建列表；ts 为空时清空
    void Refresh(const Tileset* ts, u16 selected);
    void SetSelected(u16 value);
    void Close();

    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }
    const std::vector<Entry>& entries() const { return entries_; }
    u16 selected() const { return selected_; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC dc, const RECT& client);
    void DrawTile(HDC dc, int x, int y, u16 value);
    void CreateFilterControls();
    void RebuildFilterControls();
    void ApplyFilter();
    void SelectIndex(int index, bool notify);
    void EnsureVisible(int index);
    int HitTest(int x, int y) const;
    void UpdateScroll();
    void UpdateStatus();
    void ResolveSelection();

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    const Tileset* ts_ = nullptr;
    std::vector<Entry> entries_;
    Filter filter_;
    HWND filterWalk_ = nullptr;
    HWND filterType_ = nullptr;
    HWND filterGroup_ = nullptr;
    u16 selected_ = 0;
    int selectedIndex_ = -1;
    int scrollY_ = 0;
    HBITMAP tileBitmap_ = nullptr;
    void* tileBits_ = nullptr;
    BITMAPINFO tileInfo_ = {};
    wchar_t status_[192] = L"";
};

}  // namespace scmd
