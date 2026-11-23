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
// 持久化到 settings.ini
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

void FillBrushFileList(HWND dlg, const std::vector<std::wstring>& files) {
    HWND list = ::GetDlgItem(dlg, IDC_D155_1060);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& path : files) {
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.c_str()));
    }
}

void CollectBrushFileList(HWND dlg, std::vector<std::wstring>& out) {
    out.clear();
    HWND list = ::GetDlgItem(dlg, IDC_D155_1060);
    const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; i++) {
        const int len = static_cast<int>(::SendMessageW(list, LB_GETTEXTLEN, i, 0));
        if (len <= 0) continue;
        std::wstring text(static_cast<std::size_t>(len), L'\0');
        ::SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        out.push_back(text);
    }
}

// 从对话框读取全部设置并写入 settings.ini（“保存设置”与“应用设定”共用）
void ApplySettingsFromDialog(HWND dlg) {
    AppSettings& s = AppSettings::Instance();
    s.SetUndoLimit(ReadClampedInt(dlg, IDC_D155_1012, s.undoLimit(), 1, 500));
    s.SetDefaultMineral(ReadClampedInt(dlg, IDC_D155_1010, s.defaultMineral(), 0, 100000));
    s.SetDefaultGas(ReadClampedInt(dlg, IDC_D155_1011, s.defaultGas(), 0, 100000));
    wchar_t dir[MAX_PATH] = L"";
    ::GetDlgItemTextW(dlg, IDC_D155_1008, dir, MAX_PATH);
    s.SetGameDir(dir);
    s.SetBackupEnabled(::IsDlgButtonChecked(dlg, IDC_D155_1082) == BST_CHECKED);
    s.SetBackupCount(ReadClampedInt(dlg, IDC_D155_1079, s.backupCount(), 1, 99));
    s.SetAutoBackupEnabled(::IsDlgButtonChecked(dlg, IDC_D155_1080) == BST_CHECKED);
    s.SetAutoBackupMinutes(ReadClampedInt(dlg, IDC_D155_1081, s.autoBackupMinutes(), 1, 120));
    s.SetStartMaximized(::IsDlgButtonChecked(dlg, IDC_D155_1005) == BST_CHECKED);
    s.SetLoadFitZoom(::IsDlgButtonChecked(dlg, IDC_D155_1129) == BST_CHECKED);
    s.SetSingleFile(::IsDlgButtonChecked(dlg, IDC_D155_1140) == BST_CHECKED);
    s.SetTriggerBaseNames(::IsDlgButtonChecked(dlg, IDC_D155_1093) == BST_CHECKED);
    s.SetMaxUnits(ReadClampedInt(dlg, IDC_D155_1094, s.maxUnits(), 0, 100000));
    s.SetMaxSpUnits(ReadClampedInt(dlg, IDC_D155_1009, s.maxSpUnits(), 0, 100000));
    s.SetMaxLocations(ReadClampedInt(dlg, IDC_D155_1013, s.maxLocations(), 0, 100000));
    std::vector<std::wstring> files;
    CollectMpqList(dlg, files);
    s.SetMpqFiles(std::move(files));
    std::vector<std::wstring> brushes;
    CollectBrushFileList(dlg, brushes);
    s.SetBrushFiles(std::move(brushes));
    const bool ok = s.Save();
    Log(L"[settings] save=%d undo=%d mineral=%d gas=%d mpq=%u brush=%u", ok ? 1 : 0,
        s.undoLimit(), s.defaultMineral(), s.defaultGas(),
        static_cast<unsigned>(s.mpqFiles().size()),
        static_cast<unsigned>(s.brushFiles().size()));
}

}  // namespace

