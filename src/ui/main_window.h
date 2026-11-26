#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <windows.h>
#include <commctrl.h>

#include "map/edit_history.h"
#include "map/map_document.h"
#include "terrain/brush.h"
#include "terrain/terrain_renderer.h"
#include "units/object_graphics.h"
#include "ui/brush_palette.h"
#include "ui/command_table.h"
#include "ui/doodad_palette.h"

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

// 地形放置方式（对应 ScmDraft2 的地形层模式）
enum class TerrainPlacementMode {
    kIsometric = 0,   // 等距地形：按相邻地形类型选子瓦片，自动混合
    kRectangular = 1, // 矩形地形：按组整格填充，不参与混合
    kSubtile = 2,     // 子瓦片：只改同组瓦片的子索引
    kIndexed = 3,     // 瓦片索引：放置精确的 group+index
};

// Locations 描绘模式（选项 → Locations）
enum class LocationDrawMode {
    kNeat = 0,      // 整齐的描绘：对齐 32 px 瓦片边界
    kGrid = 1,      // 按照方格描绘：对齐当前方格设置
    kAnywhere = 2,  // 任意描绘：像素精度
};

// 163：应用可自动修复的兼容性问题（越界单位/装饰物/SP、非法地点矩形）。
// 返回修复项数；有改动时把地图标记为脏。供主窗口与无头回归共用。
int RepairCompatibilityIssues(MapDocument& map);

// 主窗口：菜单命令分发 + 地形视图 + 基础编辑
class MainWindow {
public:
    ~MainWindow();
    bool Create(HINSTANCE hInstance);
    HWND hwnd() const { return hwnd_; }

    void SetTilesetDir(const std::wstring& dir) { tilesetDir_ = dir; }
    const std::wstring& tilesetDir() const { return tilesetDir_; }
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
    int LoadZoom() const;                 // 开图缩放：适配（1129）或沿用保存值
    void ApplyZoomPercent(int percent);   // 地图显示比率菜单（25%..400%）
    int pxPerTile() const { return pxPerTile_; }
    bool ClickMinimapAt(int mx, int my);  // 小地图点击定位（返回是否命中）
    RECT minimapRect() const { return MinimapRect(); }   // 回归定位用
    int scrollX() const { return scrollX_; }
    int scrollY() const { return scrollY_; }

    // 主菜单加速键（Ctrl+N/O/S/Z/Y 等），由消息循环调用
    bool TranslateAccel(MSG& msg) const;
    // 同进程多窗口共用一条消息循环：按目标窗口分派加速键
    static bool TranslateAccelForMessage(MSG& msg);
    // 当前进程内仍存活的主窗口数量（回归断言用）
    static int LiveWindowCount();
    // NewWindow 动态创建的窗口在 WM_NCDESTROY 时自删除
    void SetSelfOwned(bool owned) { selfOwned_ = owned; }
    // 从 AppSettings 重新应用视图状态（无头回归重置用）
    void ReloadViewState();
    // 无头回归/工具模式：抑制打开地图后自动弹出的提示框（163）
    void SetSuppressDialogs(bool suppress) { suppressDialogs_ = suppress; }

    // 撤销 / 重做
    bool Undo();
    bool Redo();
    const EditHistory& history() const { return history_; }

    void OnCommand(int id);
    void RegisterCommands();        // 创建窗口后一次性建立命令表
    void RegisterCommandStates();   // 可勾选命令的状态查询

    // 单位放置参数（也用于无头回归）
    void SetUnitPlacement(u16 type, u8 owner, u32 resource);
    bool PlaceUnitAtTile(int tx, int ty);

    // 装饰物放置参数（也用于无头回归）
    void SetDoodadPlacement(u16 type, u8 owner, bool disabled);
    void SetDoodadAnywhere(bool allow);
    bool PlaceDoodadAtTile(int tx, int ty);
    int RemoveDoodadsAtTile(int tx, int ty);
    bool doodadPaletteOpen() const { return doodadPalette_.IsOpen(); }

    // 地点描绘（也用于无头回归）：像素坐标，right/bottom 为开区间
    void SetLocationDrawMode(int mode);
    int AddLocationFromDrag(int px0, int py0, int px1, int py1);

