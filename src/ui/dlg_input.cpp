#include "ui/dlg_procs.h"

#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {
int g_lastInputValue = 0;
}

int LastInputValue() { return g_lastInputValue; }

// 188：输入数量（1..255）。确定返回数值，取消返回 0。
INT_PTR CALLBACK ProcInputCount(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D188_1118, 1, FALSE);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D188_1) {
                g_lastInputValue = ReadClampedInt(dlg, IDC_D188_1118, 1, 1, 255);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D188_2 || LOWORD(wParam) == IDCANCEL) {
                g_lastInputValue = 0;
                ::EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            g_lastInputValue = 0;
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// 191：重叠单位数量。
INT_PTR CALLBACK ProcOverlapUnits(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D191_1079, 2, FALSE);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D191_1) {
                g_lastInputValue = ReadClampedInt(dlg, IDC_D191_1079, 2, 1, 255);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
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

// 179：重命名开关。设置/还原先只作用于列表显示。
INT_PTR CALLBACK ProcRenameSwitch(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            // TODO: 载入开关列表
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D179_1110) {          // 设置
                wchar_t name[128] = L"";
                ::GetDlgItemTextW(dlg, IDC_D179_1079, name, 127);
                if (name[0]) {
                    const int idx = static_cast<int>(::SendDlgItemMessageW(dlg, IDC_D179_1060, LB_ADDSTRING, 0,
                                                                          reinterpret_cast<LPARAM>(name)));
                    ::SendDlgItemMessageW(dlg, IDC_D179_1060, LB_SETCURSEL, static_cast<WPARAM>(idx), 0);
                }
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D179_1111) {          // 还原
                ::SetDlgItemTextW(dlg, IDC_D179_1079, L"");
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
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
