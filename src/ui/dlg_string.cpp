#include "ui/dlg_procs.h"

#include <string>

#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {
std::wstring g_lastString;
}

const wchar_t* LastEditedString() { return g_lastString.c_str(); }

void SeedEditedString(const std::wstring& text) { g_lastString = text; }

// 189：编辑字符串。主编辑框 1120，右侧为预览（1079），左侧行号（1121）。
INT_PTR CALLBACK ProcStringEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_D189_1120, g_lastString.c_str());
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D189_1) {              // 确定
                wchar_t buf[4096] = L"";
                ::GetDlgItemTextW(dlg, IDC_D189_1120, buf, 4095);
                g_lastString = buf;
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D189_2 || LOWORD(wParam) == IDCANCEL) {   // 关闭
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
