#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
GridSettings g_grid;
}

const GridSettings& LastGridSettings() { return g_grid; }

// 166：方格设置（两组 X/Y 与偏移 + 启用开关）
INT_PTR CALLBACK ProcGridSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D166_1044, 32, FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1045, 32, FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1046, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1047, 0, FALSE);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D166_1) {                 // 确定
                g_grid.enabled = ::IsDlgButtonChecked(dlg, IDC_D166_1048) == BST_CHECKED;
                g_grid.cellX = ReadClampedInt(dlg, IDC_D166_1044, 32, 1, 64);
                g_grid.cellY = ReadClampedInt(dlg, IDC_D166_1045, 32, 1, 64);
                g_grid.offsetX = ReadClampedInt(dlg, IDC_D166_1046, 0, -64, 64);
                g_grid.offsetY = ReadClampedInt(dlg, IDC_D166_1047, 0, -64, 64);
                Log(L"[grid] %dx%d off=%d,%d on=%d", g_grid.cellX, g_grid.cellY,
                    g_grid.offsetX, g_grid.offsetY, g_grid.enabled ? 1 : 0);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D166_2) {
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

// 182：自定义刷子（列表增删；文件读写待 terrain 模块）
INT_PTR CALLBACK ProcCustomBrush(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ListAddString(dlg, IDC_D182_1060, L"默认刷子");
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D182_1006: {                            // 添加刷子
                    wchar_t name[128] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D182_1079, name, 127);
                    ListAddString(dlg, IDC_D182_1060, name);
                    return TRUE;
                }
                case IDC_D182_1124:                              // 删除
                    ListRemoveSelection(dlg, IDC_D182_1060);
                    return TRUE;
                case IDC_D182_1059:                              // 打开文件
                    Log(L"[brush] open file (TODO)");
                    return TRUE;
                case IDC_D182_1125:                              // 保存文件
                    Log(L"[brush] save file (TODO)");
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
