#include "ui/dlg_procs.h"

#include <commdlg.h>
#include <cwchar>
#include <string>
#include <vector>

#include "app/app_settings.h"
#include "archive/mpq_archive.h"
#include "common/log.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "units/iscript.h"

namespace scmd {

namespace {

std::wstring WidenName(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = static_cast<int>(s.size());
    const int n = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), len, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), len, w.data(), n);
    return w;
}

std::string NarrowName(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int len = static_cast<int>(w.size());
    const int n = ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), len, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), len, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring BaseNameOf(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

bool ReadWholeFile(const std::wstring& path, Buffer& out) {
    out.clear();
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = false;
    if (::GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart <= (64LL << 20)) {
        out.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (out.empty()) {
            ok = true;
        } else if (::ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &read, nullptr)) {
            ok = read == out.size();
        }
        if (!ok) out.clear();
    }
    ::CloseHandle(f);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const Buffer& data) {
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = data.empty() ||
                    (::WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                     written == data.size());
    ::CloseHandle(f);
    return ok;
}

void RefreshMpqList(HWND dlg) {
    ::SendMessageW(::GetDlgItem(dlg, IDC_D167_1049), LB_RESETCONTENT, 0, 0);
    MapDocument* doc = DialogDocument();
    if (!doc || !doc->IsOpen()) {
        ::SetDlgItemTextW(dlg, IDC_D167_1055, L"未打开地图");
        return;
    }
    for (const MpqFileEntry& f : doc->extraFiles()) {
        ListAddString(dlg, IDC_D167_1049, WidenName(f.name).c_str());
    }
    wchar_t info[64];
    ::wsprintfW(info, L"共 %u 个文件", static_cast<unsigned>(doc->extraFiles().size()));
    ::SetDlgItemTextW(dlg, IDC_D167_1055, info);
}

void ShowMpqSelection(HWND dlg) {
    MapDocument* doc = DialogDocument();
    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
    if (!doc || !doc->IsOpen() || sel < 0 ||
        sel >= static_cast<int>(doc->extraFiles().size())) {
        return;
    }
    const MpqFileEntry& f = doc->extraFiles()[static_cast<std::size_t>(sel)];
    wchar_t info[320];
    ::wsprintfW(info, L"%u 字节  %ls", static_cast<unsigned>(f.data.size()),
                WidenName(f.name).c_str());
    ::SetDlgItemTextW(dlg, IDC_D167_1055, info);
    ::SetDlgItemTextW(dlg, IDC_D167_1056, WidenName(f.name).c_str());
}

// 让用户在本地文件对话框里选文件；返回空表示取消
std::wstring PickOpenFile(HWND dlg) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return ::GetOpenFileNameW(&ofn) ? std::wstring(path) : std::wstring();
}

std::wstring PickSaveFile(HWND dlg, const std::wstring& suggested) {
    wchar_t path[MAX_PATH] = L"";
    if (!suggested.empty() && suggested.size() < MAX_PATH) {
        ::lstrcpynW(path, suggested.c_str(), MAX_PATH);
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dlg;
    ofn.lpstrFilter = L"所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return ::GetSaveFileNameW(&ofn) ? std::wstring(path) : std::wstring();
}

}  // namespace

