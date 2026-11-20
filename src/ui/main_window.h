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

// 主窗口：菜单命令分发 + 地形视图 + 基础编辑
class MainWindow {
public:
    bool Create(HINSTANCE hInstance);
    HWND hwnd() const { return hwnd_; }

    void SetTilesetDir(const std::wstring& dir) { tilesetDir_ = dir; }
    bool OpenMap(const std::wstring& path);
    const MapDocument& map() const { return map_; }
    MapDocument& mutableMap() { return map_; }

    bool RebuildView(int pxPerTile);

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
    MapDocument map_;
    std::wstring tilesetDir_;
    std::wstring fileName_;

    TerrainRenderer renderer_;
    TerrainImage image_;
    HBITMAP backBuffer_ = nullptr;
    void* dibBits_ = nullptr;
    int pxPerTile_ = 8;
    int scrollX_ = 0;
    int scrollY_ = 0;

    Tool tool_ = Tool::kTerrain;
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
