#include "ui/dlg_procs.h"

#include <string>

#include "common/log.h"
#include "common/string_util.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {

PlayerRoster g_playerSeed;
PlayerRoster g_playerRoster;
bool g_loading = false;

std::wstring ForceText(u16 stringId) {
    if (MapDocument* doc = DialogDocument()) {
        const std::string& text = doc->strings().At(stringId);
        if (!text.empty()) return Utf8ToWide(text);
    }
    wchar_t fallback[32];
    ::wsprintfW(fallback, L"#%u", static_cast<unsigned>(stringId));
    return fallback;
}

std::wstring PlayerLabel(int index) {
    const PlayerSlot& p = g_playerRoster.player(index);
    wchar_t text[256];
    if (index < PlayerRoster::kPlayers) {
        ::wsprintfW(text, L"P%d  %s / %s / F%d / %s",
                    index + 1, PlayerControllerName(p.controller), PlayerRaceName(p.race),
                    p.force + 1, PlayerColorName(p.color));
    } else {
        ::wsprintfW(text, L"槽 %d  %s / %s",
                    index + 1, PlayerControllerName(p.controller), PlayerRaceName(p.race));
    }
    return text;
}

std::wstring ForceLabel(int index) {
    const ForceSettings& f = g_playerRoster.force(index);
    wchar_t text[256];
    ::wsprintfW(text, L"Force %d  #%u  %s", index + 1,
                static_cast<unsigned>(f.nameStringId), ForceText(f.nameStringId).c_str());
    return text;
}

void RefreshPlayerList(HWND dlg, int select) {
    HWND list = ::GetDlgItem(dlg, IDC_D196_PLAYER_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < PlayerRoster::kPlayerSlots; i++) {
        const std::wstring label = PlayerLabel(i);
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (select < 0 || select >= PlayerRoster::kPlayerSlots) select = 0;
    ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
}

void RefreshForceList(HWND dlg, int select) {
    HWND list = ::GetDlgItem(dlg, IDC_D196_FORCE_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < PlayerRoster::kForces; i++) {
        const std::wstring label = ForceLabel(i);
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (select < 0 || select >= PlayerRoster::kForces) select = 0;
    ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
}

void LoadPlayer(HWND dlg, int index) {
    const PlayerSlot& p = g_playerRoster.player(index);
    g_loading = true;
    ::SendDlgItemMessageW(dlg, IDC_D196_CONTROLLER, CB_SETCURSEL, p.controller, 0);
    ::SendDlgItemMessageW(dlg, IDC_D196_RACE, CB_SETCURSEL, p.race, 0);
    ::SendDlgItemMessageW(dlg, IDC_D196_FORCE, CB_SETCURSEL, p.force, 0);
    ::SendDlgItemMessageW(dlg, IDC_D196_COLOR, CB_SETCURSEL, p.color, 0);
    const bool hasForce = index < PlayerRoster::kPlayers;
    ::EnableWindow(::GetDlgItem(dlg, IDC_D196_FORCE), hasForce);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D196_COLOR), hasForce);
    g_loading = false;
}

void CommitForceEdits(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_D196_FORCE_LIST);
    const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= PlayerRoster::kForces) return;
    const int nameId = ReadClampedInt(dlg, IDC_D196_FORCE_NAME, 0, 0, 65535);
    const int flags = ReadClampedInt(dlg, IDC_D196_FORCE_FLAGS, 0, 0, 255);
    g_playerRoster.SetForceNameId(sel, static_cast<u16>(nameId));
    g_playerRoster.SetForceFlags(sel, static_cast<u8>(flags));
}

void LoadForce(HWND dlg, int index) {
    const ForceSettings& f = g_playerRoster.force(index);
    g_loading = true;
    ::SetDlgItemInt(dlg, IDC_D196_FORCE_NAME, static_cast<UINT>(f.nameStringId), FALSE);
    ::SetDlgItemInt(dlg, IDC_D196_FORCE_FLAGS, static_cast<UINT>(f.flags), FALSE);
    ::SetDlgItemTextW(dlg, IDC_D196_FORCE_TEXT, ForceText(f.nameStringId).c_str());
    g_loading = false;
}

}  // namespace