// 167：MPQ 文件管理（当前地图的附加文件：列出 / 导入 / 导出 / 改名 / 添加 / 删除）
INT_PTR CALLBACK ProcMpqList(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            RefreshMpqList(dlg);
            MapDocument* doc = DialogDocument();
            if (!doc || !doc->IsOpen()) {
                const int ids[] = {IDC_D167_1049, IDC_D167_1050, IDC_D167_1051,
                                   IDC_D167_1052, IDC_D167_1053, IDC_D167_1056,
                                   IDC_D167_1092};
                for (int id : ids) ::EnableWindow(::GetDlgItem(dlg, id), FALSE);
            }
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D167_1049:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        ShowMpqSelection(dlg);
                    }
                    return TRUE;
                case IDC_D167_1053: {                            // 添加（空文件）
                    MapDocument* doc = DialogDocument();
                    if (!doc || !doc->IsOpen()) return TRUE;
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 511);
                    if (!name[0]) {
                        ::MessageBoxW(dlg, L"请先在文件名框输入条目名称。", L"MPQ 文件管理",
                                      MB_ICONINFORMATION);
                        return TRUE;
                    }
                    if (!doc->SetExtraFile(NarrowName(name), Buffer())) {
                        ::MessageBoxW(dlg, L"该名称无效（不能覆盖 listfile 或 scenario.chk）。",
                                      L"MPQ 文件管理", MB_ICONWARNING);
                        return TRUE;
                    }
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1092: {                            // 删除
                    MapDocument* doc = DialogDocument();
                    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
                    if (!doc || !doc->IsOpen() || sel < 0 ||
                        sel >= static_cast<int>(doc->extraFiles().size())) {
                        return TRUE;
                    }
                    doc->RemoveExtraFile(doc->extraFiles()[static_cast<std::size_t>(sel)].name);
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1050: {                            // 导入本地文件
                    MapDocument* doc = DialogDocument();
                    if (!doc || !doc->IsOpen()) return TRUE;
                    const std::wstring path = PickOpenFile(dlg);
                    if (path.empty()) return TRUE;
                    Buffer data;
                    if (!ReadWholeFile(path, data)) {
                        ::MessageBoxW(dlg, L"读取本地文件失败。", L"MPQ 文件管理", MB_ICONERROR);
                        return TRUE;
                    }
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 511);
                    const std::string entry =
                        name[0] ? NarrowName(name) : NarrowName(BaseNameOf(path));
                    if (!doc->SetExtraFile(entry, std::move(data))) {
                        ::MessageBoxW(dlg, L"该名称无效（不能覆盖 listfile 或 scenario.chk）。",
                                      L"MPQ 文件管理", MB_ICONWARNING);
                        return TRUE;
                    }
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1051: {                            // 导出选中条目
                    MapDocument* doc = DialogDocument();
                    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
                    if (!doc || !doc->IsOpen() || sel < 0 ||
                        sel >= static_cast<int>(doc->extraFiles().size())) {
                        return TRUE;
                    }
                    const MpqFileEntry& f = doc->extraFiles()[static_cast<std::size_t>(sel)];
                    const std::wstring path = PickSaveFile(dlg, BaseNameOf(WidenName(f.name)));
                    if (path.empty()) return TRUE;
                    if (!WriteWholeFile(path, f.data)) {
                        ::MessageBoxW(dlg, L"写入本地文件失败。", L"MPQ 文件管理", MB_ICONERROR);
                    }
                    return TRUE;
                }
                case IDC_D167_1052: {                            // 编辑（改名）
                    MapDocument* doc = DialogDocument();
                    const int sel = ListSelectedIndex(dlg, IDC_D167_1049);
                    if (!doc || !doc->IsOpen() || sel < 0 ||
                        sel >= static_cast<int>(doc->extraFiles().size())) {
                        return TRUE;
                    }
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D167_1056, name, 511);
                    if (!name[0]) {
                        ::MessageBoxW(dlg, L"请先输入新的条目名称。", L"MPQ 文件管理",
                                      MB_ICONINFORMATION);
                        return TRUE;
                    }
                    const std::string oldName =
                        doc->extraFiles()[static_cast<std::size_t>(sel)].name;
                    const std::string newName = NarrowName(name);
                    if (_stricmp(oldName.c_str(), newName.c_str()) == 0) return TRUE;
                    Buffer existing;
                    if (doc->ReadExtraFile(newName, existing)) {
                        ::MessageBoxW(dlg, L"同名条目已存在。", L"MPQ 文件管理", MB_ICONWARNING);
                        return TRUE;
                    }
                    Buffer data;
                    doc->ReadExtraFile(oldName, data);
                    doc->RemoveExtraFile(oldName);
                    if (!doc->SetExtraFile(newName, std::move(data))) {
                        ::MessageBoxW(dlg, L"该名称无效（不能覆盖 listfile 或 scenario.chk）。",
                                      L"MPQ 文件管理", MB_ICONWARNING);
                    }
                    RefreshMpqList(dlg);
                    return TRUE;
                }
                case IDC_D167_1:
                case IDC_D167_1059:
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

