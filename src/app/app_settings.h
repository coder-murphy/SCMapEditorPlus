#pragma once
#include <string>
#include <utility>
#include <vector>

namespace scmd {

// 界面状态：缩放、方格、编辑层与显示选项（随 settings.ini 持久化）
struct ViewState {
    int zoomPx = 8;
    bool gridEnabled = true;
    int gridCellX = 32;
    int gridCellY = 32;
    int gridColorId = 0x8036;
    unsigned gridColorValue = 0;   // COLORREF
    int layer = 0;                 // EditLayer
    bool showUnits = true;
    bool showDoodads = true;
    bool showSpUnits = true;
    bool showAllColors = false;
    bool showZergGround = true;
    bool showSpecialStates = true;
    bool hideMinimap = false;
    bool allowColorCycle = false;
    bool showDeathFx = false;
    bool showFog = false;
    bool showLocations = true;
};

// 编辑器本地设置：最近编辑地图（MRU）与撤销步数。
// 以 UTF-8 文本保存在 %APPDATA%\SCMDPlus\settings.ini；测试可通过 SetDirectory 指定目录。
class AppSettings {
public:
    static AppSettings& Instance();

    void SetDirectory(const std::wstring& dir);   // 空 = 默认目录
    bool Load();
    bool Save();
    std::wstring filePath() const;

    const std::vector<std::wstring>& recentMaps() const { return recent_; }
    void AddRecentMap(const std::wstring& path);  // 去重、置顶、最多 kMaxRecent 条
    void RemoveRecentMap(const std::wstring& path);
    void ClearRecentMaps() { recent_.clear(); }

    int undoLimit() const { return undoLimit_; }
    void SetUndoLimit(int limit);

    // 155 基本设置
    int defaultMineral() const { return defaultMineral_; }
    void SetDefaultMineral(int value);
    int defaultGas() const { return defaultGas_; }
    void SetDefaultGas(int value);
    const std::wstring& gameDir() const { return gameDir_; }
    void SetGameDir(const std::wstring& dir) { gameDir_ = dir; }
    const std::vector<std::wstring>& mpqFiles() const { return mpqFiles_; }
    void SetMpqFiles(std::vector<std::wstring> files) { mpqFiles_ = std::move(files); }

    // 自动备份（155 的“启动备份 / 最大备份数量”）
    bool backupEnabled() const { return backupEnabled_; }
    void SetBackupEnabled(bool enabled) { backupEnabled_ = enabled; }
    int backupCount() const { return backupCount_; }
    void SetBackupCount(int count);
    bool autoBackupEnabled() const { return autoBackupEnabled_; }
    void SetAutoBackupEnabled(bool enabled) { autoBackupEnabled_ = enabled; }
    int autoBackupMinutes() const { return autoBackupMinutes_; }
    void SetAutoBackupMinutes(int minutes);

    const ViewState& view() const { return view_; }
    ViewState& mutableView() { return view_; }
    bool soundEnabled() const { return soundEnabled_; }
    void SetSoundEnabled(bool enabled) { soundEnabled_ = enabled; }
    // 高级 → 自动检查更新（当前无更新源，仅记录用户选择）
    bool autoCheckUpdate() const { return autoCheckUpdate_; }
    void SetAutoCheckUpdate(bool enabled) { autoCheckUpdate_ = enabled; }
    // 无头回归时关闭退出保存，避免污染用户设置
    void SetPersistViewOnExit(bool persist) { persistViewOnExit_ = persist; }
    bool persistViewOnExit() const { return persistViewOnExit_; }

    static constexpr std::size_t kMaxRecent = 8;

private:
    std::wstring directory() const;
    static std::wstring DefaultDirectory();

    std::wstring dir_;
    std::vector<std::wstring> recent_;
    int undoLimit_ = 50;
    int defaultMineral_ = 8;
    int defaultGas_ = 1;
    std::wstring gameDir_;
    std::vector<std::wstring> mpqFiles_;
    bool backupEnabled_ = true;
    int backupCount_ = 5;
    bool autoBackupEnabled_ = false;
    int autoBackupMinutes_ = 5;
    ViewState view_;
    bool soundEnabled_ = true;
    bool autoCheckUpdate_ = false;
    bool persistViewOnExit_ = true;
};

}  // namespace scmd
