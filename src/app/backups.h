#pragma once
#include <string>
#include <vector>

namespace scmd {

// 地图自动备份：每次保存前把旧文件复制到 settings.ini 同目录的 backups\<key>\ 下。
struct BackupEntry {
    std::wstring file;    // 备份文件完整路径
    std::wstring label;   // 显示用时间戳（yyyyMMdd-HHmmss）
};

struct BackupGroup {
    std::wstring key;     // 备份子目录名
    std::wstring name;    // 原始地图文件名
    std::wstring source;  // 原始地图完整路径
};

std::wstring BackupRoot();
bool MakeBackup(const std::wstring& mapPath, int maxCount);
std::vector<BackupGroup> ListBackupGroups();
std::vector<BackupEntry> ListBackups(const std::wstring& key);
bool RestoreBackup(const std::wstring& file, const std::wstring& target);
void DeleteBackupFile(const std::wstring& file);
void DeleteBackupGroup(const std::wstring& key);
void ClearAllBackups();

}  // namespace scmd