// 205：Iscript / Grp 调试（解析 iscript.bin，展示动画操作码；GRP 仅校验头）
namespace {

IscriptFile g_iscript;
std::wstring g_iscriptLabel;

std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = L"";
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (slash) *slash = L'\0';
    return path;
}

// 依次尝试：星际目录 / exe 目录 / 工程根的 scripts\iscript.bin，再从 StarDat.mpq 读取
bool LoadIscriptAuto(std::wstring& label) {
    std::vector<std::wstring> dirs;
    const std::wstring gameDir = AppSettings::Instance().gameDir();
    if (!gameDir.empty()) dirs.push_back(gameDir);
    dirs.push_back(ExeDir());
    dirs.push_back(ExeDir() + L"\\..\\..\\..");

    for (const std::wstring& dir : dirs) {
        const std::wstring file = dir + L"\\scripts\\iscript.bin";
        if (::GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        if (g_iscript.Load(file)) {
            label = file;
            return true;
        }
    }
    for (const std::wstring& dir : dirs) {
        const std::wstring candidates[] = {dir + L"\\mpq\\StarDat.mpq", dir + L"\\StarDat.mpq"};
        for (const std::wstring& mpqPath : candidates) {
            if (::GetFileAttributesW(mpqPath.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            MpqArchive mpq;
            Buffer data;
            if (mpq.Open(mpqPath) && mpq.ReadFile("scripts\\iscript.bin", data) &&
                g_iscript.Parse(data)) {
                label = mpqPath + L"  (scripts\\iscript.bin)";
                return true;
            }
        }
    }
    return false;
}

void ShowIscriptAnimation(HWND dlg, const IscriptHeader* header) {
    HWND list = ::GetDlgItem(dlg, IDC_ISCRIPT_LIST);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    if (!header) return;
    HWND combo = ::GetDlgItem(dlg, IDC_ISCRIPT_ANIM);
    const int sel = static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return;
    const LRESULT data = ::SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
    if (data == CB_ERR) return;

    std::vector<IscriptInstruction> instructions;
    if (!g_iscript.Disassemble(*header, static_cast<int>(data), instructions)) return;
    for (const IscriptInstruction& ins : instructions) {
        wchar_t line[256];
        int used = ::wsprintfW(line, L"%04X  %-18hs", static_cast<unsigned>(ins.offset),
                               IscriptFile::OpcodeName(ins.opcode));
        for (u8 b : ins.operands) {
            if (used > 200) break;
            used += ::wsprintfW(line + used, L" %02X", b);
        }
        ListAddString(dlg, IDC_ISCRIPT_LIST, line);
    }
}

void RefreshIscriptDialog(HWND dlg) {
    HWND combo = ::GetDlgItem(dlg, IDC_ISCRIPT_ANIM);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    ::SendMessageW(::GetDlgItem(dlg, IDC_ISCRIPT_LIST), LB_RESETCONTENT, 0, 0);
    ::SetDlgItemTextW(dlg, IDC_ISCRIPT_PATH, g_iscriptLabel.c_str());
    if (!g_iscript.loaded()) {
        ::SetDlgItemTextW(dlg, IDC_ISCRIPT_SUMMARY,
                          L"未找到 iscript.bin。\r\n"
                          L"可用「载入...」选择文件，或在选项 → 155 中设置星际目录。\r\n");
        return;
    }
    const u16 id = static_cast<u16>(ReadClampedInt(dlg, IDC_ISCRIPT_ID, 198, 0, 65535));
    const IscriptHeader* header = g_iscript.Find(id);
    if (!header) {
        wchar_t msg[128];
        ::wsprintfW(msg, L"未找到 Iscript ID %u。\r\n", static_cast<unsigned>(id));
        ::SetDlgItemTextW(dlg, IDC_ISCRIPT_SUMMARY, msg);
        return;
    }

    int present = 0;
    for (int i = 0; i < static_cast<int>(header->animOffsets.size()); i++) {
        if (header->animOffsets[static_cast<std::size_t>(i)] == 0) continue;
        const std::wstring name = WidenName(IscriptFile::AnimationName(i));
        const LRESULT idx = ::SendMessageW(combo, CB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(name.c_str()));
        if (idx != CB_ERR) {
            ::SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(idx),
                           static_cast<LPARAM>(i));
            present++;
        }
    }
    if (present > 0) ::SendMessageW(combo, CB_SETCURSEL, 0, 0);

    wchar_t summary[640];
    ::wsprintfW(summary,
                L"文件: %ls\r\n大小: %u 字节\r\n条目: %u\r\n"
                L"ID: %u\r\n类型: %u\r\n动画槽: %u（可用 %d）\r\n",
                g_iscriptLabel.c_str(), static_cast<unsigned>(g_iscript.size()),
                static_cast<unsigned>(g_iscript.entries().size()),
                static_cast<unsigned>(id), static_cast<unsigned>(header->type),
                static_cast<unsigned>(header->animOffsets.size()), present);
    ::SetDlgItemTextW(dlg, IDC_ISCRIPT_SUMMARY, summary);
    ShowIscriptAnimation(dlg, header);
}

}  // namespace

// 205：Iscript / Grp 调试
INT_PTR CALLBACK ProcIscriptGrpDebug(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_ISCRIPT_ID, L"198");
            if (!LoadIscriptAuto(g_iscriptLabel)) g_iscriptLabel.clear();
            RefreshIscriptDialog(dlg);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_ISCRIPT_LOAD: {
                    const std::wstring path = PickOpenFile(dlg);
                    if (path.empty()) return TRUE;
                    if (!g_iscript.Load(path)) {
                        ::MessageBoxW(dlg, L"iscript.bin 解析失败。", L"Iscript / Grp 调试",
                                      MB_ICONERROR);
                        return TRUE;
                    }
                    g_iscriptLabel = path;
                    RefreshIscriptDialog(dlg);
                    return TRUE;
                }
                case IDC_ISCRIPT_PARSE:
                    RefreshIscriptDialog(dlg);
                    return TRUE;
                case IDC_ISCRIPT_ANIM:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        const u16 id = static_cast<u16>(
                            ReadClampedInt(dlg, IDC_ISCRIPT_ID, 198, 0, 65535));
                        ShowIscriptAnimation(dlg, g_iscript.Find(id));
                    }
                    return TRUE;
                case 2:
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

