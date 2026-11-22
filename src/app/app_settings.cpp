#include "app/app_settings.h"

#include <cwchar>
#include <cstdlib>
#include <windows.h>

#include "common/string_util.h"

namespace scmd {

namespace {

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    const HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(f, &size) || size.QuadPart < 0 || size.QuadPart > (1 << 20)) {
        ::CloseHandle(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool ok = out.empty() ||
                    (::ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) &&
                     read == out.size());
    ::CloseHandle(f);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const std::string& data) {
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

std::wstring TrimW(const std::wstring& in) {
    std::size_t b = 0;
    std::size_t e = in.size();
    while (b < e && (in[b] == L' ' || in[b] == L'\t' || in[b] == L'\r')) b++;
    while (e > b && (in[e - 1] == L' ' || in[e - 1] == L'\t' || in[e - 1] == L'\r')) e--;
    return in.substr(b, e - b);
}

}  // namespace

AppSettings& AppSettings::Instance() {
    static AppSettings instance;
    return instance;
}

std::wstring AppSettings::DefaultDirectory() {
    wchar_t appData[MAX_PATH] = L"";
    const DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    return std::wstring(appData) + L"\\SCMDPlus";
}

void AppSettings::SetDirectory(const std::wstring& dir) { dir_ = dir; }

std::wstring AppSettings::directory() const {
    return dir_.empty() ? DefaultDirectory() : dir_;
}

std::wstring AppSettings::filePath() const {
    const std::wstring dir = directory();
    return dir.empty() ? std::wstring() : dir + L"\\settings.ini";
}

void AppSettings::SetUndoLimit(int limit) {
    if (limit < 1) limit = 1;
    if (limit > 500) limit = 500;
    undoLimit_ = limit;
}

void AppSettings::SetDefaultMineral(int value) {
    if (value < 0) value = 0;
    if (value > 100000) value = 100000;
    defaultMineral_ = value;
}

void AppSettings::SetDefaultGas(int value) {
    if (value < 0) value = 0;
    if (value > 100000) value = 100000;
    defaultGas_ = value;
}

void AppSettings::SetBackupCount(int count) {
    if (count < 1) count = 1;
    if (count > 99) count = 99;
    backupCount_ = count;
}

void AppSettings::SetAutoBackupMinutes(int minutes) {
    if (minutes < 1) minutes = 1;
    if (minutes > 120) minutes = 120;
    autoBackupMinutes_ = minutes;
}

void AppSettings::AddRecentMap(const std::wstring& path) {
    if (path.empty()) return;
    for (auto it = recent_.begin(); it != recent_.end(); ++it) {
        if (::_wcsicmp(it->c_str(), path.c_str()) == 0) {
            recent_.erase(it);
            break;
        }
    }
    recent_.insert(recent_.begin(), path);
    if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
}

bool AppSettings::Load() {
    recent_.clear();
    undoLimit_ = 50;
    defaultMineral_ = 8;
    defaultGas_ = 1;
    gameDir_.clear();
    mpqFiles_.clear();
    backupEnabled_ = true;
    backupCount_ = 5;
    autoBackupEnabled_ = false;
    autoBackupMinutes_ = 5;
    const std::wstring path = filePath();
    if (path.empty()) return false;
    std::string text;
    if (!ReadWholeFile(path, text)) return false;

    std::size_t pos = 0;
    std::vector<std::wstring> slots(kMaxRecent);
    std::vector<std::wstring> mpqSlots;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::wstring value = TrimW(Utf8ToWide(line.substr(eq + 1)));
        if (key.compare(0, 6, "recent") == 0 && key.size() > 6 &&
            key[6] >= '0' && key[6] <= '9') {
            const int idx = std::atoi(key.c_str() + 6);
            if (idx >= 0 && idx < static_cast<int>(kMaxRecent) && !value.empty()) {
                slots[static_cast<std::size_t>(idx)] = value;
            }
        } else if (key.compare(0, 3, "mpq") == 0 && key.size() > 3 &&
                   key[3] >= '0' && key[3] <= '9') {
            const int idx = std::atoi(key.c_str() + 3);
            if (idx >= 0 && idx < 64 && !value.empty()) {
                if (mpqSlots.size() <= static_cast<std::size_t>(idx)) {
                    mpqSlots.resize(static_cast<std::size_t>(idx) + 1);
                }
                mpqSlots[static_cast<std::size_t>(idx)] = value;
            }
        } else if (key == "undoLimit") {
            SetUndoLimit(std::atoi(line.c_str() + eq + 1));
        } else if (key == "defaultMineral") {
            SetDefaultMineral(std::atoi(line.c_str() + eq + 1));
        } else if (key == "defaultGas") {
            SetDefaultGas(std::atoi(line.c_str() + eq + 1));
        } else if (key == "gameDir") {
            gameDir_ = value;
        } else if (key == "backup") {
            backupEnabled_ = std::atoi(line.c_str() + eq + 1) != 0;
        } else if (key == "backupCount") {
            SetBackupCount(std::atoi(line.c_str() + eq + 1));
        } else if (key == "autoBackup") {
            autoBackupEnabled_ = std::atoi(line.c_str() + eq + 1) != 0;
        } else if (key == "autoBackupMinutes") {
            SetAutoBackupMinutes(std::atoi(line.c_str() + eq + 1));
        }
    }
    for (const std::wstring& p : slots) {
        if (!p.empty()) recent_.push_back(p);
    }
    for (const std::wstring& p : mpqSlots) {
        if (!p.empty()) mpqFiles_.push_back(p);
    }
    return true;
}

bool AppSettings::Save() {
    const std::wstring dir = directory();
    if (dir.empty()) return false;
    // 目录已存在时 CreateDirectory 返回 FALSE + ERROR_ALREADY_EXISTS，可忽略
    ::CreateDirectoryW(dir.c_str(), nullptr);

    std::string out;
    out += "recentCount=" + std::to_string(recent_.size()) + "\n";
    for (std::size_t i = 0; i < recent_.size(); i++) {
        out += "recent" + std::to_string(i) + "=" + WideToUtf8(recent_[i]) + "\n";
    }
    out += "mpqCount=" + std::to_string(mpqFiles_.size()) + "\n";
    for (std::size_t i = 0; i < mpqFiles_.size(); i++) {
        out += "mpq" + std::to_string(i) + "=" + WideToUtf8(mpqFiles_[i]) + "\n";
    }
    out += "defaultMineral=" + std::to_string(defaultMineral_) + "\n";
    out += "defaultGas=" + std::to_string(defaultGas_) + "\n";
    out += "gameDir=" + WideToUtf8(gameDir_) + "\n";
    out += "backup=" + std::to_string(backupEnabled_ ? 1 : 0) + "\n";
    out += "backupCount=" + std::to_string(backupCount_) + "\n";
    out += "autoBackup=" + std::to_string(autoBackupEnabled_ ? 1 : 0) + "\n";
    out += "autoBackupMinutes=" + std::to_string(autoBackupMinutes_) + "\n";
    out += "undoLimit=" + std::to_string(undoLimit_) + "\n";
    return WriteWholeFile(filePath(), out);
}

}  // namespace scmd
