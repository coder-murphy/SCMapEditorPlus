#include "ui/dlg_procs.h"

#include <string>

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

// 167：MPQ 文件管理
INT_PTR CALLBACK ProcMpqList(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ListAddString(dlg, IDC_D167_1049, L"StarDat.mpq");
            ListAddString(dlg, IDC_D167_1049, L"BrooDat.mpq");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D167_1053: {                            // 添加
                    wchar_t name[128] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 127);
                    ListAddString(dlg, IDC_D167_1049, name);
                    return TRUE;
                }
                case IDC_D167_1092:                              // 删除
                    ListRemoveSelection(dlg, IDC_D167_1049);
                    return TRUE;
                case IDC_D167_1050:                              // 导入
                case IDC_D167_1051:                              // 导出
                case IDC_D167_1052:                              // 编辑
                    Log(L"[mpq] command %d (TODO)", LOWORD(wParam));
                    return TRUE;
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

// 185 / 186：同一套按钮（新建/编辑/复制/删除/上移/下移），列表 ID 不同
namespace {
INT_PTR HandleListEditor(HWND dlg, UINT msg, WPARAM wParam) {
    const int listId = ::GetDlgItem(dlg, IDC_D185_1123) ? IDC_D185_1123 : IDC_D186_1115;
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D185_1108:
                    ListAddString(dlg, listId, L"新建项");
                    return TRUE;
                case IDC_D185_1109: {
                    const int sel = ListSelectedIndex(dlg, listId);
                    if (sel >= 0) {
                        wchar_t buf[256] = L"";
                        ::SendDlgItemMessageW(dlg, listId, LB_GETTEXT, static_cast<WPARAM>(sel),
                                              reinterpret_cast<LPARAM>(buf));
                        ListAddString(dlg, listId, buf);
                    }
                    return TRUE;
                }
                case IDC_D185_1107:
                    ListRemoveSelection(dlg, listId);
                    return TRUE;
                case IDC_D185_1001:
                    ListMoveSelection(dlg, listId, -1);
                    return TRUE;
                case IDC_D185_1002:
                    ListMoveSelection(dlg, listId, 1);
                    return TRUE;
                case IDC_D185_1106:
                    Log(L"[list] edit selected (TODO)");
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
}  // namespace

INT_PTR CALLBACK ProcListEditorA(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    return HandleListEditor(dlg, msg, wParam);
}
INT_PTR CALLBACK ProcListEditorB(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    return HandleListEditor(dlg, msg, wParam);
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
