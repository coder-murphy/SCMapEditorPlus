#include "ui/dlg_procs.h"

#include <string>

#include "res/control_ids.h"
#include "ui/dlg_common.h"

namespace scmd {

namespace {
std::wstring g_lastString;

// 1121 行号列：随正文高度实时重建（0x02 与换行都算游戏内换行）
void UpdateLineNumbers(HWND dlg) {
    wchar_t text[8192] = L"";
    ::GetDlgItemTextW(dlg, IDC_D189_1120, text, 8191);
    std::wstring numbers;
    int line = 1;
    wchar_t buf[16];
    ::wsprintfW(buf, L"%d", line);
    numbers = buf;
    for (const wchar_t* p = text; *p; ++p) {
        if (*p == L'\n' || *p == L'\x02') {
            line++;
            ::wsprintfW(buf, L"%d", line);
            numbers += L"\r\n";
            numbers += buf;
        }
    }
    ::SetDlgItemTextW(dlg, IDC_D189_1121, numbers.c_str());
}

// 1079 预览：把不可见控制字符转成可读标记——
// 0x02 是游戏内换行，0x07+色号是颜色切换，其余控制字符显示为 <0xNN>。
std::wstring BuildPreview(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\x02') {
            out += L"\r\n";
        } else if (ch == L'\x07') {
            wchar_t buf[24];
            if (i + 1 < text.size()) {
                ::wsprintfW(buf, L"[颜色%u]", static_cast<unsigned>(text[i + 1]));
                ++i;
            } else {
                ::wsprintfW(buf, L"[颜色?]");
            }
            out += buf;
        } else if (ch < 0x20 && ch != L'\r' && ch != L'\n' && ch != L'\t') {
            wchar_t buf[16];
            ::wsprintfW(buf, L"<0x%02X>", static_cast<unsigned>(ch));
            out += buf;
        } else {
            out += ch;
        }
    }
    return out;
}

void UpdatePreview(HWND dlg) {
    wchar_t text[8192] = L"";
    ::GetDlgItemTextW(dlg, IDC_D189_1120, text, 8191);
    ::SetDlgItemTextW(dlg, IDC_D189_1079, BuildPreview(text).c_str());
}
}  // namespace

const wchar_t* LastEditedString() { return g_lastString.c_str(); }

void SeedEditedString(const std::wstring& text) { g_lastString = text; }

// 189：编辑字符串。主编辑框 1120，右侧为预览（1079，控制字符转可读标记），
// 左侧行号（1121），“一直显示”（1122）控制行号列显隐。
INT_PTR CALLBACK ProcStringEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_D189_1120, g_lastString.c_str());
            ::CheckDlgButton(dlg, IDC_D189_1122, BST_CHECKED);
            ::ShowWindow(::GetDlgItem(dlg, IDC_D189_1121), SW_SHOW);
            UpdateLineNumbers(dlg);
            UpdatePreview(dlg);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D189_1120 && HIWORD(wParam) == EN_CHANGE) {
                UpdateLineNumbers(dlg);
                UpdatePreview(dlg);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D189_1122) {           // 一直显示：切换行号列
                const bool show =
                    ::IsDlgButtonChecked(dlg, IDC_D189_1122) == BST_CHECKED;
                ::ShowWindow(::GetDlgItem(dlg, IDC_D189_1121),
                             show ? SW_SHOW : SW_HIDE);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D189_1) {              // 确定
                wchar_t buf[4096] = L"";
                ::GetDlgItemTextW(dlg, IDC_D189_1120, buf, 4095);
                g_lastString = buf;
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D189_2 || LOWORD(wParam) == IDCANCEL) {   // 关闭
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
