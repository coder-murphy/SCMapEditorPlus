#include <shlobj.h>

#include <utility>
#include <vector>

#include "ui/dlg_procs.h"

#include "app/app_settings.h"
#include "common/log.h"
#include "platform/file_dialog.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"
#include "ui/ui_bind.h"

namespace scmd {

// 103：关于框。只挂关闭行为，其余为静态文本。
INT_PTR CALLBACK ProcAbout(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D103_1 || LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                ::EndDialog(dlg, IDOK);
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

// 155：基本设置（默认资源 / 撤销次数 / MPQ 列表 / 自定义刷子文件 / 星际目录），
// 持久化到 settings.ini。字段表统一承担加载/提交/重置，事件用 EventBinder 注册。
namespace {

void FillList(HWND dlg, int listId, const std::vector<std::wstring>& files) {
    HWND list = ::GetDlgItem(dlg, listId);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& path : files) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.c_str()));
    }
}

void CollectList(HWND dlg, int listId, std::vector<std::wstring>& out) {
    out.clear();
    HWND list = ::GetDlgItem(dlg, listId);
    const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; i++) {
        const int len = static_cast<int>(::SendMessageW(list, LB_GETTEXTLEN, i, 0));
        if (len <= 0) continue;
        std::wstring text(static_cast<std::size_t>(len), L'\0');
        ::SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        out.push_back(text);
    }
}

struct SettingsDialogState {
    EventBinder events;
    FormSpec form;
};

FormSpec BuildSettingsForm() {
    AppSettings& s = AppSettings::Instance();
    FormSpec form;
    // 数值字段：当前值 / 范围 / 重置默认
    form.AddInt(IDC_D155_1010, s.defaultMineral(), 0, 100000, 8,
                [&s](int value) { s.SetDefaultMineral(value); });
    form.AddInt(IDC_D155_1011, s.defaultGas(), 0, 100000, 1,
                [&s](int value) { s.SetDefaultGas(value); });
    form.AddInt(IDC_D155_1012, s.undoLimit(), 1, 500, 50,
                [&s](int value) { s.SetUndoLimit(value); });
    form.AddInt(IDC_D155_1079, s.backupCount(), 1, 99, 5,
                [&s](int value) { s.SetBackupCount(value); });
    form.AddInt(IDC_D155_1081, s.autoBackupMinutes(), 1, 120, 5,
                [&s](int value) { s.SetAutoBackupMinutes(value); });
    form.AddInt(IDC_D155_1094, s.maxUnits(), 0, 100000, 3400,
                [&s](int value) { s.SetMaxUnits(value); });
    form.AddInt(IDC_D155_1009, s.maxSpUnits(), 0, 100000, 1700,
                [&s](int value) { s.SetMaxSpUnits(value); });
    form.AddInt(IDC_D155_1013, s.maxLocations(), 0, 100000, 500,
                [&s](int value) { s.SetMaxLocations(value); });
    form.AddText(IDC_D155_1008, s.gameDir(),
                 [&s](std::wstring value) { s.SetGameDir(value); });
    form.AddCheck(IDC_D155_1082, s.backupEnabled(), true,
                  [&s](bool value) { s.SetBackupEnabled(value); });
    form.AddCheck(IDC_D155_1080, s.autoBackupEnabled(), false,
                  [&s](bool value) { s.SetAutoBackupEnabled(value); });
    form.AddCheck(IDC_D155_1005, s.startMaximized(), false,
                  [&s](bool value) { s.SetStartMaximized(value); });
    form.AddCheck(IDC_D155_1129, s.loadFitZoom(), true,
                  [&s](bool value) { s.SetLoadFitZoom(value); });
    form.AddCheck(IDC_D155_1140, s.singleFile(), true,
                  [&s](bool value) { s.SetSingleFile(value); });
    form.AddCheck(IDC_D155_1093, s.triggerBaseNames(), false,
                  [&s](bool value) { s.SetTriggerBaseNames(value); });
    return form;
}

// 提交字段并写入 settings.ini（“保存设置”与“应用设定”共用）
void ApplySettingsFromDialog(HWND dlg, const FormSpec& form) {
    AppSettings& s = AppSettings::Instance();
    form.Commit(dlg);
    std::vector<std::wstring> files;
    CollectList(dlg, IDC_D155_1000, files);
    s.SetMpqFiles(std::move(files));
    std::vector<std::wstring> brushes;
    CollectList(dlg, IDC_D155_1060, brushes);
    s.SetBrushFiles(std::move(brushes));
    const bool ok = s.Save();
    Log(L"[settings] save=%d undo=%d mineral=%d gas=%d mpq=%u brush=%u", ok ? 1 : 0,
        s.undoLimit(), s.defaultMineral(), s.defaultGas(),
        static_cast<unsigned>(s.mpqFiles().size()),
        static_cast<unsigned>(s.brushFiles().size()));
}

