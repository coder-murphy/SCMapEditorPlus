#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#include "common/log.h"
#include "common/string_util.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {

std::wstring SoundPathOf(const MapDocument& doc, u32 stringId) {
    if (stringId == 0) return std::wstring();
    return Utf8ToWide(doc.strings().At(stringId));
}

void FillSoundList(HWND dlg, int select) {
    MapDocument* doc = DialogDocument();
    HWND list = ::GetDlgItem(dlg, IDC_D198_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (!doc) return;
    for (std::size_t i = 0; i < doc->sounds().Count(); i++) {
        const std::wstring path = SoundPathOf(*doc, doc->sounds().Get(i));
        wchar_t line[600];
        ::wsprintfW(line, L"#%u  %ls", static_cast<unsigned>(i),
                    path.empty() ? L"(空)" : path.c_str());
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    }
    if (select < 0) select = 0;
    if (select >= static_cast<int>(doc->sounds().Count())) {
        select = static_cast<int>(doc->sounds().Count()) - 1;
    }
    if (select >= 0) ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
}

void LoadSoundField(HWND dlg) {
    MapDocument* doc = DialogDocument();
    if (!doc) return;
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D198_LIST, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    const std::wstring path = SoundPathOf(*doc, doc->sounds().Get(static_cast<std::size_t>(sel)));
    ::SetDlgItemTextW(dlg, IDC_D198_PATH, path.c_str());
}

// 198：编辑声音（WAV = u32[512] 的 STR 下标）
INT_PTR CALLBACK ProcSoundListImpl(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            FillSoundList(dlg, 0);
            LoadSoundField(dlg);
            return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_D198_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
                LoadSoundField(dlg);
                return TRUE;
            }
            if (id == IDC_D198_SET) {
                MapDocument* doc = DialogDocument();
                const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D198_LIST, LB_GETCURSEL, 0, 0);
                if (!doc || sel < 0) return TRUE;
                wchar_t path[512] = L"";
                ::GetDlgItemTextW(dlg, IDC_D198_PATH, path, 512);
                if (!path[0]) {
                    MessageBeep(MB_ICONWARNING);
                    return TRUE;
                }
                int sid = static_cast<int>(doc->sounds().Get(static_cast<std::size_t>(sel)));
                if (sid <= 0 || static_cast<std::size_t>(sid) >= doc->strings().Count()) {
                    sid = static_cast<int>(std::max<std::size_t>(1, doc->strings().Count()));
                }
                doc->mutableStrings().Set(static_cast<std::size_t>(sid), WideToUtf8(path));
                doc->mutableSounds().Set(static_cast<std::size_t>(sel), static_cast<u32>(sid));
                doc->MarkDirty();
                FillSoundList(dlg, static_cast<int>(sel));
                LoadSoundField(dlg);
                return TRUE;
            }
            if (id == IDC_D198_CLEAR) {
                MapDocument* doc = DialogDocument();
                const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D198_LIST, LB_GETCURSEL, 0, 0);
                if (!doc || sel < 0) return TRUE;
                if (doc->mutableSounds().Set(static_cast<std::size_t>(sel), 0)) doc->MarkDirty();
                FillSoundList(dlg, static_cast<int>(sel));
                ::SetDlgItemTextW(dlg, IDC_D198_PATH, L"");
                return TRUE;
            }
            if (id == IDC_D198_CLOSE || id == IDCANCEL) {
                ::EndDialog(dlg, IDOK);
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

struct BriefEntry {
    int block = 0;
    int action = 0;
    u8 type = 0;
};

std::vector<BriefEntry> g_briefEntries;
int g_briefSelected = 0;

const wchar_t* BriefActionName(u8 type) {
    switch (type) {
        case 3: return L"文本消息";
        case 4: return L"任务目标";
        case 8: return L"传输";
        default: return L"简报行动";
    }
}

void FillBriefList(HWND dlg, int select) {
    MapDocument* doc = DialogDocument();
    HWND list = ::GetDlgItem(dlg, IDC_D199_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    g_briefEntries.clear();
    if (!doc) return;
    auto& items = doc->mutableBriefing().MutableItems();
    for (int b = 0; b < static_cast<int>(items.size()); b++) {
        Trigger& t = items[static_cast<std::size_t>(b)];
        for (int a = 0; a < static_cast<int>(TriggerSection::kActionCount); a++) {
            const u8 type = t.actions[a].type;
            if (type == 0) break;
            if (type != 3 && type != 4 && type != 8) continue;
            g_briefEntries.push_back({b, a, type});
            std::wstring text = Utf8ToWide(doc->strings().At(t.actions[a].stringId));
            if (text.size() > 28) text = text.substr(0, 28) + L"…";
            wchar_t line[320];
            ::wsprintfW(line, L"P%u  %ls  %ls", static_cast<unsigned>(t.actions[a].player) + 1,
                        BriefActionName(type), text.empty() ? L"(空)" : text.c_str());
            ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
        }
    }
    if (g_briefEntries.empty()) {
        ::EnableWindow(::GetDlgItem(dlg, IDC_D199_UPDATE), FALSE);
        ::EnableWindow(::GetDlgItem(dlg, IDC_D199_TEXT), FALSE);
        ::SetDlgItemTextW(dlg, IDC_D199_TEXT, L"");
        return;
    }
    ::EnableWindow(::GetDlgItem(dlg, IDC_D199_UPDATE), TRUE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D199_TEXT), TRUE);
    if (select < 0) select = 0;
    if (select >= static_cast<int>(g_briefEntries.size())) {
        select = static_cast<int>(g_briefEntries.size()) - 1;
    }
    ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
}

BriefEntry* SelectedBriefEntry(MapDocument* doc) {
    if (!doc || g_briefEntries.empty()) return nullptr;
    if (g_briefSelected < 0 || static_cast<std::size_t>(g_briefSelected) >= g_briefEntries.size()) {
        return nullptr;
    }
    const BriefEntry& e = g_briefEntries[static_cast<std::size_t>(g_briefSelected)];
    auto& items = doc->mutableBriefing().MutableItems();
    if (e.block < 0 || static_cast<std::size_t>(e.block) >= items.size()) return nullptr;
    Trigger& t = items[static_cast<std::size_t>(e.block)];
    if (e.action < 0 || e.action >= static_cast<int>(TriggerSection::kActionCount)) return nullptr;
    return &g_briefEntries[static_cast<std::size_t>(g_briefSelected)];
}

void LoadBriefField(HWND dlg) {
    MapDocument* doc = DialogDocument();
    BriefEntry* e = SelectedBriefEntry(doc);
    if (!e) {
        ::SetDlgItemTextW(dlg, IDC_D199_TEXT, L"");
        return;
    }
    Trigger& t = doc->mutableBriefing().MutableItems()[static_cast<std::size_t>(e->block)];
    const std::wstring text = Utf8ToWide(doc->strings().At(t.actions[e->action].stringId));
    ::SetDlgItemTextW(dlg, IDC_D199_TEXT, text.c_str());
}

// 199：任务简报（编辑 MBRF 中已有的文本/目标/传输行动）
INT_PTR CALLBACK ProcMissionBriefingImpl(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            g_briefSelected = 0;
            FillBriefList(dlg, 0);
            LoadBriefField(dlg);
            return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_D199_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
                const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D199_LIST, LB_GETCURSEL, 0, 0);
                if (sel >= 0) g_briefSelected = static_cast<int>(sel);
                LoadBriefField(dlg);
                return TRUE;
            }
            if (id == IDC_D199_UPDATE) {
                MapDocument* doc = DialogDocument();
                BriefEntry* e = SelectedBriefEntry(doc);
                if (!doc || !e) return TRUE;
                Trigger& t = doc->mutableBriefing().MutableItems()[static_cast<std::size_t>(e->block)];
                wchar_t text[1024] = L"";
                ::GetDlgItemTextW(dlg, IDC_D199_TEXT, text, 1024);
                const std::string utf8 = WideToUtf8(text);
                int sid = static_cast<int>(t.actions[e->action].stringId);
                if (!utf8.empty()) {
                    if (sid <= 0 || static_cast<std::size_t>(sid) >= doc->strings().Count()) {
                        sid = static_cast<int>(std::max<std::size_t>(1, doc->strings().Count()));
                    }
                    doc->mutableStrings().Set(static_cast<std::size_t>(sid), utf8);
                    t.actions[e->action].stringId = static_cast<u32>(sid);
                    doc->MarkDirty();
                } else if (sid > 0 && static_cast<std::size_t>(sid) < doc->strings().Count()) {
                    doc->mutableStrings().Set(static_cast<std::size_t>(sid), std::string());
                    doc->MarkDirty();
                }
                const int keep = g_briefSelected;
                FillBriefList(dlg, keep);
                g_briefSelected = keep;
                LoadBriefField(dlg);
                return TRUE;
            }
            if (id == IDC_D199_CLOSE || id == IDCANCEL) {
                ::EndDialog(dlg, IDOK);
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

}  // namespace

INT_PTR CALLBACK ProcSoundList(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    return ProcSoundListImpl(dlg, msg, wParam, lParam);
}

INT_PTR CALLBACK ProcMissionBriefing(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    return ProcMissionBriefingImpl(dlg, msg, wParam, lParam);
}

}  // namespace scmd
