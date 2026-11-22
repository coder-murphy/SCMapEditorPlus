#pragma once
#include <string>
#include <vector>

namespace scmd {

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
    void ClearRecentMaps() { recent_.clear(); }

    int undoLimit() const { return undoLimit_; }
    void SetUndoLimit(int limit);

    static constexpr std::size_t kMaxRecent = 8;

private:
    std::wstring directory() const;
    static std::wstring DefaultDirectory();

    std::wstring dir_;
    std::vector<std::wstring> recent_;
    int undoLimit_ = 50;
};

}  // namespace scmd