INT_PTR CALLBACK ProcSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            AppSettings& s = AppSettings::Instance();
            // 控件与标签对齐：默认水晶 1010 / 默认气矿 1011 / 撤销次数 1012
            ::SetDlgItemInt(dlg, IDC_D155_1010, static_cast<UINT>(s.defaultMineral()), FALSE);
            ::SetDlgItemInt(dlg, IDC_D155_1011, static_cast<UINT>(s.defaultGas()), FALSE);
            ::SetDlgItemInt(dlg, IDC_D155_1012, static_cast<UINT>(s.undoLimit()), FALSE);
            ::SetDlgItemTextW(dlg, IDC_D155_1008, s.gameDir().c_str());
            ::SetDlgItemInt(dlg, IDC_D155_1079, static_cast<UINT>(s.backupCount()), FALSE);
            ::CheckDlgButton(dlg, IDC_D155_1082,
                             s.backupEnabled() ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D155_1080,
                             s.autoBackupEnabled() ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(dlg, IDC_D155_1081, static_cast<UINT>(s.autoBackupMinutes()), FALSE);
            ::CheckDlgButton(dlg, IDC_D155_1005,
                             s.startMaximized() ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D155_1129,
                             s.loadFitZoom() ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D155_1140,
                             s.singleFile() ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D155_1093,
                             s.triggerBaseNames() ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(dlg, IDC_D155_1094, static_cast<UINT>(s.maxUnits()), FALSE);
            ::SetDlgItemInt(dlg, IDC_D155_1009, static_cast<UINT>(s.maxSpUnits()), FALSE);
            ::SetDlgItemInt(dlg, IDC_D155_1013, static_cast<UINT>(s.maxLocations()), FALSE);
            FillMpqList(dlg, s.mpqFiles());
            FillBrushFileList(dlg, s.brushFiles());
            Log(L"[settings] dialog opened");
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D155_1:      // 保存设置
                    ApplySettingsFromDialog(dlg);
                    ::EndDialog(dlg, IDOK);
                    return TRUE;
                case IDC_D155_1007:   // 应用设定：写入配置但不关闭
                    ApplySettingsFromDialog(dlg);
                    return TRUE;
                case IDC_D155_1014:   // 重置默认
                    ::SetDlgItemInt(dlg, IDC_D155_1010, 8, FALSE);    // 默认水晶
                    ::SetDlgItemInt(dlg, IDC_D155_1011, 1, FALSE);    // 默认气矿
                    ::SetDlgItemInt(dlg, IDC_D155_1012, 50, FALSE);   // 撤销次数
                    ::SetDlgItemInt(dlg, IDC_D155_1079, 5, FALSE);    // 最大备份数量
                    ::CheckDlgButton(dlg, IDC_D155_1082, BST_CHECKED);
                    ::CheckDlgButton(dlg, IDC_D155_1080, BST_UNCHECKED);
                    ::SetDlgItemInt(dlg, IDC_D155_1081, 5, FALSE);    // 自动备份间隔
                    ::CheckDlgButton(dlg, IDC_D155_1005, BST_UNCHECKED);  // 启动最大值
                    ::CheckDlgButton(dlg, IDC_D155_1129, BST_CHECKED);    // 地图载入最大值
                    ::CheckDlgButton(dlg, IDC_D155_1140, BST_CHECKED);    // 单一文件
                    ::CheckDlgButton(dlg, IDC_D155_1093, BST_UNCHECKED);  // 触发使用基本名称
                    ::SetDlgItemInt(dlg, IDC_D155_1094, 3400, FALSE); // 最多单位
                    ::SetDlgItemInt(dlg, IDC_D155_1009, 1700, FALSE); // SP单位
                    ::SetDlgItemInt(dlg, IDC_D155_1013, 500, FALSE);  // Location编号
                    Log(L"[settings] reset to defaults");
                    return TRUE;
                case IDC_D155_1004:   // 添加 MPQ（来自编辑框 1017）
                    {
                        wchar_t path[MAX_PATH] = L"";
                        ::GetDlgItemTextW(dlg, IDC_D155_1017, path, MAX_PATH);
                        if (path[0]) {
                            ListAddString(dlg, IDC_D155_1000, path);
                            ::SetDlgItemTextW(dlg, IDC_D155_1017, L"");
                        }
                    }
                    return TRUE;
                case IDC_D155_1016: {  // 浏览星际目录
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
                    return TRUE;
                }
                case IDC_D155_1018: {  // 浏览 MPQ 文件
                    std::wstring path;
                    if (OpenFileDialog(dlg, L"MPQ 文件 (*.mpq)\0*.mpq\0所有文件 (*.*)\0*.*\0\0",
                                       path)) {
                        ::SetDlgItemTextW(dlg, IDC_D155_1017, path.c_str());
                    }
                    return TRUE;
                }
                case IDC_D155_1006:   // 添加自定义刷子文件（编辑框 1115）
                    {
                        wchar_t path[MAX_PATH] = L"";
                        ::GetDlgItemTextW(dlg, IDC_D155_1115, path, MAX_PATH);
                        if (path[0]) {
                            ListAddString(dlg, IDC_D155_1060, path);
                            ::SetDlgItemTextW(dlg, IDC_D155_1115, L"");
                        }
                    }
                    return TRUE;
                case IDC_D155_1019: {  // 浏览刷子文件
                    std::wstring path;
                    if (OpenFileDialog(dlg, L"SCMD 自定义刷子 (*.scb)\0*.scb\0所有文件 (*.*)\0*.*\0\0",
                                       path)) {
                        ::SetDlgItemTextW(dlg, IDC_D155_1115, path.c_str());
                    }
                    return TRUE;
                }
                case IDC_D155_1116:   // 删除自定义刷子文件
                    ListRemoveSelection(dlg, IDC_D155_1060);
                    return TRUE;
                case IDC_D155_1015:   // 刷子列表上移
                    ListMoveSelection(dlg, IDC_D155_1060, -1);
                    return TRUE;
                case IDC_D155_1020:   // 刷子列表下移
                    ListMoveSelection(dlg, IDC_D155_1060, 1);
                    return TRUE;
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
