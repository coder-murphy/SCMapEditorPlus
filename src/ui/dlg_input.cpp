#include "ui/dlg_procs.h"

#include <algorithm>
#include <string>

#include "common/string_util.h"
#include "map/map_document.h"
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
            ::CheckDlgButton(dlg, IDC_D188_1119, BST_UNCHECKED);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D188_1119) {   // 所有单位：固定 255 并禁用输入
                const bool all = ::IsDlgButtonChecked(dlg, IDC_D188_1119) == BST_CHECKED;
                ::EnableWindow(::GetDlgItem(dlg, IDC_D188_1118), !all);
                if (all) ::SetDlgItemInt(dlg, IDC_D188_1118, 255, FALSE);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D188_1) {
                if (::IsDlgButtonChecked(dlg, IDC_D188_1119) == BST_CHECKED) {
                    g_lastInputValue = 255;
                } else {
                    g_lastInputValue = ReadClampedInt(dlg, IDC_D188_1118, 1, 1, 255);
                }
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

// 179：重命名开关（SWNM = u32[256] 的 STR 下标）
namespace {

void FillSwitchList(HWND dlg, int select) {
    MapDocument* doc = DialogDocument();
    HWND list = ::GetDlgItem(dlg, IDC_D179_1060);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (!doc) return;
    for (std::size_t i = 0; i < doc->switchNames().Count(); i++) {
        const u32 sid = doc->switchNames().Get(i);
        const std::wstring name = sid == 0 ? std::wstring() : Utf8ToWide(doc->strings().At(sid));
        wchar_t line[320];
        ::wsprintfW(line, L"开关 %u: %ls", static_cast<unsigned>(i) + 1,
                    name.empty() ? L"(默认)" : name.c_str());
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    }
    if (select < 0) select = 0;
    if (select >= static_cast<int>(doc->switchNames().Count())) {
        select = static_cast<int>(doc->switchNames().Count()) - 1;
    }
    if (select >= 0) ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
}

void LoadSwitchField(HWND dlg) {
    MapDocument* doc = DialogDocument();
    if (!doc) return;
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D179_1060, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    const u32 sid = doc->switchNames().Get(static_cast<std::size_t>(sel));
    const std::wstring name = sid == 0 ? std::wstring() : Utf8ToWide(doc->strings().At(sid));
    ::SetDlgItemTextW(dlg, IDC_D179_1079, name.c_str());
}

}  // namespace

INT_PTR CALLBACK ProcRenameSwitch(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            FillSwitchList(dlg, 0);
            LoadSwitchField(dlg);
            return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_D179_1060 && HIWORD(wParam) == LBN_SELCHANGE) {
                LoadSwitchField(dlg);
                return TRUE;
            }
            if (id == IDC_D179_1110) {          // 设置
                MapDocument* doc = DialogDocument();
                const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D179_1060, LB_GETCURSEL, 0, 0);
                if (!doc || sel < 0) return TRUE;
                wchar_t name[128] = L"";
                ::GetDlgItemTextW(dlg, IDC_D179_1079, name, 128);
                if (!name[0]) {
                    MessageBeep(MB_ICONWARNING);
                    return TRUE;
                }
                int sid = static_cast<int>(doc->switchNames().Get(static_cast<std::size_t>(sel)));
                if (sid <= 0 || static_cast<std::size_t>(sid) >= doc->strings().Count()) {
                    sid = static_cast<int>(std::max<std::size_t>(1, doc->strings().Count()));
                }
                doc->mutableStrings().Set(static_cast<std::size_t>(sid), WideToUtf8(name));
                doc->mutableSwitchNames().Set(static_cast<std::size_t>(sel),
                                              static_cast<u32>(sid));
                doc->MarkDirty();
                FillSwitchList(dlg, static_cast<int>(sel));
                LoadSwitchField(dlg);
                return TRUE;
            }
            if (id == IDC_D179_1111) {          // 还原
                MapDocument* doc = DialogDocument();
                const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D179_1060, LB_GETCURSEL, 0, 0);
                if (!doc || sel < 0) return TRUE;
                if (doc->mutableSwitchNames().Set(static_cast<std::size_t>(sel), 0)) {
                    doc->MarkDirty();
                }
                FillSwitchList(dlg, static_cast<int>(sel));
                LoadSwitchField(dlg);
                return TRUE;
            }
            if (id == IDCANCEL) {
                ::EndDialog(dlg, IDOK);      // 修改已实时写入
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            ::EndDialog(dlg, IDOK);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace scmd
