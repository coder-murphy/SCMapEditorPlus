#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
UnitProps g_unitProps;
unsigned g_spFlags = 0;
}

const UnitProps& LastUnitProps() { return g_unitProps; }
unsigned LastSpFlags() { return g_spFlags; }

// 180：设置单位属性（生命/护盾/魔法/资源/弹药 + 5 个状态位）
INT_PTR CALLBACK ProcUnitProps(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            SetPercent(dlg, IDC_D180_1078, 100);
            SetPercent(dlg, IDC_D180_1079, 100);
            SetPercent(dlg, IDC_D180_1081, 100);
            ::SetDlgItemInt(dlg, IDC_D180_1080, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D180_1082, 0, FALSE);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D180_1) {                 // 确定
                g_unitProps.hp = ReadClampedInt(dlg, IDC_D180_1078, 100, 0, 100);
                g_unitProps.shield = ReadClampedInt(dlg, IDC_D180_1079, 100, 0, 100);
                g_unitProps.energy = ReadClampedInt(dlg, IDC_D180_1081, 100, 0, 100);
                g_unitProps.resource = ReadClampedInt(dlg, IDC_D180_1080, 0, 0, 0x7FFFFF);
                g_unitProps.ammo = ReadClampedInt(dlg, IDC_D180_1082, 0, 0, 255);
                unsigned f = 0;
                if (::IsDlgButtonChecked(dlg, IDC_D180_1073)) f |= 1u << 0;   // 无敌
                if (::IsDlgButtonChecked(dlg, IDC_D180_1074)) f |= 1u << 1;   // 幻影
                if (::IsDlgButtonChecked(dlg, IDC_D180_1075)) f |= 1u << 2;   // 钻地
                if (::IsDlgButtonChecked(dlg, IDC_D180_1076)) f |= 1u << 3;   // 隐形
                if (::IsDlgButtonChecked(dlg, IDC_D180_1077)) f |= 1u << 4;   // 升起
                g_unitProps.flags = f;
                Log(L"[unit] hp=%d sh=%d en=%d res=%d ammo=%d flags=0x%X", g_unitProps.hp,
                    g_unitProps.shield, g_unitProps.energy, g_unitProps.resource, g_unitProps.ammo, f);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D180_2) {
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

// 172：单位名字表。选中条目回填各字段。
INT_PTR CALLBACK ProcUnitNames(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ListAddString(dlg, IDC_D172_1060, L"Marine");
            ListAddString(dlg, IDC_D172_1060, L"Zergling");
            SetPercent(dlg, IDC_D172_1078, 100);
            SetPercent(dlg, IDC_D172_1079, 100);
            SetPercent(dlg, IDC_D172_1081, 100);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D172_1060:                              // 列表选择
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        const int sel = ListSelectedIndex(dlg, IDC_D172_1060);
                        wchar_t name[128] = L"";
                        if (sel >= 0) {
                            ::SendDlgItemMessageW(dlg, IDC_D172_1060, LB_GETTEXT, static_cast<WPARAM>(sel),
                                                  reinterpret_cast<LPARAM>(name));
                            ::SetDlgItemTextW(dlg, IDC_D172_1072, name);
                        }
                    }
                    return TRUE;
                case IDC_D172_1073:                              // 无敌
                case IDC_D172_1074:                              // 幻影
                case IDC_D172_1075:                              // 钻地
                case IDC_D172_1076:                              // 隐形
                case IDC_D172_1077:                              // 升起
                    // BS_AUTOCHECKBOX 由系统自动切换，这里只记日志
                    Log(L"[unitname] flag button %d toggled", LOWORD(wParam));
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

// 164：SP 单位属性。Bit0..Bit14 组成 16 位掩码，edit 显示十六进制。
INT_PTR CALLBACK ProcUnitSpFlags(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            wchar_t hex[16];
            ::wsprintfW(hex, L"0x%04X", g_spFlags);
            ::SetDlgItemTextW(dlg, IDC_D164_1037, hex);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id >= IDC_D164_1023 && id <= IDC_D164_1036) {
                const unsigned bit = 1u << (id - IDC_D164_1023);
                if (::IsDlgButtonChecked(dlg, id) == BST_CHECKED) {
                    ::CheckDlgButton(dlg, id, BST_UNCHECKED);
                    g_spFlags &= ~bit;
                } else {
                    ::CheckDlgButton(dlg, id, BST_CHECKED);
                    g_spFlags |= bit;
                }
                wchar_t hex[16];
                ::wsprintfW(hex, L"0x%04X", g_spFlags);
                ::SetDlgItemTextW(dlg, IDC_D164_1037, hex);
                return TRUE;
            }
            if (id == IDC_D164_1) {                             // 关闭
                ::EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace scmd