// 192：Iscript 测试（浏览动画操作码 + 线性模拟 Play）
namespace {

u16 IscriptTestId(HWND dlg) {
    return static_cast<u16>(ReadClampedInt(dlg, IDC_D192_1085, 198, 0, 65535));
}

int IscriptTestAnim(HWND dlg) {
    HWND combo = ::GetDlgItem(dlg, IDC_D192_1133);
    const int sel = static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return -1;
    const LRESULT data = ::SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
    return data == CB_ERR ? -1 : static_cast<int>(data);
}

void FillIscriptTestCombo(HWND dlg, const IscriptHeader* header) {
    HWND combo = ::GetDlgItem(dlg, IDC_D192_1133);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (!header) return;
    int first = -1;
    for (int i = 0; i < static_cast<int>(header->animOffsets.size()); i++) {
        if (header->animOffsets[static_cast<std::size_t>(i)] == 0) continue;
        const std::wstring name = WidenName(IscriptFile::AnimationName(i));
        const LRESULT idx = ::SendMessageW(combo, CB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(name.c_str()));
        if (idx == CB_ERR) continue;
        ::SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(idx),
                       static_cast<LPARAM>(i));
        if (first < 0) first = static_cast<int>(idx);
    }
    if (first >= 0) ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(first), 0);
}

