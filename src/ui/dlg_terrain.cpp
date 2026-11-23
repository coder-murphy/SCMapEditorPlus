#include "ui/dlg_procs.h"

#include <algorithm>
#include <cwchar>
#include <utility>

#include "common/log.h"
#include "platform/file_dialog.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {
GridSettings g_grid;
GridSettings g_seed;
bool g_gridAccepted = false;

std::vector<TerrainBrush> g_brushes;
TerrainBrush g_currentBrush;
TerrainBrush g_selectedBrush;
int g_selectedBrushIndex = -1;
int g_filterMode = 0;   // 0 全部 / 1 单格 / 2 多格
int g_sortMode = 0;     // 0 名称 / 1 尺寸
bool g_brushAccepted = false;

constexpr const wchar_t* kBrushFilter =
    L"SCMD 自定义刷子 (*.scb)\0*.scb\0所有文件 (*.*)\0*.*\0\0";

bool MatchBrushFilter(const TerrainBrush& brush, int mode) {
    if (mode == 1) return brush.width == 1 && brush.height == 1;
    if (mode == 2) return brush.width > 1 || brush.height > 1;
    return true;
}

int SelectedBrushIndex(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_D182_1060);
    if (!list) return -1;
    const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return -1;
    const int index = static_cast<int>(::SendMessageW(list, LB_GETITEMDATA,
                                                      static_cast<WPARAM>(sel), 0));
    return index >= 0 && index < static_cast<int>(g_brushes.size()) ? index : -1;
}

void RebuildBrushList(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_D182_1060);
    if (!list) return;
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);

    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(g_brushes.size()); i++) {
        if (MatchBrushFilter(g_brushes[i], g_filterMode)) indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), [](int a, int b) {
        const TerrainBrush& x = g_brushes[a];
        const TerrainBrush& y = g_brushes[b];
        if (g_sortMode == 1) {
            const int ax = x.width * x.height;
            const int ay = y.width * y.height;
            if (ax != ay) return ax < ay;
        }
        return _wcsicmp(x.name.c_str(), y.name.c_str()) < 0;
    });

    for (int index : indices) {
        const TerrainBrush& brush = g_brushes[index];
        wchar_t label[192];
        ::wsprintfW(label, L"%s  [%dx%d]", brush.name.c_str(), brush.width, brush.height);
        const LRESULT at = ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (at != LB_ERR) {
            ::SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(at),
                           static_cast<LPARAM>(index));
        }
    }

    int sel = -1;
    const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; i++) {
        if (static_cast<int>(::SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(i), 0)) ==
            g_selectedBrushIndex) {
            sel = i;
            break;
        }
    }
    if (sel < 0 && count > 0) sel = 0;

    if (sel >= 0) {
        ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
        g_selectedBrushIndex = static_cast<int>(::SendMessageW(list, LB_GETITEMDATA,
                                                               static_cast<WPARAM>(sel), 0));
        g_selectedBrush = g_brushes[g_selectedBrushIndex];
        ::SetDlgItemTextW(dlg, IDC_D182_1079, g_selectedBrush.name.c_str());
    } else {
        g_selectedBrushIndex = -1;
        g_selectedBrush = TerrainBrush();
        ::SetDlgItemTextW(dlg, IDC_D182_1079, L"");
    }
}

bool ApplySelectedBrush(HWND dlg) {
    const int index = SelectedBrushIndex(dlg);
    if (index < 0) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    g_selectedBrushIndex = index;
    g_selectedBrush = g_brushes[index];
    g_brushAccepted = true;
    ::EndDialog(dlg, IDOK);
    return true;
}
}  // namespace

const GridSettings& LastGridSettings() { return g_grid; }

void SeedGridSettings(const GridSettings& seed) {
    g_seed = seed;
    g_gridAccepted = false;
}

bool LastGridSettingsAccepted() { return g_gridAccepted; }

