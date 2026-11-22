#pragma once
#include <string>

#include <windows.h>

#include "map/edit_history.h"
#include "map/map_document.h"
#include "terrain/brush.h"
#include "terrain/terrain_renderer.h"
#include "ui/brush_palette.h"

namespace scmd {

enum class Tool {
    kNone = 0,
    kTerrain = 1,     // 左键刷地形
    kUnit = 2,        // 左键放单位
    kPicker = 3,      // 左键吸取瓦片
    kSelect = 4,      // 左键拖拽框选
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
    int FitZoom() const;                  // 按地图尺寸限制当前缩放
    void ApplyZoomPercent(int percent);   // 地图显示比率菜单（25%..400%）
    int pxPerTile() const { return pxPerTile_; }

    // 主菜单加速键（Ctrl+N/O/S/Z/Y 等），由消息循环调用
    bool TranslateAccel(MSG& msg) const;

    // 撤销 / 重做
    bool Undo();
    bool Redo();
    const EditHistory& history() const { return history_; }

    void OnCommand(int id);

    // 单位放置参数（也用于无头回归）
    void SetUnitPlacement(u16 type, u8 owner, u32 resource);
    bool PlaceUnitAtTile(int tx, int ty);

    // 选择与剪贴板（也用于无头回归）
    void SelectRect(int x0, int y0, int x1, int y1);
    void SelectAllTiles();
    void ClearSelection();
    void CopySelection(bool cut);
    void PasteClipboard();
    bool PasteClipboardAt(int tx, int ty);
    int OverlapSelectedUnitsAt(int tx, int ty);
    int DeleteInvalidUnits();

    // 多格图案刷（也用于无头回归）
    void SetBrushPattern(const TerrainBrush& brush);
    bool PaintBrushAt(int tx, int ty);
    const TerrainBrush& brushPattern() const { return brushPattern_; }
    const std::vector<TerrainBrush>& customBrushes() const { return customBrushes_; }

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
    void TogglePlacementRule(int commandId);
    void OpenUnitPlacement();
    void OpenPlayerRoster();
    void OpenRestrictions();
    void OpenSpUnitEditor(int selected = -1);   // 164 SP 单位属性
    int SpriteIndexAtTile(int tx, int ty) const;
    bool ApplyRadarFogCommand(int id);   // 剧本设置 → 小雷达 / 迷雾
    void SyncMenuChecks();
    void DrawGrid(HDC dc, const RECT& client) const;
    void DrawLocations(HDC dc, const RECT& client) const;
    void DrawDoodads(HDC dc, const RECT& client) const;
    void DrawSprites(HDC dc, const RECT& client) const;
    void DrawFog(HDC dc, const RECT& client) const;

    // 对话框接线
    void ResizeMapDialog();
    void EditUnitProps();
    void EditLocation(int index);
    int LocationIndexAtTile(int tx, int ty) const;
    void EditMapText(int stringIndex, bool richText);
    int UnitIndexAtTile(int tx, int ty) const;
    void GotoStartPosition();
    void SaveMapImage();

    // 地形刷选择窗口（窗口 → 刷子窗口）
    void ShowBrushPalette();
    void SetBrushTile(u16 value);
    void RefreshBrushPalette();

    // 自定义刷子（编辑层 → 复制/剪切/粘贴/刷子，182）
    void OpenCustomBrush();

