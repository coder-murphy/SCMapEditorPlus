#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"

namespace scmd {

// 103 / 167：关于框。只挂关闭行为，其余为静态文本。
INT_PTR CALLBACK ProcAbout(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D103_1 || LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                ::EndDialog(dlg, IDOK);
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

// 155：基本设置。当前只接主按钮，控件的持久化留给 app 配置模块。
INT_PTR CALLBACK ProcSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            // TODO: 从配置载入 MPQ 列表 / 撤销次数 / 默认资源
            Log(L"[settings] dialog opened");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D155_1:      // 保存设置
                    // TODO: 写回配置
                    Log(L"[settings] save");
                    ::EndDialog(dlg, IDOK);
                    return TRUE;
                case IDC_D155_1014:   // 重置默认
                    ::SetDlgItemInt(dlg, IDC_D155_1009, 8, FALSE);    // 默认水晶
                    ::SetDlgItemInt(dlg, IDC_D155_1010, 1, FALSE);    // 默认气矿
                    ::SetDlgItemInt(dlg, IDC_D155_1013, 64, FALSE);   // 撤销次数
                    Log(L"[settings] reset to defaults");
                    return TRUE;
                case IDC_D155_2:      // 关闭（IDCANCEL 同为 2，故不重复列出）
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

// 163：兼容性警告。列表内容待接入版本检测。
INT_PTR CALLBACK ProcCompatWarning(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            Log(L"[compat] warning shown");
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D163_1) {   // 保存
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D163_2 || LOWORD(wParam) == IDCANCEL) {   // 关闭
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
