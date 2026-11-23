#include "ui/dlg_procs.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "app/backups.h"
#include "common/log.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

// 173：查看备份数据（模态，单实例）
namespace {

std::vector<BackupGroup> g_backupGroups;
std::vector<BackupEntry> g_backupEntries;

void RefreshBackupGroups(HWND dlg) {
    g_backupGroups = ListBackupGroups();
    HWND list = ::GetDlgItem(dlg, IDC_D173_1060);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const BackupGroup& g : g_backupGroups) {
        std::wstring label = g.name;
        if (!g.source.empty()) label += L"    (" + g.source + L")";
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    ::SendMessageW(::GetDlgItem(dlg, IDC_D173_1083), LB_RESETCONTENT, 0, 0);
    g_backupEntries.clear();
}

void RefreshBackups(HWND dlg) {
    const int sel = ListSelectedIndex(dlg, IDC_D173_1060);
    g_backupEntries.clear();
    HWND list = ::GetDlgItem(dlg, IDC_D173_1083);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (sel < 0 || sel >= static_cast<int>(g_backupGroups.size())) return;
    g_backupEntries = ListBackups(g_backupGroups[static_cast<std::size_t>(sel)].key);
    for (const BackupEntry& e : g_backupEntries) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.label.c_str()));
    }
}

}  // namespace

INT_PTR CALLBACK ProcBackupViewer(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            RefreshBackupGroups(dlg);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D173_1060:
                    if (HIWORD(wParam) == LBN_SELCHANGE) RefreshBackups(dlg);
                    return TRUE;
                case IDC_D173_1084: {                            // 恢复
                    const int groupSel = ListSelectedIndex(dlg, IDC_D173_1060);
                    const int entrySel = ListSelectedIndex(dlg, IDC_D173_1083);
                    if (groupSel < 0 || entrySel < 0 ||
                        entrySel >= static_cast<int>(g_backupEntries.size())) {
                        MessageBeep(MB_ICONWARNING);
                        return TRUE;
                    }
                    const BackupGroup& group = g_backupGroups[static_cast<std::size_t>(groupSel)];
                    const BackupEntry& entry = g_backupEntries[static_cast<std::size_t>(entrySel)];
                    if (::MessageBoxW(dlg, L"用选中的备份覆盖当前地图文件？\n（已打开的地图需重新打开后生效）",
                                      L"恢复备份", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return TRUE;
                    }
                    if (RestoreBackup(entry.file, group.source)) {
                        ::MessageBoxW(dlg, L"备份已恢复。", L"恢复备份", MB_ICONINFORMATION);
                        Log(L"[backup] restored %s -> %s", entry.file.c_str(), group.source.c_str());
                    } else {
                        ::MessageBoxW(dlg, L"恢复失败（目标路径不可写或备份已删除）。",
                                      L"恢复备份", MB_ICONWARNING);
                    }
                    return TRUE;
                }
                case IDC_D173_1085: {                            // 删除文档（单个备份）
                    const int entrySel = ListSelectedIndex(dlg, IDC_D173_1083);
                    if (entrySel < 0 || entrySel >= static_cast<int>(g_backupEntries.size())) {
                        MessageBeep(MB_ICONWARNING);
                        return TRUE;
                    }
                    DeleteBackupFile(g_backupEntries[static_cast<std::size_t>(entrySel)].file);
                    RefreshBackups(dlg);
                    return TRUE;
                }
                case IDC_D173_1059: {                            // 删除地图组（该地图全部备份）
                    const int sel = ListSelectedIndex(dlg, IDC_D173_1060);
                    if (sel < 0 || sel >= static_cast<int>(g_backupGroups.size())) {
                        MessageBeep(MB_ICONWARNING);
                        return TRUE;
                    }
                    if (::MessageBoxW(dlg, L"删除该地图的全部备份？", L"删除地图组",
                                      MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return TRUE;
                    }
                    DeleteBackupGroup(g_backupGroups[static_cast<std::size_t>(sel)].key);
                    RefreshBackupGroups(dlg);
                    return TRUE;
                }
                case IDC_D173_1086:                              // 清除备份
                    if (::MessageBoxW(dlg, L"删除所有地图备份？", L"清除备份",
                                      MB_YESNO | MB_ICONQUESTION) != IDYES) {
                        return TRUE;
                    }
                    ClearAllBackups();
                    RefreshBackupGroups(dlg);
                    Log(L"[backup] cleared all");
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

// 174：在线升级（下载逻辑待接入）
INT_PTR CALLBACK ProcUpdater(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SendDlgItemMessageW(dlg, IDC_D174_1087, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            ::SendDlgItemMessageW(dlg, IDC_D174_1087, PBM_SETPOS, 0, 0);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D174_1090) {              // 下载升级文件
                ::SetDlgItemTextW(dlg, IDC_D174_1091, L"升级服务尚未接入");
                Log(L"[update] download requested (TODO)");
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D174_1) {
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
