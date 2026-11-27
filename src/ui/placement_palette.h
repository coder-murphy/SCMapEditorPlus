#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

#include "common/types.h"

namespace scmd {

class MainWindow;

// 放置面板：独立浮动工具窗（原版“ScmDraft Terrain Palette”风格），
// 用 Tab 分类容纳 地形 / 单位 / 装饰物 / 地区 四类放置物。
// 选中即切换当前层与放置类型；图形使用地形集调色板与单位 GRP 缩略图。
class PlacementPalette {
public:
    // tab >= 0 时先切到指定页（0 地形 / 1 单位 / 2 装饰物 / 3 地区）
    void Show(HINSTANCE instance, HWND owner, MainWindow* win, int tab = -1);
    void Hide();
    void Refresh();                 // 地图/地形集/层数据变化后重建当前页
    void RefreshTerrain();          // 仅当前页为地形时重建（放置参数变化用）
    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void CreateChildren();
    void Layout();
    void OnTabChanged();
    void RebuildCurrentPage();
    void ShowTabControls();
    void PopulateTerrain();
    void PopulateTerrainTypes();
    void PopulateUnits();
    void PopulateDoodads();
    void PopulateLocations();
    void RebuildTerrainGrid();
    bool TerrainTypeListMode() const;   // 标准/方块模式：网格列出地形类型
    void ApplySelection(int index);
    void ClearItems();
    void DestroyImageList();
    void BeginUpdate();
    void EndUpdate();

    // 图形生成
    bool RenderTile(u16 value, std::vector<u8>& bgra32) const;
    bool RenderUnitIcon(u16 type, std::vector<u8>& bgra32) const;
    bool RenderDoodadIcon(u16 type, std::vector<u8>& bgra32) const;
    int AddIcon(const std::vector<u8>& bgra32);
    int AddPlaceholderIcon();

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HWND modeCombo_ = nullptr;   // 地形放置方式（对应原版 5 种模式）
    HWND sizeCombo_ = nullptr;   // 笔刷尺寸 01..14
    HWND typeCombo_ = nullptr;   // 子瓦片/瓦片索引模式的当前地形类型
    HWND list_ = nullptr;
    HWND modeButtons_[3] = {nullptr, nullptr, nullptr};
    MainWindow* win_ = nullptr;

    int tab_ = 0;
    int terrainSlot_ = 0;
    int updating_ = 0;              // 防止程序化填充触发选中回调
    std::vector<u16> itemValues_;   // 与列表项一一对应：瓦片值/单位类型/装饰物类型
    HIMAGELIST images_ = nullptr;
};

}  // namespace scmd
