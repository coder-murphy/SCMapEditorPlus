#include "ui/dlg_procs.h"

#include <commdlg.h>
#include <string>

#include "common/log.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {

std::wstring WidenName(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = static_cast<int>(s.size());
    const int n = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), len, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), len, w.data(), n);
    return w;
}

std::string NarrowName(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int len = static_cast<int>(w.size());
    const int n = ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), len, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), len, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring BaseNameOf(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

bool ReadWholeFile(const std::wstring& path, Buffer& out) {
    out.clear();
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = false;
    if (::GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart <= (64LL << 20)) {
        out.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (out.empty()) {
            ok = true;
        } else if (::ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &read, nullptr)) {
            ok = read == out.size();
        }
        if (!ok) out.clear();
    }
    ::CloseHandle(f);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const Buffer& data) {
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() ||
                    (::WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                     written == data.size());
    ::CloseHandle(f);
    return ok;
}

void RefreshMpqList(HWND dlg) {
    ::SendMessageW(::GetDlgItem(dlg, IDC_D167_1049), LB_RESETCONTENT, 0, 0);
    MapDocument* doc = DialogDocument();
    if (!doc || !doc->IsOpen()) {
        ::SetDlgItemTextW(dlg, IDC_D167_1055, L"未打开地图");
        return;
    }
    for (const MpqFileEntry& f : doc->extraFiles()) {
        ListAddString(dlg, IDC_D167_1049, WidenName(f.name).c_str());
    }
    wchar_t info[64];
    ::wsprintfW(info, L"共 %u 个文件", static_cast<unsigned>(doc->extraFiles().size()));
    ::SetDlgItemTextW(dlg, IDC_D167_1055, info);
}

void ShowMpqSelection(HWND dlg) {
    MapDocument* doc = DialogDocument();
    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
    if (!doc || !doc->IsOpen() || sel < 0 ||
        sel >= static_cast<int>(doc->extraFiles().size())) {
        return;
    }
    const MpqFileEntry& f = doc->extraFiles()[static_cast<std::size_t>(sel)];
    wchar_t info[320];
    ::wsprintfW(info, L"%u 字节  %ls", static_cast<unsigned>(f.data.size()),
                WidenName(f.name).c_str());
    ::SetDlgItemTextW(dlg, IDC_D167_1055, info);
    ::SetDlgItemTextW(dlg, IDC_D167_1056, WidenName(f.name).c_str());
}

// 让用户在本地文件对话框里选文件；返回空表示取消
std::wstring PickOpenFile(HWND dlg) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return ::GetOpenFileNameW(&ofn) ? std::wstring(path) : std::wstring();
}

std::wstring PickSaveFile(HWND dlg, const std::wstring& suggested) {
    wchar_t path[MAX_PATH] = L"";
    if (!suggested.empty() && suggested.size() < MAX_PATH) {
        ::lstrcpynW(path, suggested.c_str(), MAX_PATH);
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return ::GetSaveFileNameW(&ofn) ? std::wstring(path) : std::wstring();
}

}  // namespace

