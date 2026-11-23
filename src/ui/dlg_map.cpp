#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
MapSizeParams g_mapSize;
ResizeParams g_resize;
}

const MapSizeParams& LastMapSize() { return g_mapSize; }
const ResizeParams& LastResizeParams() { return g_resize; }

// 169：新建地图。尺寸限制 16..256，默认 64x64。
INT_PTR CALLBACK ProcNewMap(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D169_1062, 64, FALSE);
            ::SetDlgItemInt(dlg, IDC_D169_1063, 64, FALSE);
            HWND combo = ::GetDlgItem(dlg, IDC_D169_1141);
            const wchar_t* items[] = {L"无默认触发", L"3 条默认触发", L"4 条默认触发"};
            for (int i = 0; i < 3; i++) {
                const LRESULT idx = ::SendMessageW(combo, CB_ADDSTRING, 0,
                                                   reinterpret_cast<LPARAM>(items[i]));
                if (idx != CB_ERR) {
                    ::SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(idx),
                                   static_cast<LPARAM>(i));
                }
            }
            ::SendMessageW(combo, CB_SETCURSEL, 1, 0);   // 默认 3 条
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D169_1) {                 // 创建地图
                g_mapSize.width = ReadClampedInt(dlg, IDC_D169_1062, 64, 16, 256);
                g_mapSize.height = ReadClampedInt(dlg, IDC_D169_1063, 64, 16, 256);
                g_mapSize.triggerPreset = 1;
                HWND combo = ::GetDlgItem(dlg, IDC_D169_1141);
                const int sel = static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
                if (sel != CB_ERR) {
                    const LRESULT data = ::SendMessageW(combo, CB_GETITEMDATA,
                                                        static_cast<WPARAM>(sel), 0);
                    if (data != CB_ERR) g_mapSize.triggerPreset = static_cast<int>(data);
                }
                Log(L"[newmap] %dx%d preset=%d", g_mapSize.width, g_mapSize.height,
                    g_mapSize.triggerPreset);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D169_2) {                 // 关闭
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

// 175：重设地图大小。带左右/上下偏移与平滑边缘选项。
INT_PTR CALLBACK ProcResizeMap(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D175_1062, 64, FALSE);
            ::SetDlgItemInt(dlg, IDC_D175_1063, 64, FALSE);
            ::SetDlgItemInt(dlg, IDC_D175_1044, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D175_1045, 0, FALSE);
            ::CheckDlgButton(dlg, IDC_D175_1099, BST_CHECKED);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D175_1) {                 // 重设
                g_resize.width = ReadClampedInt(dlg, IDC_D175_1062, 64, 16, 256);
                g_resize.height = ReadClampedInt(dlg, IDC_D175_1063, 64, 16, 256);
                g_resize.offsetX = ReadClampedInt(dlg, IDC_D175_1044, 0, -256, 256);
                g_resize.offsetY = ReadClampedInt(dlg, IDC_D175_1045, 0, -256, 256);
                g_resize.smoothEdges = ::IsDlgButtonChecked(dlg, IDC_D175_1099) == BST_CHECKED;
                Log(L"[resize] %dx%d off=%d,%d smooth=%d", g_resize.width, g_resize.height,
                    g_resize.offsetX, g_resize.offsetY, g_resize.smoothEdges ? 1 : 0);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D175_2) {
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
