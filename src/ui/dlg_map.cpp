#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
MapSizeParams g_mapSize;
ResizeParams g_resize;

const wchar_t* const kTilesetNames[8] = {
    L"Badlands", L"Space", L"Installation", L"Ashworld",
    L"Jungle", L"Desert", L"Ice", L"Twilight",
};
const int kMapSizes[] = {64, 96, 128, 192, 256};

int SelectedItemData(HWND dlg, int listId, int fallback) {
    const LRESULT sel = ::SendDlgItemMessageW(dlg, listId, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return fallback;
    const LRESULT data = ::SendDlgItemMessageW(dlg, listId, LB_GETITEMDATA,
                                               static_cast<WPARAM>(sel), 0);
    return data == LB_ERR ? fallback : static_cast<int>(data);
}

void FillTilesetList(HWND dlg, int listId, int selectedEra) {
    ::SendDlgItemMessageW(dlg, listId, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 8; i++) {
        const LRESULT at = ::SendDlgItemMessageW(
            dlg, listId, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kTilesetNames[i]));
        if (at != LB_ERR) {
            ::SendDlgItemMessageW(dlg, listId, LB_SETITEMDATA, static_cast<WPARAM>(at),
                                  static_cast<LPARAM>(i));
        }
    }
    ::SendDlgItemMessageW(dlg, listId, LB_SETCURSEL,
                          static_cast<WPARAM>(selectedEra < 0 ? 0 : selectedEra & 7), 0);
}

void FillSizeList(HWND dlg, int listId, int selectedSize) {
    ::SendDlgItemMessageW(dlg, listId, LB_RESETCONTENT, 0, 0);
    int selected = 0;
    for (int i = 0; i < 5; i++) {
        wchar_t label[32];
        ::wsprintfW(label, L"%d x %d", kMapSizes[i], kMapSizes[i]);
        const LRESULT at = ::SendDlgItemMessageW(dlg, listId, LB_ADDSTRING, 0,
                                                 reinterpret_cast<LPARAM>(label));
        if (at != LB_ERR) {
            ::SendDlgItemMessageW(dlg, listId, LB_SETITEMDATA, static_cast<WPARAM>(at),
                                  static_cast<LPARAM>(kMapSizes[i]));
            if (kMapSizes[i] == selectedSize) selected = i;
        }
    }
    ::SendDlgItemMessageW(dlg, listId, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
}
}  // namespace

const MapSizeParams& LastMapSize() { return g_mapSize; }
const ResizeParams& LastResizeParams() { return g_resize; }
void SeedResizeParams(const ResizeParams& seed) { g_resize = seed; }

// 169：新建地图。尺寸限制 16..256，默认 64x64。
INT_PTR CALLBACK ProcNewMap(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D169_1062, static_cast<UINT>(g_mapSize.width), FALSE);
            ::SetDlgItemInt(dlg, IDC_D169_1063, static_cast<UINT>(g_mapSize.height), FALSE);
            FillSizeList(dlg, IDC_D169_1061, g_mapSize.width);
            FillTilesetList(dlg, IDC_D169_1060, g_mapSize.era);
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
            ::SendMessageW(combo, CB_SETCURSEL,
                           static_cast<WPARAM>(g_mapSize.triggerPreset), 0);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D169_1061 && HIWORD(wParam) == LBN_SELCHANGE) {
                const int size = SelectedItemData(dlg, IDC_D169_1061, g_mapSize.width);
                ::SetDlgItemInt(dlg, IDC_D169_1062, static_cast<UINT>(size), FALSE);
                ::SetDlgItemInt(dlg, IDC_D169_1063, static_cast<UINT>(size), FALSE);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D169_1) {                 // 创建地图
                g_mapSize.width = ReadClampedInt(dlg, IDC_D169_1062, 64, 16, 256);
                g_mapSize.height = ReadClampedInt(dlg, IDC_D169_1063, 64, 16, 256);
                g_mapSize.era = SelectedItemData(dlg, IDC_D169_1060, 0) & 7;
                g_mapSize.triggerPreset = 1;
                HWND combo = ::GetDlgItem(dlg, IDC_D169_1141);
                const int sel = static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
                if (sel != CB_ERR) {
                    const LRESULT data = ::SendMessageW(combo, CB_GETITEMDATA,
                                                        static_cast<WPARAM>(sel), 0);
                    if (data != CB_ERR) g_mapSize.triggerPreset = static_cast<int>(data);
                }
                Log(L"[newmap] %dx%d era=%d preset=%d", g_mapSize.width, g_mapSize.height,
                    g_mapSize.era, g_mapSize.triggerPreset);
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

// 175：重设地图大小。带左右/上下偏移、平滑边缘与目标地形集。
INT_PTR CALLBACK ProcResizeMap(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D175_1062, static_cast<UINT>(g_resize.width), FALSE);
            ::SetDlgItemInt(dlg, IDC_D175_1063, static_cast<UINT>(g_resize.height), FALSE);
            ::SetDlgItemInt(dlg, IDC_D175_1044, static_cast<UINT>(g_resize.offsetX), FALSE);
            ::SetDlgItemInt(dlg, IDC_D175_1045, static_cast<UINT>(g_resize.offsetY), FALSE);
            ::CheckDlgButton(dlg, IDC_D175_1099,
                             g_resize.smoothEdges ? BST_CHECKED : BST_UNCHECKED);
            FillTilesetList(dlg, IDC_D175_1060, g_resize.currentEra);
            FillTilesetList(dlg, IDC_D175_1095,
                            g_resize.era >= 0 ? g_resize.era : g_resize.currentEra);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D175_1) {                 // 重设 / 改变 Tileset
                g_resize.width = ReadClampedInt(dlg, IDC_D175_1062, 64, 16, 256);
                g_resize.height = ReadClampedInt(dlg, IDC_D175_1063, 64, 16, 256);
                g_resize.offsetX = ReadClampedInt(dlg, IDC_D175_1044, 0, -256, 256);
                g_resize.offsetY = ReadClampedInt(dlg, IDC_D175_1045, 0, -256, 256);
                g_resize.smoothEdges = ::IsDlgButtonChecked(dlg, IDC_D175_1099) == BST_CHECKED;
                const int targetEra =
                    SelectedItemData(dlg, IDC_D175_1095, g_resize.currentEra) & 7;
                g_resize.era = targetEra == g_resize.currentEra ? -1 : targetEra;
                Log(L"[resize] %dx%d off=%d,%d smooth=%d era=%d->%d", g_resize.width,
                    g_resize.height, g_resize.offsetX, g_resize.offsetY,
                    g_resize.smoothEdges ? 1 : 0, g_resize.currentEra, targetEra);
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
