#include "ui/dlg_common.h"

namespace scmd {

namespace {
MapDocument* g_dialogDocument = nullptr;
}  // namespace

void SetDialogDocument(MapDocument* doc) { g_dialogDocument = doc; }
MapDocument* DialogDocument() { return g_dialogDocument; }

void CenterDialog(HWND dlg) {
    RECT rcDlg, rcOwner;
    ::GetWindowRect(dlg, &rcDlg);
    HWND owner = ::GetWindow(dlg, GW_OWNER);
    if (!owner) {
        ::GetWindowRect(::GetDesktopWindow(), &rcOwner);
    } else {
        ::GetWindowRect(owner, &rcOwner);
    }
    const int w = rcDlg.right - rcDlg.left;
    const int h = rcDlg.bottom - rcDlg.top;
    const int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
    const int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;
    ::SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

int ReadClampedInt(HWND dlg, int controlId, int defValue, int minValue, int maxValue) {
    BOOL ok = FALSE;
    const int v = static_cast<int>(::GetDlgItemInt(dlg, controlId, &ok, FALSE));
    if (!ok) return defValue;
    if (v < minValue) return minValue;
    if (v > maxValue) return maxValue;
    return v;
}

void SetPercent(HWND dlg, int controlId, int value) {
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    ::SetDlgItemInt(dlg, controlId, static_cast<UINT>(value), FALSE);
}

void ListAddString(HWND dlg, int listId, const wchar_t* text) {
    if (!text || !text[0]) return;
    const LRESULT idx = ::SendDlgItemMessageW(dlg, listId, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    if (idx >= 0) ::SendDlgItemMessageW(dlg, listId, LB_SETCURSEL, static_cast<WPARAM>(idx), 0);
}

void ListRemoveSelection(HWND dlg, int listId) {
    const int sel = static_cast<int>(::SendDlgItemMessageW(dlg, listId, LB_GETCURSEL, 0, 0));
    if (sel >= 0) ::SendDlgItemMessageW(dlg, listId, LB_DELETESTRING, static_cast<WPARAM>(sel), 0);
}

void ListMoveSelection(HWND dlg, int listId, int delta) {
    const int sel = static_cast<int>(::SendDlgItemMessageW(dlg, listId, LB_GETCURSEL, 0, 0));
    if (sel < 0) return;
    const int count = static_cast<int>(::SendDlgItemMessageW(dlg, listId, LB_GETCOUNT, 0, 0));
    const int dst = sel + delta;
    if (dst < 0 || dst >= count) return;
    wchar_t buf[512] = L"";
    ::SendDlgItemMessageW(dlg, listId, LB_GETTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(buf));
    ::SendDlgItemMessageW(dlg, listId, LB_DELETESTRING, static_cast<WPARAM>(sel), 0);
    ::SendDlgItemMessageW(dlg, listId, LB_INSERTSTRING, static_cast<WPARAM>(dst), reinterpret_cast<LPARAM>(buf));
    ::SendDlgItemMessageW(dlg, listId, LB_SETCURSEL, static_cast<WPARAM>(dst), 0);
}

int ListSelectedIndex(HWND dlg, int listId) {
    return static_cast<int>(::SendDlgItemMessageW(dlg, listId, LB_GETCURSEL, 0, 0));
}

}  // namespace scmd
