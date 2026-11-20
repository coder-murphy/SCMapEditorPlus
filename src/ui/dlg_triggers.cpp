#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
LocationRect g_location;
}

const LocationRect& LastLocation() { return g_location; }

// 181：触发编辑器。列表操作已接通，触发内容编辑待 triggers 模块。
INT_PTR CALLBACK ProcTriggerEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ListAddString(dlg, IDC_D181_182, L"[玩家 1] 初始触发");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D181_1100:                              // 新建
                    ListAddString(dlg, IDC_D181_182, L"新触发");
                    return TRUE;
                case IDC_D181_1105: {                            // 复制
                    const int sel = ListSelectedIndex(dlg, IDC_D181_182);
                    if (sel >= 0) {
                        wchar_t buf[256] = L"";
                        ::SendDlgItemMessageW(dlg, IDC_D181_182, LB_GETTEXT, static_cast<WPARAM>(sel),
                                              reinterpret_cast<LPARAM>(buf));
                        ListAddString(dlg, IDC_D181_182, buf);
                    }
                    return TRUE;
                }
                case IDC_D181_1104:                              // 删除
                    ListRemoveSelection(dlg, IDC_D181_182);
                    return TRUE;
                case IDC_D181_1001:                              // 上移
                    ListMoveSelection(dlg, IDC_D181_182, -1);
                    return TRUE;
                case IDC_D181_1002:                              // 下移
                    ListMoveSelection(dlg, IDC_D181_182, 1);
                    return TRUE;
                case IDC_D181_1101:                              // 修改
                    Log(L"[trigger] edit selected (TODO: 打开条件/动作编辑)");
                    return TRUE;
                case IDC_D181_1:                                 // 关闭
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

// 171：编辑 Location。六个地形高度按钮 + 四边坐标。
INT_PTR CALLBACK ProcEditLocation(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D171_1079, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1080, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1081, 16, FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1082, 16, FALSE);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D171_1065: g_location.elevation = 0; return TRUE;   // 凹陷
                case IDC_D171_1066: g_location.elevation = 1; return TRUE;   // 平地
                case IDC_D171_1067: g_location.elevation = 2; return TRUE;   // 高地
                case IDC_D171_1069: g_location.elevation = 3; return TRUE;   // 较低空中
                case IDC_D171_1068: g_location.elevation = 4; return TRUE;   // 普通空中
                case IDC_D171_1070: g_location.elevation = 5; return TRUE;   // 较高空中
                case IDC_D171_1:                                             // 确定
                    g_location.left = ReadClampedInt(dlg, IDC_D171_1079, 0, 0, 255);
                    g_location.top = ReadClampedInt(dlg, IDC_D171_1080, 0, 0, 255);
                    g_location.right = ReadClampedInt(dlg, IDC_D171_1081, 16, 0, 255);
                    g_location.bottom = ReadClampedInt(dlg, IDC_D171_1082, 16, 0, 255);
                    Log(L"[location] rect=(%d,%d)-(%d,%d) elev=%d", g_location.left, g_location.top,
                        g_location.right, g_location.bottom, g_location.elevation);
                    ::EndDialog(dlg, IDOK);
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