void UpdateIscriptTest(HWND dlg, const wchar_t* extra) {
    const u16 id = IscriptTestId(dlg);
    const IscriptHeader* header = g_iscript.Find(id);
    const int anim = IscriptTestAnim(dlg);

    std::wstring summary;
    if (!header) {
        wchar_t msg[96];
        ::wsprintfW(msg, L"未找到 Iscript ID %u\r\n", static_cast<unsigned>(id));
        summary = msg;
    } else {
        wchar_t msg[256];
        ::wsprintfW(msg, L"Iscript %u  type %u\r\n动画槽 %u\r\n",
                    static_cast<unsigned>(header->id), static_cast<unsigned>(header->type),
                    static_cast<unsigned>(header->animOffsets.size()));
        summary = msg;
        if (anim >= 0) {
            wchar_t animMsg[128];
            ::wsprintfW(animMsg, L"动画 %d: %hs\r\n", anim, IscriptFile::AnimationName(anim));
            summary += animMsg;
        }
        if (extra) summary += extra;
    }
    ::SetDlgItemTextW(dlg, IDC_D192_1132, summary.c_str());

    HWND edit = ::GetDlgItem(dlg, IDC_D192_1079);
    std::vector<IscriptInstruction> list;
    if (!header || anim < 0 || !g_iscript.Disassemble(*header, anim, list)) {
        ::SetWindowTextW(edit, L"该动画没有可解析的操作码。");
        return;
    }
    std::wstring text;
    for (const IscriptInstruction& ins : list) {
        wchar_t line[256];
        int used = ::wsprintfW(line, L"%04X  %-18hs", static_cast<unsigned>(ins.offset),
                               IscriptFile::OpcodeName(ins.opcode));
        for (u8 b : ins.operands) {
            if (used > 200) break;
            used += ::wsprintfW(line + used, L" %02X", b);
        }
        text += line;
        text += L"\r\n";
    }
    ::SetWindowTextW(edit, text.c_str());
}

void RefreshIscriptTest(HWND dlg) {
    if (!g_iscript.loaded() && !LoadIscriptAuto(g_iscriptLabel)) g_iscriptLabel.clear();
    if (!g_iscript.loaded()) {
        ::SetDlgItemTextW(dlg, IDC_D192_1132,
                          L"未找到 iscript.bin\r\n可在 205 载入，或在 155 设置星际目录。");
        ::SetWindowTextW(::GetDlgItem(dlg, IDC_D192_1079), L"");
        return;
    }
    const u16 id = IscriptTestId(dlg);
    const IscriptHeader* header = g_iscript.Find(id);
    FillIscriptTestCombo(dlg, header);
    wchar_t name[128];
    if (header) {
        ::wsprintfW(name, L"iscript %u (type %u)", static_cast<unsigned>(id),
                    static_cast<unsigned>(header->type));
    } else {
        ::wsprintfW(name, L"iscript %u 未找到", static_cast<unsigned>(id));
    }
    ::SetDlgItemTextW(dlg, IDC_D192_1087, name);
    UpdateIscriptTest(dlg, nullptr);
}