void SeedPlayerRoster(const PlayerRoster& roster) {
    g_playerSeed = roster;
}

const PlayerRoster& LastPlayerRoster() {
    return g_playerRoster;
}

// 196：玩家 / 势力设定（OWNR / SIDE / FORC / COLR）
INT_PTR CALLBACK ProcPlayerRoster(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            g_loading = true;
            g_playerRoster = g_playerSeed;

            for (int i = 0; i < 9; i++) {
                ::SendDlgItemMessageW(dlg, IDC_D196_CONTROLLER, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(PlayerControllerName(
                                          static_cast<u8>(i))));
            }
            for (int i = 0; i < 8; i++) {
                ::SendDlgItemMessageW(dlg, IDC_D196_RACE, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(PlayerRaceName(
                                          static_cast<u8>(i))));
            }
            for (int i = 0; i < 16; i++) {
                ::SendDlgItemMessageW(dlg, IDC_D196_COLOR, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(PlayerColorName(
                                          static_cast<u8>(i))));
            }
            for (int i = 0; i < PlayerRoster::kForces; i++) {
                wchar_t label[32];
                ::wsprintfW(label, L"Force %d", i + 1);
                ::SendDlgItemMessageW(dlg, IDC_D196_FORCE, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(label));
            }
            g_loading = false;

            RefreshPlayerList(dlg, 0);
            RefreshForceList(dlg, 0);
            LoadPlayer(dlg, 0);
            LoadForce(dlg, 0);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);

            if (id == IDC_D196_PLAYER_LIST && code == LBN_SELCHANGE) {
                HWND list = ::GetDlgItem(dlg, IDC_D196_PLAYER_LIST);
                const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0) LoadPlayer(dlg, sel);
                return TRUE;
            }
            if (!g_loading && (id == IDC_D196_CONTROLLER || id == IDC_D196_RACE ||
                               id == IDC_D196_FORCE || id == IDC_D196_COLOR) &&
                code == CBN_SELCHANGE) {
                HWND list = ::GetDlgItem(dlg, IDC_D196_PLAYER_LIST);
                const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    const int value = static_cast<int>(::SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0));
                    if (id == IDC_D196_CONTROLLER) g_playerRoster.SetController(sel, static_cast<u8>(value));
                    else if (id == IDC_D196_RACE) g_playerRoster.SetRace(sel, static_cast<u8>(value));
                    else if (id == IDC_D196_FORCE) g_playerRoster.SetForce(sel, static_cast<u8>(value));
                    else if (id == IDC_D196_COLOR) g_playerRoster.SetColor(sel, static_cast<u8>(value));
                    RefreshPlayerList(dlg, sel);
                }
                return TRUE;
            }
            if (id == IDC_D196_FORCE_LIST && code == LBN_SELCHANGE) {
                HWND list = ::GetDlgItem(dlg, IDC_D196_FORCE_LIST);
                const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0) LoadForce(dlg, sel);
                return TRUE;
            }
            if ((id == IDC_D196_FORCE_NAME || id == IDC_D196_FORCE_FLAGS) &&
                (code == EN_KILLFOCUS || code == EN_CHANGE) && !g_loading) {
                CommitForceEdits(dlg);
                HWND list = ::GetDlgItem(dlg, IDC_D196_FORCE_LIST);
                const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (sel >= 0) {
                    RefreshForceList(dlg, sel);
                    ::SetDlgItemTextW(dlg, IDC_D196_FORCE_TEXT,
                                      ForceText(g_playerRoster.force(sel).nameStringId).c_str());
                }
                return TRUE;
            }
            if (id == IDC_D196_1) {                          // 确定
                CommitForceEdits(dlg);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDC_D196_2 || id == IDCANCEL) {
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
