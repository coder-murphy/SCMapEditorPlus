#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
LocationRect g_location;

// 181 的「触发」分组框（1113）里嵌入的三个子面板：
//   184 列表视图 / 185 条件编辑 / 186 动作编辑（模板样式本身即 WS_CHILD）
struct TriggerPanels {
    HWND hwnd[3] = {nullptr, nullptr, nullptr};
    int mode = 0;  // 0=原始触发列表；1..3=对应子面板
};

TriggerPanels* PanelsOf(HWND dlg) {
    return reinterpret_cast<TriggerPanels*>(::GetWindowLongPtrW(dlg, GWLP_USERDATA));
}

// 子面板放到分组框内部；单位换算走 MapDialogRect，随 DPI/字体变化
void LayoutPanels(HWND dlg, const TriggerPanels& panels) {
    RECT rc = {2, 84, 366, 314};
    ::MapDialogRect(dlg, &rc);
    for (HWND h : panels.hwnd) {
        if (!h) continue;
        ::SetWindowPos(h, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// 面板显示时隐藏会被遮住的原始控件（触发列表 + 左侧树），回到 0 模式再恢复
void ApplyTriggerMode(HWND dlg, TriggerPanels& panels, int mode) {
    panels.mode = mode;
    for (int i = 0; i < 3; i++) {
        if (panels.hwnd[i]) ::ShowWindow(panels.hwnd[i], mode == i + 1 ? SW_SHOW : SW_HIDE);
    }
    const int listShow = mode == 0 ? SW_SHOW : SW_HIDE;
    ::ShowWindow(::GetDlgItem(dlg, IDC_D181_182), listShow);
    ::ShowWindow(::GetDlgItem(dlg, IDC_D181_1110), listShow);
    Log(L"[trigger] view=%d", mode);
}
}  // namespace

const LocationRect& LastLocation() { return g_location; }

// 181：触发编辑器。已内嵌 184/185/186 子面板；列表操作已接通，触发内容编辑待接线。
INT_PTR CALLBACK ProcTriggerEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ListAddString(dlg, IDC_D181_182, L"[玩家 1] 初始触发");

            auto* panels = new TriggerPanels();
            ::SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panels));
            const HINSTANCE inst =
                reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(dlg, GWLP_HINSTANCE));
            panels->hwnd[0] = ::CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_DLG_184), dlg,
                                                   &ProcListViewHost, 0);
            panels->hwnd[1] = ::CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_DLG_185), dlg,
                                                   &ProcListEditorA, 0);
            panels->hwnd[2] = ::CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_DLG_186), dlg,
                                                   &ProcListEditorB, 0);
            LayoutPanels(dlg, *panels);
            ApplyTriggerMode(dlg, *panels, 0);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D181_1101: {                            // 修改：在原始列表/184/185/186 间切换
                    TriggerPanels* panels = PanelsOf(dlg);
                    if (panels) ApplyTriggerMode(dlg, *panels, (panels->mode + 1) % 4);
                    return TRUE;
                }
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
                case IDC_D181_1:                                 // 关闭
                    ::EndDialog(dlg, IDCANCEL);
                    return TRUE;
                default:
                    break;
            }
            break;
        case WM_DESTROY: {
            TriggerPanels* panels = PanelsOf(dlg);
            ::SetWindowLongPtrW(dlg, GWLP_USERDATA, 0);
            delete panels;                                   // 子面板随父对话框销毁
            return TRUE;
        }
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
