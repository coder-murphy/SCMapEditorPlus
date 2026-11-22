#include "app/backups.h"

#include <algorithm>
#include <cwchar>
#include <functional>

#include <windows.h>

#include "app/app_settings.h"
#include "common/string_util.h"

namespace scmd {

namespace {

bool FileExists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring FileNameOf(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring() : path.substr(0, at);
}

std::wstring SanitizeKey(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    for (wchar_t& c : name) {
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' ||
            c == L'|' || c == L'?' || c == L'*') {
            c = L'_';
        }
    }
    if (name.empty()) name = L"map";
    const std::size_t hash = std::hash<std::wstring>{}(path);
    wchar_t suffix[16];
    ::wsprintfW(suffix, L"_%08X", static_cast<unsigned>(hash & 0xFFFFFFFFu));
    return name + suffix;
}

std::wstring Timestamp() {
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    wchar_t text[32];
    ::wsprintfW(text, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
    return text;
}

bool WriteTextFile(const std::wstring& path, const std::string& data) {
    const HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() ||
                    (::WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written,
                                 nullptr) && written == data.size());
    ::CloseHandle(f);
    return ok;
}

bool ReadTextFile(const std::wstring& path, std::string& out) {
    const HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[2048] = {};
    DWORD read = 0;
    const bool ok = ::ReadFile(f, buf, sizeof(buf) - 1, &read, nullptr) && read > 0;
    ::CloseHandle(f);
    if (ok) out.assign(buf, read);
    return ok;
}

void PruneBackups(const std::wstring& dir, int maxCount) {
    if (maxCount <= 0) return;
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = dir + L"\\*.scx";
    const HANDLE find = ::FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            files.push_back(dir + L"\\" + fd.cFileName);
        }
    } while (::FindNextFileW(find, &fd));
    ::FindClose(find);
    if (static_cast<int>(files.size()) <= maxCount) return;
    std::sort(files.begin(), files.end(), std::greater<std::wstring>());
    for (std::size_t i = static_cast<std::size_t>(maxCount); i < files.size(); i++) {
        ::DeleteFileW(files[i].c_str());
    }
}

}  // namespace

std::wstring BackupRoot() {
    const std::wstring settings = AppSettings::Instance().filePath();
    if (settings.empty()) return std::wstring();
    return DirectoryOf(settings) + L"\\backups";
}

bool MakeBackup(const std::wstring& mapPath, int maxCount) {
    if (mapPath.empty() || maxCount <= 0) return false;
    if (!FileExists(mapPath)) return false;
    const std::wstring root = BackupRoot();
    if (root.empty()) return false;
    const std::wstring key = SanitizeKey(mapPath);
    const std::wstring dir = root + L"\\" + key;
    ::CreateDirectoryW(root.c_str(), nullptr);
    ::CreateDirectoryW(dir.c_str(), nullptr);
    if (!DirectoryExists(dir)) return false;

    std::wstring stamp = Timestamp();
    std::wstring dest = dir + L"\\" + stamp + L".scx";
    for (int n = 2; FileExists(dest) && n < 100; n++) {
        dest = dir + L"\\" + stamp + L"-" + std::to_wstring(n) + L".scx";
    }
    if (!::CopyFileW(mapPath.c_str(), dest.c_str(), FALSE)) return false;
    WriteTextFile(dir + L"\\source.txt", WideToUtf8(mapPath));
    PruneBackups(dir, maxCount);
    return true;
}

std::vector<BackupGroup> ListBackupGroups() {
    std::vector<BackupGroup> groups;
    const std::wstring root = BackupRoot();
    if (root.empty() || !DirectoryExists(root)) return groups;

    WIN32_FIND_DATAW fd;
    const std::wstring pattern = root + L"\\*";
    const HANDLE find = ::FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return groups;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (::wcscmp(fd.cFileName, L".") == 0 || ::wcscmp(fd.cFileName, L"..") == 0) continue;
        const std::wstring dir = root + L"\\" + fd.cFileName;
        std::string source;
        if (!ReadTextFile(dir + L"\\source.txt", source)) continue;
        BackupGroup g;
        g.key = fd.cFileName;
        g.source = Utf8ToWide(source);
        g.name = FileNameOf(g.source);
        groups.push_back(std::move(g));
    } while (::FindNextFileW(find, &fd));
    ::FindClose(find);

    std::sort(groups.begin(), groups.end(),
              [](const BackupGroup& a, const BackupGroup& b) { return a.name < b.name; });
    return groups;
}

std::vector<BackupEntry> ListBackups(const std::wstring& key) {
    std::vector<BackupEntry> entries;
    const std::wstring root = BackupRoot();
    if (root.empty() || key.empty()) return entries;
    const std::wstring dir = root + L"\\" + key;

    WIN32_FIND_DATAW fd;
    const std::wstring pattern = dir + L"\\*.scx";
    const HANDLE find = ::FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        BackupEntry e;
        e.file = dir + L"\\" + fd.cFileName;
        e.label = fd.cFileName;
        const std::size_t dot = e.label.find_last_of(L'.');
        if (dot != std::wstring::npos) e.label = e.label.substr(0, dot);
        entries.push_back(std::move(e));
    } while (::FindNextFileW(find, &fd));
    ::FindClose(find);

    std::sort(entries.begin(), entries.end(),
              [](const BackupEntry& a, const BackupEntry& b) { return a.label > b.label; });
    return entries;
}

bool RestoreBackup(const std::wstring& file, const std::wstring& target) {
    if (file.empty() || target.empty() || !FileExists(file)) return false;
    return ::CopyFileW(file.c_str(), target.c_str(), FALSE) != FALSE;
}

void DeleteBackupFile(const std::wstring& file) {
    if (!file.empty()) ::DeleteFileW(file.c_str());
}

void DeleteBackupGroup(const std::wstring& key) {
    const std::wstring root = BackupRoot();
    if (root.empty() || key.empty()) return;
    const std::wstring dir = root + L"\\" + key;
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = dir + L"\\*";
    const HANDLE find = ::FindFirstFileW(pattern.c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            ::DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
        } while (::FindNextFileW(find, &fd));
        ::FindClose(find);
    }
    ::RemoveDirectoryW(dir.c_str());
}

void ClearAllBackups() {
    const std::wstring root = BackupRoot();
    if (root.empty()) return;
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = root + L"\\*";
    const HANDLE find = ::FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> keys;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (::wcscmp(fd.cFileName, L".") == 0 || ::wcscmp(fd.cFileName, L"..") == 0) continue;
        keys.emplace_back(fd.cFileName);
    } while (::FindNextFileW(find, &fd));
    ::FindClose(find);
    for (const std::wstring& key : keys) DeleteBackupGroup(key);
}

}  // namespace scmd