// 167：MPQ 文件管理（当前地图的附加文件：列出 / 导入 / 导出 / 改名 / 添加 / 删除）
INT_PTR CALLBACK ProcMpqList(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            RefreshMpqList(dlg);
            MapDocument* doc = DialogDocument();
            if (!doc || !doc->IsOpen()) {
                const int ids[] = {IDC_D167_1049, IDC_D167_1050, IDC_D167_1051,
                                   IDC_D167_1052, IDC_D167_1053, IDC_D167_1056,
                                   IDC_D167_1092};
                for (int id : ids) ::EnableWindow(::GetDlgItem(dlg, id), FALSE);
            }
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D167_1049:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        ShowMpqSelection(dlg);
                    }
                    return TRUE;
                case IDC_D167_1053: {                            // 添加（空文件）
                    MapDocument* doc = DialogDocument();
                    if (!doc || !doc->IsOpen()) return TRUE;
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 511);
                    if (!name[0]) {
                        ::MessageBoxW(dlg, L"请先在文件名框输入条目名称。", L"MPQ 文件管理",
                                      MB_ICONINFORMATION);
                        return TRUE;
                    }
                    if (!doc->SetExtraFile(NarrowName(name), Buffer())) {
                        ::MessageBoxW(dlg, L"该名称无效（不能覆盖 listfile 或 scenario.chk）。",
                                      L"MPQ 文件管理", MB_ICONWARNING);
                        return TRUE;
                    }
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1092: {                            // 删除
                    MapDocument* doc = DialogDocument();
                    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
                    if (!doc || !doc->IsOpen() || sel < 0 ||
                        sel >= static_cast<int>(doc->extraFiles().size())) {
                        return TRUE;
                    }
                    doc->RemoveExtraFile(doc->extraFiles()[static_cast<std::size_t>(sel)].name);
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1050: {                            // 导入本地文件
                    MapDocument* doc = DialogDocument();
                    if (!doc || !doc->IsOpen()) return TRUE;
                    const std::wstring path = PickOpenFile(dlg);
                    if (path.empty()) return TRUE;
                    Buffer data;
                    if (!ReadWholeFile(path, data)) {
                        ::MessageBoxW(dlg, L"读取本地文件失败。", L"MPQ 文件管理", MB_ICONERROR);
                        return TRUE;
                    }
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 511);
                    const std::string entry =
                        name[0] ? NarrowName(name) : NarrowName(BaseNameOf(path));
                    if (!doc->SetExtraFile(entry, std::move(data))) {
                        ::MessageBoxW(dlg, L"该名称无效（不能覆盖 listfile 或 scenario.chk）。",
                                      L"MPQ 文件管理", MB_ICONWARNING);
                        return TRUE;
                    }
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1051: {                            // 导出选中条目
                    MapDocument* doc = DialogDocument();
                    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
                    if (!doc || !doc->IsOpen() || sel < 0 ||
                        sel >= static_cast<int>(doc->extraFiles().size())) {
                        return TRUE;
                    }
                    const MpqFileEntry& f = doc->extraFiles()[static_cast<std::size_t>(sel)];
                    const std::wstring path = PickSaveFile(dlg, BaseNameOf(WidenName(f.name)));
                    if (path.empty()) return TRUE;
                    if (!WriteWholeFile(path, f.data)) {
                        ::MessageBoxW(dlg, L"写入本地文件失败。", L"MPQ 文件管理", MB_ICONERROR);
                    }
                    return TRUE;
                }
                case IDC_D167_1052: {                            // 编辑（改名）
                    MapDocument* doc = DialogDocument();
                    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
                    if (!doc || !doc->IsOpen() || sel < 0 ||
                        sel >= static_cast<int>(doc->extraFiles().size())) {
                        return TRUE;
                    }
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 511);
                    if (!name[0]) {
                        ::MessageBoxW(dlg, L"请先输入新的条目名称。", L"MPQ 文件管理",
                                      MB_ICONINFORMATION);
                        return TRUE;
                    }
                    const std::string oldName =
                        doc->extraFiles()[static_cast<std::size_t>(sel)].name;
                    const std::string newName = NarrowName(name);
                    if (_stricmp(oldName.c_str(), newName.c_str()) == 0) return TRUE;
                    Buffer existing;
                    if (doc->ReadExtraFile(newName, existing)) {
                        ::MessageBoxW(dlg, L"同名条目已存在。", L"MPQ 文件管理", MB_ICONWARNING);
                        return TRUE;
                    }
                    Buffer data;
                    doc->ReadExtraFile(oldName, data);
                    doc->RemoveExtraFile(oldName);
                    if (!doc->SetExtraFile(newName, std::move(data))) {
                        ::MessageBoxW(dlg, L"该名称无效（不能覆盖 listfile 或 scenario.chk）。",
                                      L"MPQ 文件管理", MB_ICONWARNING);
                    }
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1:
                case IDC_D167_1059:
                    ::EndDialog(dlg, IDCANCEL);
                    return TRUE;
                default:
                    break;
            }
            break;
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// 183：标签容器
INT_PTR CALLBACK ProcTabHost(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D183_1) {                  // 确定
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D183_2) {
                ::EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// 184：纯列表容器
INT_PTR CALLBACK ProcListViewHost(HWND dlg, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// 185 / 186：触发条件 / 行动列表，实际逻辑在 dlg_triggers.cpp
INT_PTR CALLBACK ProcListEditorA(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    return HandleTriggerListEditor(dlg, msg, wParam);
}
INT_PTR CALLBACK ProcListEditorB(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    return HandleTriggerListEditor(dlg, msg, wParam);
}

// 187：富文本（RichEdit20A）
namespace {
std::wstring g_richText;
}
const wchar_t* LastRichText() { return g_richText.c_str(); }
void SeedRichText(const std::wstring& text) { g_richText = text; }

INT_PTR CALLBACK ProcRichText(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_D187_1134, g_richText.c_str());
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D187_1) {
                wchar_t buf[4096] = L"";
                ::GetDlgItemTextW(dlg, IDC_D187_1134, buf, 4095);
                g_richText = buf;
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D187_2) {
                ::EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace scmd