void SeedCustomBrushes(const std::vector<TerrainBrush>& brushes, const TerrainBrush& current,
                       int selectedIndex) {
    g_brushes = brushes;
    g_currentBrush = current;
    g_selectedBrushIndex = selectedIndex;
    g_brushAccepted = false;
    g_filterMode = 0;
    g_sortMode = 0;
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(g_brushes.size())) {
        g_selectedBrush = g_brushes[selectedIndex];
    } else if (!g_brushes.empty()) {
        g_selectedBrushIndex = 0;
        g_selectedBrush = g_brushes[0];
    } else {
        g_selectedBrush = TerrainBrush();
    }
}

const std::vector<TerrainBrush>& LastCustomBrushes() { return g_brushes; }
const TerrainBrush& LastCustomBrush() { return g_selectedBrush; }
bool LastCustomBrushAccepted() { return g_brushAccepted; }

// 166：方格设置（两组 X/Y 与偏移 + 启用开关）
INT_PTR CALLBACK ProcGridSettings(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::CheckDlgButton(dlg, IDC_D166_1048, g_seed.enabled ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(dlg, IDC_D166_1044, static_cast<UINT>(g_seed.cellX), FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1045, static_cast<UINT>(g_seed.cellY), FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1046, static_cast<UINT>(g_seed.offsetX), TRUE);
            ::SetDlgItemInt(dlg, IDC_D166_1047, static_cast<UINT>(g_seed.offsetY), TRUE);
            // 第二组“静态方格”（1049..1053）：固定于地图原点的第二套参考网格
            ::CheckDlgButton(dlg, IDC_D166_1053,
                             g_seed.staticEnabled ? BST_CHECKED : BST_UNCHECKED);
            ::SetDlgItemInt(dlg, IDC_D166_1049, static_cast<UINT>(g_seed.staticCellX), FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1050, static_cast<UINT>(g_seed.staticCellY), FALSE);
            ::SetDlgItemInt(dlg, IDC_D166_1051, static_cast<UINT>(g_seed.staticOffsetX), TRUE);
            ::SetDlgItemInt(dlg, IDC_D166_1052, static_cast<UINT>(g_seed.staticOffsetY), TRUE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D166_1) {                 // 确定
                g_grid.enabled = ::IsDlgButtonChecked(dlg, IDC_D166_1048) == BST_CHECKED;
                g_grid.cellX = ReadClampedInt(dlg, IDC_D166_1044, 32, 1, 64);
                g_grid.cellY = ReadClampedInt(dlg, IDC_D166_1045, 32, 1, 64);
                g_grid.offsetX = ReadClampedInt(dlg, IDC_D166_1046, 0, -64, 64);
                g_grid.offsetY = ReadClampedInt(dlg, IDC_D166_1047, 0, -64, 64);
                g_grid.staticEnabled =
                    ::IsDlgButtonChecked(dlg, IDC_D166_1053) == BST_CHECKED;
                g_grid.staticCellX = ReadClampedInt(dlg, IDC_D166_1049, 32, 1, 64);
                g_grid.staticCellY = ReadClampedInt(dlg, IDC_D166_1050, 32, 1, 64);
                g_grid.staticOffsetX = ReadClampedInt(dlg, IDC_D166_1051, 0, -64, 64);
                g_grid.staticOffsetY = ReadClampedInt(dlg, IDC_D166_1052, 0, -64, 64);
                g_gridAccepted = true;
                Log(L"[grid] %dx%d off=%d,%d on=%d | static %dx%d off=%d,%d on=%d",
                    g_grid.cellX, g_grid.cellY, g_grid.offsetX, g_grid.offsetY,
                    g_grid.enabled ? 1 : 0,
                    g_grid.staticCellX, g_grid.staticCellY,
                    g_grid.staticOffsetX, g_grid.staticOffsetY,
                    g_grid.staticEnabled ? 1 : 0);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D166_2) {
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

// 182：自定义刷子（列表增删、打开/保存 .scb、双击应用）
INT_PTR CALLBACK ProcCustomBrush(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            g_brushAccepted = false;

            HWND filter = ::GetDlgItem(dlg, IDC_D182_1126);
            ::SendMessageW(filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部刷子"));
            ::SendMessageW(filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"单格刷"));
            ::SendMessageW(filter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"多格刷"));
            ::SendMessageW(filter, CB_SETCURSEL, static_cast<WPARAM>(g_filterMode), 0);

            HWND sort = ::GetDlgItem(dlg, IDC_D182_1120);
            ::SendMessageW(sort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"按名称"));
            ::SendMessageW(sort, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"按尺寸"));
            ::SendMessageW(sort, CB_SETCURSEL, static_cast<WPARAM>(g_sortMode), 0);

            RebuildBrushList(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDC_D182_1126 && code == CBN_SELCHANGE) {
                g_filterMode = static_cast<int>(::SendDlgItemMessageW(
                    dlg, IDC_D182_1126, CB_GETCURSEL, 0, 0));
                RebuildBrushList(dlg);
                return TRUE;
            }
            if (id == IDC_D182_1120 && code == CBN_SELCHANGE) {
                g_sortMode = static_cast<int>(::SendDlgItemMessageW(
                    dlg, IDC_D182_1120, CB_GETCURSEL, 0, 0));
                RebuildBrushList(dlg);
                return TRUE;
            }
            if (id == IDC_D182_1060) {
                if (code == LBN_SELCHANGE) {
                    const int index = SelectedBrushIndex(dlg);
                    if (index >= 0) {
                        g_selectedBrushIndex = index;
                        g_selectedBrush = g_brushes[index];
                        ::SetDlgItemTextW(dlg, IDC_D182_1079,
                                          g_selectedBrush.name.c_str());
                    }
                    return TRUE;
                }
                if (code == LBN_DBLCLK) {
                    ApplySelectedBrush(dlg);
                    return TRUE;
                }
            }
            if (id == IDC_D182_1006) {                          // 添加刷子
                if (g_currentBrush.Empty()) {
                    ::MessageBoxW(dlg, L"当前没有可添加的刷子图案。", L"自定义刷子",
                                  MB_ICONINFORMATION);
                    return TRUE;
                }
                wchar_t name[128] = L"";
                ::GetDlgItemTextW(dlg, IDC_D182_1079, name, 127);
                TerrainBrush brush = g_currentBrush;
                if (name[0]) {
                    brush.name = name;
                } else {
                    wchar_t fallback[64];
                    ::wsprintfW(fallback, L"刷子 %d", static_cast<int>(g_brushes.size()) + 1);
                    brush.name = fallback;
                }
                g_brushes.push_back(std::move(brush));
                g_selectedBrushIndex = static_cast<int>(g_brushes.size()) - 1;
                RebuildBrushList(dlg);
                return TRUE;
            }
            if (id == IDC_D182_1124) {                          // 删除
                const int index = SelectedBrushIndex(dlg);
                if (index >= 0) {
                    g_brushes.erase(g_brushes.begin() + index);
                    g_selectedBrushIndex = g_brushes.empty()
                                               ? -1
                                               : std::min(index,
                                                          static_cast<int>(g_brushes.size()) - 1);
                    RebuildBrushList(dlg);
                }
                return TRUE;
            }
            if (id == IDC_D182_1059) {                          // 打开文件
                std::wstring path;
                if (OpenFileDialog(dlg, kBrushFilter, path)) {
                    std::vector<TerrainBrush> loaded;
                    if (LoadBrushes(path, loaded)) {
                        g_brushes = std::move(loaded);
                        g_selectedBrushIndex = g_brushes.empty() ? -1 : 0;
                        RebuildBrushList(dlg);
                    } else {
                        ::MessageBoxW(dlg, L"无法读取刷子文件（格式或版本不匹配）。",
                                      L"自定义刷子", MB_ICONWARNING);
                    }
                }
                return TRUE;
            }
            if (id == IDC_D182_1125) {                          // 保存文件
                if (g_brushes.empty()) {
                    MessageBeep(MB_ICONWARNING);
                    return TRUE;
                }
                std::wstring path;
                if (SaveFileDialog(dlg, kBrushFilter, L"scb", path)) {
                    if (!SaveBrushes(path, g_brushes)) {
                        ::MessageBoxW(dlg, L"写入刷子文件失败。", L"自定义刷子", MB_ICONWARNING);
                    }
                }
                return TRUE;
            }
            if (id == IDOK) {                                   // 回车应用
                ApplySelectedBrush(dlg);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            g_brushAccepted = false;
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace scmd
