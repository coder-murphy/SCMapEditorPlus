#include "platform/file_dialog.h"

#include <windows.h>
#include <commdlg.h>

namespace scmd {

bool OpenFileDialog(void* ownerHwnd, const wchar_t* filter, std::wstring& outPath) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(ownerHwnd);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn)) return false;
    outPath = buf;
    return true;
}

bool SaveFileDialog(void* ownerHwnd, const wchar_t* filter, const wchar_t* defaultExt,
                    std::wstring& outPath, const wchar_t* initialName) {
    wchar_t buf[MAX_PATH] = L"";
    if (initialName && initialName[0]) {
        ::lstrcpynW(buf, initialName, MAX_PATH);
    }
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(ownerHwnd);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defaultExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn)) return false;
    outPath = buf;
    return true;
}

}  // namespace scmd
