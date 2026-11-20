#include "ui/dlg_procs.h"

#include <commctrl.h>

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

// 173：查看备份数据
INT_PTR CALLBACK ProcBackupViewer(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ListAddString(dlg, IDC_D173_1083, L"自动备份 #1");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D173_1084:                              // 恢复
                    Log(L"[backup] restore selected (TODO)");
                    return TRUE;
                case IDC_D173_1085:                              // 删除文档
                    ListRemoveSelection(dlg, IDC_D173_1083);
                    return TRUE;
                case IDC_D173_1059:                              // 删除地图组
                    ListRemoveSelection(dlg, IDC_D173_1060);
                    return TRUE;
                case IDC_D173_1086:                              // 清除备份
                    ::SendDlgItemMessageW(dlg, IDC_D173_1083, LB_RESETCONTENT, 0, 0);
                    Log(L"[backup] cleared");
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

// 174：在线升级（下载逻辑待接入）
INT_PTR CALLBACK ProcUpdater(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SendDlgItemMessageW(dlg, IDC_D174_1087, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            ::SendDlgItemMessageW(dlg, IDC_D174_1087, PBM_SETPOS, 0, 0);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D174_1090) {              // 下载升级文件
                ::SetDlgItemTextW(dlg, IDC_D174_1091, L"升级服务尚未接入");
                Log(L"[update] download requested (TODO)");
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D174_1) {
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

// 192：Iscript 测试（动画调试工具，仅接播放/重启）
INT_PTR CALLBACK ProcIscriptTest(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_D192_1132, L"未载入单位");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D192_1130:                              // Play
                    Log(L"[iscript] play");
                    return TRUE;
                case IDC_D192_1080:                              // Restart
                    Log(L"[iscript] restart");
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

}  // namespace scmd
