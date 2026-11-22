#pragma once
#include <string>

namespace scmd {

// 打开/保存地图文件对话框（保持路径为 UTF-8）
bool OpenFileDialog(void* ownerHwnd, const wchar_t* filter, std::wstring& outPath);
bool SaveFileDialog(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                    std::wstring& outPath, const wchar_t* initialName = nullptr);

}  // namespace scmd