void RegisterSettingsEvents(SettingsDialogState& state) {
    state.events.On(IDC_D155_1, BN_CLICKED, [&state](HWND dlg) {
        ApplySettingsFromDialog(dlg, state.form);
        ::EndDialog(dlg, IDOK);
    });
    state.events.On(IDC_D155_1007, BN_CLICKED, [&state](HWND dlg) {
        ApplySettingsFromDialog(dlg, state.form);
    });
    state.events.On(IDC_D155_1014, BN_CLICKED, [&state](HWND dlg) {
        state.form.ResetDefaults(dlg);
        Log(L"[settings] reset to defaults");
    });
    state.events.On(IDC_D155_1004, BN_CLICKED, [](HWND dlg) {   // 添加 MPQ（1017）
        wchar_t path[MAX_PATH] = L"";
        ::GetDlgItemTextW(dlg, IDC_D155_1017, path, MAX_PATH);
        if (path[0]) {
            ListAddString(dlg, IDC_D155_1000, path);
            ::SetDlgItemTextW(dlg, IDC_D155_1017, L"");
        }
    });
    state.events.On(IDC_D155_1006, BN_CLICKED, [](HWND dlg) {   // 添加刷子文件（1115）
        wchar_t path[MAX_PATH] = L"";
        ::GetDlgItemTextW(dlg, IDC_D155_1115, path, MAX_PATH);
        if (path[0]) {
            ListAddString(dlg, IDC_D155_1060, path);
            ::SetDlgItemTextW(dlg, IDC_D155_1115, L"");
        }
    });
    state.events.On(IDC_D155_1016, BN_CLICKED, [](HWND dlg) {   // 浏览星际目录
        wchar_t dir[MAX_PATH] = L"";
        BROWSEINFOW bi = {};
        bi.hwndOwner = dlg;
        bi.pszDisplayName = dir;
        bi.lpszTitle = L"选择星际争霸安装目录";
        LPITEMIDLIST idl = ::SHBrowseForFolderW(&bi);
        if (idl) {
            if (::SHGetPathFromIDListW(idl, dir)) {
                ::SetDlgItemTextW(dlg, IDC_D155_1008, dir);
            }
            ::CoTaskMemFree(idl);
        }
    });
    state.events.On(IDC_D155_1018, BN_CLICKED, [](HWND dlg) {   // 浏览 MPQ 文件
        std::wstring path;
        if (OpenFileDialog(dlg, L"MPQ 文件 (*.mpq)\0*.mpq\0所有文件 (*.*)\0*.*\0\0", path)) {
            ::SetDlgItemTextW(dlg, IDC_D155_1017, path.c_str());
        }
    });
    state.events.On(IDC_D155_1019, BN_CLICKED, [](HWND dlg) {   // 浏览刷子文件
        std::wstring path;
        if (OpenFileDialog(dlg, L"SCMD 自定义刷子 (*.scb)\0*.scb\0所有文件 (*.*)\0*.*\0\0", path)) {
            ::SetDlgItemTextW(dlg, IDC_D155_1115, path.c_str());
        }
    });
    state.events.On(IDC_D155_1003, BN_CLICKED,
                    [](HWND dlg) { ListRemoveSelection(dlg, IDC_D155_1000); });
    state.events.On(IDC_D155_1001, BN_CLICKED,
                    [](HWND dlg) { ListMoveSelection(dlg, IDC_D155_1000, -1); });
    state.events.On(IDC_D155_1002, BN_CLICKED,
                    [](HWND dlg) { ListMoveSelection(dlg, IDC_D155_1000, 1); });
    state.events.On(IDC_D155_1116, BN_CLICKED,
                    [](HWND dlg) { ListRemoveSelection(dlg, IDC_D155_1060); });
    state.events.On(IDC_D155_1015, BN_CLICKED,
                    [](HWND dlg) { ListMoveSelection(dlg, IDC_D155_1060, -1); });
    state.events.On(IDC_D155_1020, BN_CLICKED,
                    [](HWND dlg) { ListMoveSelection(dlg, IDC_D155_1060, 1); });
    state.events.On(IDC_D155_2, BN_CLICKED,
                    [](HWND dlg) { ::EndDialog(dlg, IDCANCEL); });   // IDCANCEL 同为 2
}

}  // namespace

INT_PTR CALLBACK ProcSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            auto* state = new SettingsDialogState();
            ::SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            state->form = BuildSettingsForm();
            state->form.Load(dlg);
            AppSettings& s = AppSettings::Instance();
            FillList(dlg, IDC_D155_1000, s.mpqFiles());
            FillList(dlg, IDC_D155_1060, s.brushFiles());
            RegisterSettingsEvents(*state);
            Log(L"[settings] dialog opened");
            return TRUE;
        }
        case WM_COMMAND: {
            auto* state = reinterpret_cast<SettingsDialogState*>(
                ::GetWindowLongPtrW(dlg, DWLP_USER));
            if (state && state->events.Dispatch(dlg, wParam)) return TRUE;
            break;
        }
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        case WM_NCDESTROY: {
            auto* state = reinterpret_cast<SettingsDialogState*>(
                ::GetWindowLongPtrW(dlg, DWLP_USER));
            delete state;
            ::SetWindowLongPtrW(dlg, DWLP_USER, 0);
            break;
        }
        default:
            break;
    }
    return FALSE;
}

// 163：兼容性警告。打开地图后由主窗口检测并填入问题列表；
// 「保存」= 自动修复可修复项（主窗口执行），「关闭」= 忽略。
namespace {
std::vector<std::wstring> g_compatIssues;
}

void SeedCompatIssues(const std::vector<std::wstring>& issues) { g_compatIssues = issues; }

INT_PTR CALLBACK ProcCompatWarning(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            HWND list = ::GetDlgItem(dlg, IDC_D163_1038);
            if (list) {
                ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
                for (const std::wstring& issue : g_compatIssues) {
                    ::SendMessageW(list, LB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(issue.c_str()));
                }
            }
            Log(L"[compat] warning shown, issues=%u",
                static_cast<unsigned>(g_compatIssues.size()));
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D163_1) {   // 保存：返回 IDOK 由主窗口执行修复
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D163_2 || LOWORD(wParam) == IDCANCEL) {   // 关闭
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
