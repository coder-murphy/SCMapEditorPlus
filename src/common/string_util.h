#pragma once
#include <string>

namespace scmd {

// 地图路径多为中文，内部统一用 UTF-8 std::string 存路径/文件名，
// 只在调用 Win32 API 时转 UTF-16。
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

}  // namespace scmd