    // 地形放置方式与刷子大小（ScmDraft2 风格）
    void SetTerrainPlacementMode(TerrainPlacementMode mode);
    void ResizeTerrainBrush(int delta);
    TerrainPlacementMode terrainPlacementMode() const { return terrainMode_; }
    int terrainBrushWidth() const { return brushW_; }
    int terrainBrushHeight() const { return brushH_; }
    // 当前地形类型（ScmDraft2 地形类型表的槽位 0..12，用于等距地形）
    void SetTerrainTypeSlot(int slot);
    int terrainTypeSlot() const { return terrainTypeSlot_; }

    // SP 单位放置（也用于无头回归）
    void SetSpPlacement(u16 type, u8 owner, u16 flags);
    void SetSpAlignGrid(bool align);
    bool PlaceSpriteAtPixels(int px, int py);
    int RemoveSpriteAtTile(int tx, int ty);

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
    // 155 自定义刷子文件：启动时按配置加载，去重后追加到 182 列表
    void LoadBrushFiles(const std::vector<std::wstring>& paths);

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
    bool ScrollToMinimapPoint(int mx, int my);   // 小地图拖拽：按光标位置滚动
    void ScrollByPixels(int dx, int dy);         // 方向键滚动
    void DrawGrid(HDC dc, const RECT& client) const;
    void DrawLocations(HDC dc, const RECT& client) const;
    void DrawLocationPreview(HDC dc, const RECT& client) const;
    void DrawDoodads(HDC dc, const RECT& client) const;
    void DrawSprites(HDC dc, const RECT& client) const;
    void DrawFog(HDC dc, const RECT& client) const;

    // 对象 GRP：按 images.tbl 相对路径取（并缓存）绘制位图
    struct SpriteBitmap {
        HBITMAP bmp = nullptr;
        int width = 0;
        int height = 0;
    };
    const ObjectGraphics& ObjectGraphicsFor() const;
    const SpriteBitmap* SpriteBitmapFor(const std::wstring& relPath, int owner,
                                        int frame = 0, bool shadow = false) const;
    // 在源像素坐标 (mapX,mapY) 处居中绘制；frame 为 GRP 帧（默认 0）
    // shadow=true 时按原版规则画成 50% 黑色投影
    bool BlitObject(HDC dc, const std::wstring& relPath, int mapX, int mapY, int owner,
                    int frame = 0, bool shadow = false) const;
    const Buffer& TeamPaletteFor(int owner) const;
    void ReleaseSpriteCache();

    // 对话框接线
    void ResizeMapDialog();
    void EditUnitProps();
    void EditLocation(int index);
    int LocationIndexAtTile(int tx, int ty) const;
    void EditMapText(int stringIndex, bool richText);
    int UnitIndexAtTile(int tx, int ty) const;
    void GotoStartPosition();
    void SaveMapImage();
    void AutoPlaceCliffs();
    void NewWindow();
    void MinimizeWindows();
    void ArrangeWindows(bool horizontal);
    void SetDefaultMapEditor();
    void CreateStartMenuShortcut();

    // 地形刷选择窗口（窗口 → 刷子窗口）
    void ShowBrushPalette();
    void SetBrushTile(u16 value);
    void RefreshBrushPalette();
    void RebuildTerrainTypeCombo();
    void StampTerrainAt(int tx, int ty);
    u16 TerrainValueFor(int tx, int ty) const;

    // 装饰物窗口（窗口 → 装饰物窗口）
    void ShowDoodadPalette();
    void RefreshDoodadPalette();

    // 自定义刷子（编辑层 → 复制/剪切/粘贴/刷子，182）
    void OpenCustomBrush();

