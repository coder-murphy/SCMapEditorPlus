#pragma once
#include <string>

#include <windows.h>

#include "map/edit_history.h"
#include "map/map_document.h"
#include "terrain/terrain_renderer.h"

namespace scmd {

enum class Tool {
    kNone = 0,
    kTerrain = 1,     // 左键刷地形
    kUnit = 2,        // 左键放单位
    kPicker = 3,      // 左键吸取瓦片
};

// 编辑层（对应「编辑 → 编辑层」）
enum class EditLayer {
    kTerrain = 0,
    kDoodads = 1,
    kFog = 2,
    kLocations = 3,
    kUnits = 4,
    kSpUnits = 5,
};

// 主窗口：菜单命令分发 + 地形视图 + 基础编辑
class MainWindow {
public:
    bool Create(HINSTANCE hInstance);
    HWND hwnd() const { return hwnd_; }

    void SetTilesetDir(const std::wstring& dir) { tilesetDir_ = dir; }
    bool OpenMap(const std::wstring& path);
    // 文件菜单闭环：新建 / 关闭 / 保存 / 另存为
    void NewMap();
    void CloseMap();
    bool SaveMap(bool saveAs);
    bool MaybeSave();   // 有未保存修改时询问，返回 false 表示取消
    const MapDocument& map() const { return map_; }
    MapDocument& mutableMap() { return map_; }

    bool RebuildView(int pxPerTile);

    // 主菜单加速键（Ctrl+N/O/S/Z/Y 等），由消息循环调用
    bool TranslateAccel(MSG& msg) const;

    // 撤销 / 重做
    bool Undo();
    bool Redo();
    const EditHistory& history() const { return history_; }

    void OnCommand(int id);

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

    void UpdateTitle();
    void Present(HDC dc, const RECT& client);
    void ReleaseBackBuffer();

    // 编辑层 / 方格 / 显示选项
    void SetLayer(EditLayer layer);
    void ApplyGridPreset(int cell);
    void OpenGridSettings();
    void SetGridColor(COLORREF color, int commandId);
    void ToggleDisplay(int commandId);
    void SyncMenuChecks();
    void DrawGrid(HDC dc, const RECT& client) const;
    void DrawLocations(HDC dc, const RECT& client) const;

    void UpdateScrollBars();
    void OnLButtonDown(int mx, int my);
    void OnMouseMove(int mx, int my, bool leftDown);
    void OnRButtonDown(int mx, int my);
    void OnKeyDown(int vk);
    void BeginEdit();
    void CommitEdit(const wchar_t* label);
    void TileCoordsFromClient(int mx, int my, int& tx, int& ty) const;
    void PaintTileToBuffer(int tx, int ty, u16 value);
    void DrawUnits(HDC dc, const RECT& client) const;
    void DrawHud(HDC dc) const;

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HACCEL hAccel_ = nullptr;
    MapDocument map_;
    std::wstring tilesetDir_;
    std::wstring fileName_;
    std::wstring mapPath_;      // 完整路径（未命名地图为空）

    TerrainRenderer renderer_;
    TerrainImage image_;
    HBITMAP backBuffer_ = nullptr;
    void* dibBits_ = nullptr;
    int pxPerTile_ = 8;
    int scrollX_ = 0;
    int scrollY_ = 0;

    Tool tool_ = Tool::kTerrain;
    EditLayer layer_ = EditLayer::kTerrain;
    bool gridEnabled_ = true;      // 方格开关
    int gridCellX_ = 32;           // 方格尺寸（源像素，32 = 一格）
    int gridCellY_ = 32;
    int gridOffsetX_ = 0;
    int gridOffsetY_ = 0;
    COLORREF gridColor_ = RGB(0, 0, 0);
    int gridColorCommand_ = 0x8036;   // 颜色菜单项（用于勾选回显）
    bool showUnits_ = true;
    bool showLocations_ = false;
    bool showDoodads_ = true;
    bool showSpUnits_ = true;
    bool showAllColors_ = false;
    bool showFog_ = false;
    bool hideMinimap_ = false;
    bool allowColorCycle_ = false;
    bool showDeathFx_ = false;
    bool showZergGround_ = true;
    bool showSpecialStates_ = true;
    u16 brushTile_ = 0x0040;   // 默认：platform 甲板
    u16 unitType_ = 176;       // 默认：矿物
    EditHistory history_;
    bool editOpen_ = false;
    Buffer tilesSnapshot_;
    std::vector<UnitRecord> unitsSnapshot_;
    int cursorTx_ = -1;
    int cursorTy_ = -1;
};

}  // namespace scmd