    void UpdateScrollBars();
    void OnLButtonDown(int mx, int my);
    void OnLButtonDblClk(int mx, int my);
    void OnMouseMove(int mx, int my, bool leftDown);
    void OnRButtonDown(int mx, int my);
    void OnKeyDown(int vk);
    void BeginEdit();
    void CommitEdit(const wchar_t* label);
    void StampBrushAt(int tx, int ty);
    void BeginSelection(int tx, int ty);
    void UpdateSelection(int tx, int ty);
    void DrawSelection(HDC dc, const RECT& client) const;
    int SelectionCenterX() const { return (selX0_ + selX1_) / 2; }
    int SelectionCenterY() const { return (selY0_ + selY1_) / 2; }
    void TileCoordsFromClient(int mx, int my, int& tx, int& ty) const;
    void PaintTileToBuffer(int tx, int ty, u16 value);
    // 撤销/重做后只重绘发生变化的瓦片区域（大面积改动回退为整图重建）
    void RefreshAfterHistory(const Buffer& oldTiles);
    void DrawUnits(HDC dc, const RECT& client) const;
    void DrawHud(HDC dc) const;
    void DrawMinimap(HDC dc, const RECT& client) const;
    void DrawOverlapMarkers(HDC dc, const RECT& client) const;
    void DrawUnitConflictSizes(HDC dc, const RECT& client) const;

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
    ULONGLONG lastBackupTick_ = 0;   // 自动备份节流
    bool debugMode_ = false;         // 0x80DE
    int debugDataMode_ = 0;          // 0 = 普通, 1 = 精确, 2 = 最终
    bool showOverlapMarkers_ = false; // 0x80E1
    bool showCompareData_ = false;    // 0x80E0
    bool showUnitConflict_ = false;   // 0x811D
    bool showCpuTiming_ = false;     // 0x80F2
    bool showDebugImageSize_ = false; // 0x810A
    ULONGLONG lastRenderMs_ = 0;     // 上次整图重建耗时

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
    TerrainBrush brushPattern_;      // 当前刷子图案（1x1 即单格刷）
    std::vector<TerrainBrush> customBrushes_;   // 182 列表（进程内）
    int customBrushIndex_ = -1;
    u16 unitType_ = 176;       // 默认：矿物
    u8 unitOwner_ = 11;        // 0..7 玩家，11 中立
    u32 unitResource_ = 1500;
    bool placeUnitsAnywhere_ = false;      // 0x80E9
    bool placeBuildingsAnywhere_ = false;  // 0x80EA
    bool buildNormalRules_ = true;         // 0x8032
    bool allowGroundOverlap_ = false;      // 0x8015（待单位元数据）
    bool allowAirOverlap_ = false;         // 0x8109（待单位元数据）
    bool unitGridDisplay_ = false;         // 0x80B5
    bool autoConnectAddon_ = true;         // 0x80B8（待单位元数据）
    bool mineralBaseDistance_ = false;     // 0x80BF（待单位元数据）
    bool clipboardDoodads_ = false;        // 0x811C（剪贴板未实现）
    BrushPalette brushPalette_;
    EditHistory history_;
    bool editOpen_ = false;
    Buffer tilesSnapshot_;
    std::vector<UnitRecord> unitsSnapshot_;
    Buffer triggersSnapshot_;
    Buffer locationsSnapshot_;
    Buffer stringsSnapshot_;
    Buffer playersSnapshot_;
    Buffer restrictionsSnapshot_;
    Buffer doodadsSnapshot_;
    Buffer fogSnapshot_;
    Buffer spritesSnapshot_;
    Buffer wavSnapshot_;
    Buffer swnmSnapshot_;
    Buffer mbrfSnapshot_;
    bool fogPaint_ = false;         // 迷雾拖动时统一写入的目标状态
    int cursorTx_ = -1;
    int cursorTy_ = -1;

    // 框选与内部剪贴板（剪贴板可跨地图使用）
    struct ClipboardData {
        int width = 0;
        int height = 0;
        std::vector<u16> tiles;
        std::vector<UnitRecord> units;   // x/y 已归一化为选区相对像素
        std::vector<Doodad> doodads;     // 仅在“包括装饰物”开启时复制；x/y 同样归一化
        bool Empty() const { return width <= 0 || height <= 0; }
    };
    bool hasSelection_ = false;
    bool selecting_ = false;
    int selAnchorX_ = 0;
    int selAnchorY_ = 0;
    int selX0_ = 0;
    int selY0_ = 0;
    int selX1_ = 0;
    int selY1_ = 0;
    ClipboardData clipboard_;
};

}  // namespace scmd