void PlayIscriptTest(HWND dlg) {
    const u16 id = IscriptTestId(dlg);
    const IscriptHeader* header = g_iscript.Find(id);
    const int anim = IscriptTestAnim(dlg);
    if (!header || anim < 0) {
        ::SetDlgItemTextW(dlg, IDC_D192_1132, L"没有可模拟的动画。");
        return;
    }
    IscriptSimResult r;
    if (!g_iscript.Simulate(*header, anim, 1000, r)) {
        ::SetDlgItemTextW(dlg, IDC_D192_1132, L"该动画无法模拟。");
        return;
    }
    wchar_t line[320];
    ::wsprintfW(line, L"模拟 1000 步上限\r\nops=%d frames=%d waits=%d loops=%d\r\n"
                      L"last=%hs frame=%u\r\n%s\r\n",
                r.ops, r.frames, r.waits, r.loops,
                IscriptFile::OpcodeName(r.lastOp), static_cast<unsigned>(r.lastFrame),
                r.ended ? L"终止: 是" : L"终止: 否（循环或上限）");

    std::wstring extra = line;
    wchar_t grpPath[512] = L"";
    ::GetDlgItemTextW(dlg, IDC_D192_1086, grpPath, 511);
    if (grpPath[0]) {
        GrpFile grp;
        if (grp.Load(grpPath)) {
            wchar_t grpLine[192];
            ::wsprintfW(grpLine, L"GRP: %d 帧 %dx%d\r\n", grp.frameCount(), grp.width(),
                        grp.height());
            extra += grpLine;
            if (r.lastFrame < grp.frameCount()) {
                Buffer palette;
                GrpFile::GrayPalette(palette);
                Buffer bgra;
                int w = 0;
                int h = 0;
                if (grp.DecodeFrame(static_cast<int>(r.lastFrame), palette, bgra, w, h)) {
                    int count = 0;
                    for (std::size_t p = 3; p < bgra.size(); p += 4) {
                        if (bgra[p]) count++;
                    }
                    wchar_t frameLine[192];
                    ::wsprintfW(frameLine, L"解码帧 %u: %dx%d 不透明像素 %d\r\n",
                                static_cast<unsigned>(r.lastFrame), w, h, count);
                    extra += frameLine;
                }
            }
        } else {
            extra += L"GRP 读取失败\r\n";
        }
    }
    UpdateIscriptTest(dlg, extra.c_str());
}

}  // namespace

// 192：Iscript 测试
INT_PTR CALLBACK ProcIscriptTest(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_D192_1085, L"198");
            ::SetDlgItemTextW(dlg, IDC_D192_1086, L"");
            RefreshIscriptTest(dlg);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D192_1133:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        UpdateIscriptTest(dlg, nullptr);
                    }
                    return TRUE;
                case IDC_D192_1085:
                    if (HIWORD(wParam) == EN_KILLFOCUS) {
                        RefreshIscriptTest(dlg);
                    }
                    return TRUE;
                case IDC_D192_1086:
                    if (HIWORD(wParam) == EN_KILLFOCUS && g_iscript.loaded()) {
                        wchar_t grpPath[512] = L"";
                        ::GetDlgItemTextW(dlg, IDC_D192_1086, grpPath, 511);
                        std::wstring extra;
                        if (grpPath[0]) {
                            GrpFile grp;
                            wchar_t line[192];
                            if (grp.Load(grpPath)) {
                                ::wsprintfW(line, L"GRP: %d 帧 %dx%d\r\n", grp.frameCount(),
                                            grp.width(), grp.height());
                            } else {
                                ::wsprintfW(line, L"GRP 读取失败\r\n");
                            }
                            extra = line;
                        }
                        UpdateIscriptTest(dlg, extra.c_str());
                    }
                    return TRUE;
                case IDC_D192_1130:                              // Play
                    PlayIscriptTest(dlg);
                    return TRUE;
                case IDC_D192_1080:                              // Restart
                    RefreshIscriptTest(dlg);
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

// 183：标签容器
INT_PTR CALLBACK ProcTabHost(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D183_1) {                  // 确定
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D183_2) {
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

// 184：纯列表容器
INT_PTR CALLBACK ProcListViewHost(HWND dlg, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            return TRUE;
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// 185 / 186：触发条件 / 行动列表，实际逻辑在 dlg_triggers.cpp
INT_PTR CALLBACK ProcListEditorA(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    return HandleTriggerListEditor(dlg, msg, wParam);
}
INT_PTR CALLBACK ProcListEditorB(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    return HandleTriggerListEditor(dlg, msg, wParam);
}

// 187：富文本（RichEdit20A）
namespace {
std::wstring g_richText;
}
const wchar_t* LastRichText() { return g_richText.c_str(); }
void SeedRichText(const std::wstring& text) { g_richText = text; }

INT_PTR CALLBACK ProcRichText(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemTextW(dlg, IDC_D187_1134, g_richText.c_str());
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D187_1) {
                wchar_t buf[4096] = L"";
                ::GetDlgItemTextW(dlg, IDC_D187_1134, buf, 4095);
                g_richText = buf;
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D187_2) {
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
