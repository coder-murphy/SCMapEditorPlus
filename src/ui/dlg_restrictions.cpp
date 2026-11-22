#include "ui/dlg_procs.h"

#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {

UpgradeTechRestrictions g_restrictionSeed;
UpgradeTechRestrictions g_restrictions;
bool g_versionBW = false;
int g_player = -1;   // -1 全局默认，0..11 玩家
bool g_loading = false;

RestrictionTable& Table(bool tech) {
    return g_restrictions.Table(g_versionBW, tech);
}

int SelectedIndex(HWND dlg, int listId) {
    HWND list = ::GetDlgItem(dlg, listId);
    return static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
}

void RefreshLists(HWND dlg) {
    g_loading = true;
    HWND upgradeList = ::GetDlgItem(dlg, IDC_D197_UPGRADE_LIST);
    HWND techList = ::GetDlgItem(dlg, IDC_D197_TECH_LIST);
    ::SendMessageW(upgradeList, LB_RESETCONTENT, 0, 0);
    ::SendMessageW(techList, LB_RESETCONTENT, 0, 0);

    const RestrictionTable& up = Table(false);
    for (int i = 0; i < up.itemCount; i++) {
        wchar_t label[32];
        ::wsprintfW(label, L"升级 %d", i);
        ::SendMessageW(upgradeList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    const RestrictionTable& tech = Table(true);
    for (int i = 0; i < tech.itemCount; i++) {
        wchar_t label[32];
        ::wsprintfW(label, L"科技 %d", i);
        ::SendMessageW(techList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    if (up.itemCount > 0) ::SendMessageW(upgradeList, LB_SETCURSEL, 0, 0);
    if (tech.itemCount > 0) ::SendMessageW(techList, LB_SETCURSEL, 0, 0);
    g_loading = false;
}

void LoadSelection(HWND dlg) {
    const RestrictionTable& up = Table(false);
    const RestrictionTable& tech = Table(true);
    int upSel = SelectedIndex(dlg, IDC_D197_UPGRADE_LIST);
    int techSel = SelectedIndex(dlg, IDC_D197_TECH_LIST);
    if (upSel < 0 && up.itemCount > 0) {
        upSel = 0;
        ::SendMessageW(::GetDlgItem(dlg, IDC_D197_UPGRADE_LIST), LB_SETCURSEL, 0, 0);
    }
    if (techSel < 0 && tech.itemCount > 0) {
        techSel = 0;
        ::SendMessageW(::GetDlgItem(dlg, IDC_D197_TECH_LIST), LB_SETCURSEL, 0, 0);
    }
    if (upSel < 0 || upSel >= up.itemCount || techSel < 0 || techSel >= tech.itemCount) return;

    g_loading = true;
    if (g_player < 0) {
        ::SetDlgItemInt(dlg, IDC_D197_UP_MAX, up.GlobalPrimary(upSel), FALSE);
        ::SetDlgItemInt(dlg, IDC_D197_UP_START, up.GlobalSecondary(upSel), FALSE);
        ::CheckDlgButton(dlg, IDC_D197_UP_DEFAULTS, BST_UNCHECKED);
        ::EnableWindow(::GetDlgItem(dlg, IDC_D197_UP_DEFAULTS), FALSE);

        ::CheckDlgButton(dlg, IDC_D197_TECH_AVAILABLE,
                         tech.GlobalPrimary(techSel) ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_D197_TECH_START,
                         tech.GlobalSecondary(techSel) ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_D197_TECH_DEFAULTS, BST_UNCHECKED);
        ::EnableWindow(::GetDlgItem(dlg, IDC_D197_TECH_DEFAULTS), FALSE);
    } else {
        ::SetDlgItemInt(dlg, IDC_D197_UP_MAX, up.Primary(g_player, upSel), FALSE);
        ::SetDlgItemInt(dlg, IDC_D197_UP_START, up.Secondary(g_player, upSel), FALSE);
        ::CheckDlgButton(dlg, IDC_D197_UP_DEFAULTS,
                         up.UseDefaults(g_player, upSel) ? BST_CHECKED : BST_UNCHECKED);
        ::EnableWindow(::GetDlgItem(dlg, IDC_D197_UP_DEFAULTS), TRUE);

        ::CheckDlgButton(dlg, IDC_D197_TECH_AVAILABLE,
                         tech.Primary(g_player, techSel) ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_D197_TECH_START,
                         tech.Secondary(g_player, techSel) ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_D197_TECH_DEFAULTS,
                         tech.UseDefaults(g_player, techSel) ? BST_CHECKED : BST_UNCHECKED);
        ::EnableWindow(::GetDlgItem(dlg, IDC_D197_TECH_DEFAULTS), TRUE);
    }
    g_loading = false;
}

void CommitUpgrade(HWND dlg) {
    RestrictionTable& table = Table(false);
    int sel = SelectedIndex(dlg, IDC_D197_UPGRADE_LIST);
    if (sel < 0 && table.itemCount > 0) {
        sel = 0;
        ::SendMessageW(::GetDlgItem(dlg, IDC_D197_UPGRADE_LIST), LB_SETCURSEL, 0, 0);
    }
    if (g_loading) return;
    if (sel < 0 || sel >= table.itemCount) return;

    const u8 maxLevel = static_cast<u8>(ReadClampedInt(dlg, IDC_D197_UP_MAX, 0, 0, 255));
    const u8 startLevel = static_cast<u8>(ReadClampedInt(dlg, IDC_D197_UP_START, 0, 0, 255));
    const u8 useDefaults =
        ::IsDlgButtonChecked(dlg, IDC_D197_UP_DEFAULTS) == BST_CHECKED ? 1 : 0;

    if (g_player < 0) {
        table.SetGlobalPrimary(sel, maxLevel);
        table.SetGlobalSecondary(sel, startLevel);
    } else {
        table.SetPrimary(g_player, sel, maxLevel);
        table.SetSecondary(g_player, sel, startLevel);
        table.SetUseDefaults(g_player, sel, useDefaults);
    }
}

void CommitTech(HWND dlg) {
    if (g_loading) return;
    RestrictionTable& table = Table(true);
    int sel = SelectedIndex(dlg, IDC_D197_TECH_LIST);
    if (sel < 0 && table.itemCount > 0) {
        sel = 0;
        ::SendMessageW(::GetDlgItem(dlg, IDC_D197_TECH_LIST), LB_SETCURSEL, 0, 0);
    }
    if (sel < 0 || sel >= table.itemCount) return;

    const u8 available =
        ::IsDlgButtonChecked(dlg, IDC_D197_TECH_AVAILABLE) == BST_CHECKED ? 1 : 0;
    const u8 start =
        ::IsDlgButtonChecked(dlg, IDC_D197_TECH_START) == BST_CHECKED ? 1 : 0;
    const u8 useDefaults =
        ::IsDlgButtonChecked(dlg, IDC_D197_TECH_DEFAULTS) == BST_CHECKED ? 1 : 0;

    if (g_player < 0) {
        table.SetGlobalPrimary(sel, available);
        table.SetGlobalSecondary(sel, start);
    } else {
        table.SetPrimary(g_player, sel, available);
        table.SetSecondary(g_player, sel, start);
        table.SetUseDefaults(g_player, sel, useDefaults);
    }
}

}  // namespace

void SeedRestrictions(const UpgradeTechRestrictions& restrictions) {
    g_restrictionSeed = restrictions;
    g_versionBW = restrictions.HasBroodWar();
    g_player = -1;
}

const UpgradeTechRestrictions& LastRestrictions() {
    return g_restrictions;
}

// 197：升级 / 科技限制（UPGR / PTEC / PUPx / PTEx）
INT_PTR CALLBACK ProcRestrictions(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            g_loading = true;
            g_restrictions = g_restrictionSeed;

            ::SendDlgItemMessageW(dlg, IDC_D197_VERSION, CB_ADDSTRING, 0,
                                  reinterpret_cast<LPARAM>(L"原版 (UPGR / PTEC)"));
            ::SendDlgItemMessageW(dlg, IDC_D197_VERSION, CB_ADDSTRING, 0,
                                  reinterpret_cast<LPARAM>(L"资料片 (PUPx / PTEx)"));
            ::SendDlgItemMessageW(dlg, IDC_D197_VERSION, CB_SETCURSEL,
                                  g_versionBW ? 1 : 0, 0);

            ::SendDlgItemMessageW(dlg, IDC_D197_PLAYER, CB_ADDSTRING, 0,
                                  reinterpret_cast<LPARAM>(L"全局默认"));
            for (int i = 0; i < RestrictionTable::kPlayers; i++) {
                wchar_t label[32];
                ::wsprintfW(label, L"Player %d", i + 1);
                ::SendDlgItemMessageW(dlg, IDC_D197_PLAYER, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(label));
            }
            ::SendDlgItemMessageW(dlg, IDC_D197_PLAYER, CB_SETCURSEL, 0, 0);
            g_player = -1;
            g_loading = false;

            RefreshLists(dlg);
            LoadSelection(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (!g_loading && id == IDC_D197_VERSION && code == CBN_SELCHANGE) {
                CommitUpgrade(dlg);
                CommitTech(dlg);
                g_versionBW = ::SendDlgItemMessageW(dlg, IDC_D197_VERSION, CB_GETCURSEL, 0, 0) == 1;
                RefreshLists(dlg);
                LoadSelection(dlg);
                return TRUE;
            }
            if (!g_loading && id == IDC_D197_PLAYER && code == CBN_SELCHANGE) {
                CommitUpgrade(dlg);
                CommitTech(dlg);
                const int sel = static_cast<int>(::SendDlgItemMessageW(
                    dlg, IDC_D197_PLAYER, CB_GETCURSEL, 0, 0));
                g_player = sel <= 0 ? -1 : sel - 1;
                LoadSelection(dlg);
                return TRUE;
            }
            if (!g_loading && (id == IDC_D197_UPGRADE_LIST || id == IDC_D197_TECH_LIST) &&
                code == LBN_SELCHANGE) {
                CommitUpgrade(dlg);
                CommitTech(dlg);
                LoadSelection(dlg);
                return TRUE;
            }
            if (!g_loading && (id == IDC_D197_UP_MAX || id == IDC_D197_UP_START) &&
                code == EN_CHANGE) {
                CommitUpgrade(dlg);
                return TRUE;
            }
            if (!g_loading && id == IDC_D197_UP_DEFAULTS && code == BN_CLICKED) {
                CommitUpgrade(dlg);
                return TRUE;
            }
            if (!g_loading && (id == IDC_D197_TECH_AVAILABLE ||
                               id == IDC_D197_TECH_START ||
                               id == IDC_D197_TECH_DEFAULTS) &&
                code == BN_CLICKED) {
                CommitTech(dlg);
                return TRUE;
            }
            if (id == IDC_D197_1) {
                CommitUpgrade(dlg);
                CommitTech(dlg);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDC_D197_2 || id == IDCANCEL) {
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