    void UpdateScrollBars();
    // 布局：左侧=微缩地图+场景树，右侧上部=地图视口、下部=日志，底部=状态栏
    RECT MapViewRect() const;
    RECT SideRect() const;
    RECT MinimapRect() const;
    RECT StartupBannerRect() const;   // 未打开地图时的左侧启动图区域
    RECT LogRect() const;
    RECT SceneTreeRect() const;
    RECT StatusRect() const;
    RECT HScrollRect() const;
    RECT VScrollRect() const;
    RECT LogVScrollRect() const;
    int LogTop() const;
    void UpdateLogScrollBar();
    void ScrollLogLines(int delta);
    RECT ToolbarRect() const;
    void CreateChrome();     // 顶部工具栏 + 地图滚动条 + 底部状态栏
    void LayoutChrome();
    void UpdateStatusBar();
    void CreateToolbar();
    void SyncToolbarControls();
    bool OnToolbarCommand(int id, int code);
    int LogHeight() const;
    bool PointInMapView(int mx, int my) const;
    void CreateSceneTree();
    void LayoutSceneTree();
    void RefreshSceneTree();
    void DrawSidebar(HDC dc) const;
    void OnSceneTreeDblClk();
    void CheckCompatibility();   // 163：开图后检测并提示（有问题才弹）
    void OpenHelpUrl(const wchar_t* url);   // 帮助菜单外链（默认浏览器）
    void OnLButtonDown(int mx, int my);
    void OnLButtonDblClk(int mx, int my);
    void OnMouseMove(int mx, int my, bool leftDown);
    void OnRButtonDown(int mx, int my);
    void OnKeyDown(int vk);
    void BeginEdit();
    void BeepWarning() const;        // 受“启用声音”控制
    void CommitEdit(const wchar_t* label);
    void StampBrushAt(int tx, int ty);
    void BeginSelection(int tx, int ty);
    void UpdateSelection(int tx, int ty);
    void DrawSelection(HDC dc, const RECT& client) const;
    int SelectionCenterX() const { return (selX0_ + selX1_) / 2; }
    int SelectionCenterY() const { return (selY0_ + selY1_) / 2; }
    void TileCoordsFromClient(int mx, int my, int& tx, int& ty) const;
    void PixelCoordsFromClient(int mx, int my, int& px, int& py) const;
    void BeginLocationDraw(int mx, int my);
    void UpdateLocationDraw(int mx, int my);
    void FinishLocationDraw();
    void PaintTileToBuffer(int tx, int ty, u16 value);
    // 撤销/重做后只重绘发生变化的瓦片区域（大面积改动回退为整图重建）
    void RefreshAfterHistory(const Buffer& oldTiles);
    void DrawUnits(HDC dc, const RECT& client) const;
    // 显示选项 → Zerg地皮：在虫族建筑下铺 4×4 creep 图案
    void DrawCreep(HDC dc, const RECT& client) const;
    void DrawDeathMarks(HDC dc, const RECT& client) const;
    void DrawOutputPanel(HDC dc, const RECT& client) const;
    int OutputPanelHeight() const;
    bool PointInOutputPanel(int mx, int my) const;
    void Output(int severity, const std::wstring& text);
    int RemoveUnitsAtTileWithFx(int tx, int ty);
    void DrawHud(HDC dc) const;
    void DrawMinimap(HDC dc) const;
    void DrawOverlapMarkers(HDC dc, const RECT& client) const;
    void DrawUnitConflictSizes(HDC dc, const RECT& client) const;

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND sceneTree_ = nullptr;
    HWND toolbar_ = nullptr;
    HIMAGELIST toolbarImages_ = nullptr;
    HBITMAP menuIconBitmap_ = nullptr;
    HBITMAP startupBanner_ = nullptr;
    mutable ObjectGraphics objectGraphics_;
    mutable bool objectGraphicsTried_ = false;
    mutable std::unordered_map<std::wstring, SpriteBitmap> spriteCache_;
    mutable std::unordered_set<std::wstring> spriteMissing_;
    mutable std::unordered_map<int, Buffer> teamPalettes_;   // 按玩家颜色生成的对象调色板
    HWND layerCombo_ = nullptr;
    HWND playerCombo_ = nullptr;
    HWND zoomCombo_ = nullptr;
    int zoomTempItem_ = -1;   // 滚轮缩放档位不在固定列表时临时追加的项
    HWND terrainCombo_ = nullptr;
    HWND indexCombo_ = nullptr;
    HWND mirrorButtons_[3] = {nullptr, nullptr, nullptr};
    HWND hScroll_ = nullptr;
    HWND vScroll_ = nullptr;
    HWND logVScroll_ = nullptr;
    HWND statusBar_ = nullptr;   // 右侧栏下方场景树（TreeView 子窗口）
    bool suppressDialogs_ = false;   // 无头运行：不弹开图兼容性提示
    HACCEL hAccel_ = nullptr;
    CommandTable commands_;
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
    bool soundEnabled_ = true;       // 0x8072
    int debugDataMode_ = 0;          // 0 = 普通, 1 = 精确, 2 = 最终
    bool showOverlapMarkers_ = false; // 0x80E1
    bool showCompareData_ = false;    // 0x80E0
    bool showUnitConflict_ = false;   // 0x811D
    bool showCpuTiming_ = false;     // 0x80F2
    bool showDebugImageSize_ = false;
    bool showImageSizeAlways_ = false;   // 0x810B：无需开启调试也显示图像尺寸 // 0x810A
    ULONGLONG lastRenderMs_ = 0;     // 上次整图重建耗时

