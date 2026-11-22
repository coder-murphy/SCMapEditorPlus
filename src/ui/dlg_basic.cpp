#include <utility>
#include <vector>

#include "ui/dlg_procs.h"

#include "app/app_settings.h"
#include "common/log.h"
#include "platform/file_dialog.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"

namespace scmd {

// 103 / 167：关于框。只挂关闭行为，其余为静态文本。
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

// 155：基本设置（默认资源 / 撤销次数 / MPQ 列表 / 星际目录），持久化到 settings.ini
namespace {

void FillMpqList(HWND dlg, const std::vector<std::wstring>& files) {
    HWND list = ::GetDlgItem(dlg, IDC_D155_1000);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& path : files) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.c_str()));
    }
}

void CollectMpqList(HWND dlg, std::vector<std::wstring>& out) {
    out.clear();
    HWND list = ::GetDlgItem(dlg, IDC_D155_1000);
    const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; i++) {
        const int len = static_cast<int>(::SendMessageW(list, LB_GETTEXTLEN, i, 0));
        if (len <= 0) continue;
        std::wstring text(static_cast<std::size_t>(len), L'\0');
        ::SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        out.push_back(text);
    }
}

}  // namespace

INT_PTR CALLBACK ProcSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            AppSettings& s = AppSettings::Instance();
            ::SetDlgItemInt(dlg, IDC_D155_1009, static_cast<UINT>(s.defaultMineral()), FALSE);
            ::SetDlgItemInt(dlg, IDC_D155_1010, static_cast<UINT>(s.defaultGas()), FALSE);
            ::SetDlgItemInt(dlg, IDC_D155_1013, static_cast<UINT>(s.undoLimit()), FALSE);
            ::SetDlgItemTextW(dlg, IDC_D155_1008, s.gameDir().c_str());
            ::SetDlgItemInt(dlg, IDC_D155_1079, static_cast<UINT>(s.backupCount()), FALSE);
            ::CheckDlgButton(dlg, IDC_D155_1082,
                             s.backupEnabled() ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D155_1080,
                             s.autoBackupEnabled() ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(dlg, IDC_D155_1081, static_cast<UINT>(s.autoBackupMinutes()), FALSE);
            FillMpqList(dlg, s.mpqFiles());
            Log(L"[settings] dialog opened");
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D155_1: {    // 保存设置
                    AppSettings& s = AppSettings::Instance();
                    s.SetUndoLimit(ReadClampedInt(dlg, IDC_D155_1013, s.undoLimit(), 1, 500));
                    s.SetDefaultMineral(
                        ReadClampedInt(dlg, IDC_D155_1009, s.defaultMineral(), 0, 100000));
                    s.SetDefaultGas(
                        ReadClampedInt(dlg, IDC_D155_1010, s.defaultGas(), 0, 100000));
                    wchar_t dir[MAX_PATH] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D155_1008, dir, MAX_PATH);
                    s.SetGameDir(dir);
                    s.SetBackupEnabled(::IsDlgButtonChecked(dlg, IDC_D155_1082) == BST_CHECKED);
                    s.SetBackupCount(ReadClampedInt(dlg, IDC_D155_1079, s.backupCount(), 1, 99));
                    s.SetAutoBackupEnabled(::IsDlgButtonChecked(dlg, IDC_D155_1080) == BST_CHECKED);
                    s.SetAutoBackupMinutes(
                        ReadClampedInt(dlg, IDC_D155_1081, s.autoBackupMinutes(), 1, 120));
                    std::vector<std::wstring> files;
                    CollectMpqList(dlg, files);
                    s.SetMpqFiles(std::move(files));
                    const bool ok = s.Save();
                    Log(L"[settings] save=%d undo=%d mineral=%d gas=%d mpq=%u", ok ? 1 : 0,
                        s.undoLimit(), s.defaultMineral(), s.defaultGas(),
                        static_cast<unsigned>(s.mpqFiles().size()));
                    ::EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDC_D155_1014:   // 重置默认
                    ::SetDlgItemInt(dlg, IDC_D155_1009, 8, FALSE);    // 默认水晶
                    ::SetDlgItemInt(dlg, IDC_D155_1010, 1, FALSE);    // 默认气矿
                    ::SetDlgItemInt(dlg, IDC_D155_1013, 50, FALSE);   // 撤销次数
                    ::SetDlgItemInt(dlg, IDC_D155_1079, 5, FALSE);    // 最大备份数量
                    ::CheckDlgButton(dlg, IDC_D155_1082, BST_CHECKED);
                    ::CheckDlgButton(dlg, IDC_D155_1080, BST_UNCHECKED);
                    ::SetDlgItemInt(dlg, IDC_D155_1081, 5, FALSE);    // 自动备份间隔
                    Log(L"[settings] reset to defaults");
                    return TRUE;
                case IDC_D155_1004: {  // 添加 MPQ
                    std::wstring path;
                    if (OpenFileDialog(dlg, L"MPQ 文件 (*.mpq)\0*.mpq\0所有文件 (*.*)\0*.*\0\0",
                                       path)) {
                        ListAddString(dlg, IDC_D155_1000, path.c_str());
                    }
                    return TRUE;
                }
                case IDC_D155_1003:   // 删除 MPQ
                    ListRemoveSelection(dlg, IDC_D155_1000);
                    return TRUE;
                case IDC_D155_1001:   // 上移
                    ListMoveSelection(dlg, IDC_D155_1000, -1);
                    return TRUE;
                case IDC_D155_1002:   // 下移
                    ListMoveSelection(dlg, IDC_D155_1000, 1);
                    return TRUE;
                case IDC_D155_2:      // 关闭（IDCANCEL 同为 2，故不重复列出）
                    ::EndDialog(dlg, IDCANCEL);
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

// 163：兼容性警告。列表内容待接入版本检测。
INT_PTR CALLBACK ProcCompatWarning(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            Log(L"[compat] warning shown");
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D163_1) {   // 保存
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