    Tool tool_ = Tool::kTerrain;
    EditLayer layer_ = EditLayer::kTerrain;
    bool gridEnabled_ = true;      // 方格开关
    int gridCellX_ = 32;           // 方格尺寸（源像素，32 = 一格）
    int gridCellY_ = 32;
    int gridOffsetX_ = 0;
    int gridOffsetY_ = 0;
    bool staticGridEnabled_ = false;   // 第二套“静态方格”（166-1053）
    int staticGridCellX_ = 32;
    int staticGridCellY_ = 32;
    int staticGridOffsetX_ = 0;
    int staticGridOffsetY_ = 0;
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
    struct DeathMark {
        int tx = 0;
        int ty = 0;
        ULONGLONG tick = 0;
    };
    std::vector<DeathMark> deathMarks_;   // 0x80F6：删除单位后的短暂 X 标记
    int animFrame_ = 0;                   // 场景动效时钟（120ms/拍，气矿烟雾等使用）

    // 选项 → 输出面板
    struct OutputLine {
        std::wstring text;
        int severity = 2;   // 0 错误 / 1 警告 / 2 普通 / 3 详细 / 4 调试
    };
    std::vector<OutputLine> outputLines_;
    bool outputVisible_ = true;
    int outputFontPt_ = 12;
    int logTopLine_ = 0;        // 日志首行索引
    bool logFollow_ = true;     // 新输出时自动滚到末尾
    int outputVerbosity_ = 2;   // 0 关闭 / 1 简化 / 2 正常 / 3 高级 / 4 调试
    u16 brushTile_ = 0x0040;   // 默认：platform 甲板
    TerrainBrush brushPattern_;      // 当前刷子图案（1x1 即单格刷）
    std::vector<TerrainBrush> customBrushes_;   // 182 列表（进程内）
    TerrainPlacementMode terrainMode_ = TerrainPlacementMode::kIsometric;
    int brushW_ = 1;   // 地形刷宽（格），+/- 调整
    int brushH_ = 1;   // 地形刷高（格）
    int terrainTypeSlot_ = 0;   // ScmDraft2 地形类型表槽位（等距模式使用）
    int customBrushIndex_ = -1;
    u16 unitType_ = 176;       // 默认：矿物
    u8 unitOwner_ = 11;        // 0..7 玩家，11 中立
    u32 unitResource_ = 1500;
    bool placeUnitsAnywhere_ = false;      // 0x80E9
    bool placeBuildingsAnywhere_ = false;  // 0x80EA
    bool buildNormalRules_ = true;         // 0x8032
    bool allowGroundOverlap_ = false;      // 0x8015
    bool allowAirOverlap_ = false;         // 0x8109
    bool unitGridDisplay_ = false;         // 0x80B5
    bool autoConnectAddon_ = true;         // 0x80B8
    bool mineralBaseDistance_ = false;     // 0x80BF
    bool clipboardDoodads_ = false;        // 0x811C
    bool placeDoodadsAnywhere_ = false;    // 0x8010
    u16 doodadType_ = 0;
    u8 doodadOwner_ = 0;
    bool doodadDisabled_ = false;

    // Locations 描绘（0x8013/0x80EB/0x80EC）
    LocationDrawMode locationDrawMode_ = LocationDrawMode::kNeat;
    bool locationDrawing_ = false;
    int locDrawX0_ = 0;
    int locDrawY0_ = 0;
    int locDrawX1_ = 0;
    int locDrawY1_ = 0;

    // SP 单位（THG2）放置参数与 0x80B7
    bool spAlignGrid_ = false;
    u16 spPlaceType_ = 0;
    u8 spPlaceOwner_ = 0;
    u16 spPlaceFlags_ = 0;
    BrushPalette brushPalette_;
    DoodadPalette doodadPalette_;
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
    // 进程级共享剪贴板（跨窗口/跨地图粘贴）
    static ClipboardData& SharedClipboard();
    bool hasSelection_ = false;
    bool selecting_ = false;
    bool minimapDragging_ = false;   // 小地图按住拖动中
    int selAnchorX_ = 0;
    int selAnchorY_ = 0;
    int selX0_ = 0;
    int selY0_ = 0;
    int selX1_ = 0;
    int selY1_ = 0;
    bool selfOwned_ = false;
};

}  // namespace scmd
