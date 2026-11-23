#include "ui/dlg_procs.h"

#include "common/log.h"
#include "map/decor_model.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"
#include "units/unit_data.h"

#include <cwchar>
#include <vector>

namespace scmd {

namespace {
UnitProps g_unitProps;
UnitProps g_unitPropsSeed;
UnitPlacementParams g_unitPlacement;
UnitPlacementParams g_unitPlacementSeed;
std::vector<SpriteRecord> g_spUnits;
std::vector<SpriteRecord> g_spUnitsSeed;
int g_spSelected = 0;
}

const UnitProps& LastUnitProps() { return g_unitProps; }
const UnitPlacementParams& LastUnitPlacement() { return g_unitPlacement; }
const std::vector<SpriteRecord>& LastSpUnits() { return g_spUnits; }

void SeedUnitProps(const UnitProps& seed) { g_unitPropsSeed = seed; }
void SeedUnitPlacement(const UnitPlacementParams& seed) { g_unitPlacementSeed = seed; }
void SeedSpUnits(const std::vector<SpriteRecord>& units, int selected) {
    g_spUnitsSeed = units;
    g_spSelected = selected;
}

// 180：设置单位属性（生命/护盾/魔法/资源/弹药 + 5 个状态位）
INT_PTR CALLBACK ProcUnitProps(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            SetPercent(dlg, IDC_D180_1078, g_unitPropsSeed.hp);
            SetPercent(dlg, IDC_D180_1079, g_unitPropsSeed.shield);
            SetPercent(dlg, IDC_D180_1081, g_unitPropsSeed.energy);
            ::SetDlgItemInt(dlg, IDC_D180_1080, static_cast<UINT>(g_unitPropsSeed.resource), FALSE);
            ::SetDlgItemInt(dlg, IDC_D180_1082, static_cast<UINT>(g_unitPropsSeed.ammo), FALSE);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D180_1) {                 // 确定
                g_unitProps.hp = ReadClampedInt(dlg, IDC_D180_1078, 100, 0, 100);
                g_unitProps.shield = ReadClampedInt(dlg, IDC_D180_1079, 100, 0, 100);
                g_unitProps.energy = ReadClampedInt(dlg, IDC_D180_1081, 100, 0, 100);
                g_unitProps.resource = ReadClampedInt(dlg, IDC_D180_1080, 0, 0, 0x7FFFFF);
                g_unitProps.ammo = ReadClampedInt(dlg, IDC_D180_1082, 0, 0, 255);
                unsigned f = 0;
                if (::IsDlgButtonChecked(dlg, IDC_D180_1073)) f |= 1u << 0;   // 无敌
                if (::IsDlgButtonChecked(dlg, IDC_D180_1074)) f |= 1u << 1;   // 幻影
                if (::IsDlgButtonChecked(dlg, IDC_D180_1075)) f |= 1u << 2;   // 钻地
                if (::IsDlgButtonChecked(dlg, IDC_D180_1076)) f |= 1u << 3;   // 隐形
                if (::IsDlgButtonChecked(dlg, IDC_D180_1077)) f |= 1u << 4;   // 升起
                g_unitProps.flags = f;
                Log(L"[unit] hp=%d sh=%d en=%d res=%d ammo=%d flags=0x%X", g_unitProps.hp,
                    g_unitProps.shield, g_unitProps.energy, g_unitProps.resource, g_unitProps.ammo, f);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D180_2) {
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

// 195：单位放置参数（类型 / 玩家 / 资源）
INT_PTR CALLBACK ProcUnitPlacement(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D195_TYPE, static_cast<UINT>(g_unitPlacementSeed.type), FALSE);
            ::SetDlgItemInt(dlg, IDC_D195_RESOURCE,
                            static_cast<UINT>(g_unitPlacementSeed.resource), FALSE);

            struct Preset { const wchar_t* name; int type; };
            const Preset presets[] = {
                {L"矿物 176", 176}, {L"矿物 177", 177}, {L"矿物 178", 178},
                {L"气矿 188", 188}, {L"出生点 214", 214},
            };
            HWND preset = ::GetDlgItem(dlg, IDC_D195_PRESET);
            LRESULT presetSel = -1;
            for (const Preset& p : presets) {
                const LRESULT at = ::SendMessageW(preset, CB_ADDSTRING, 0,
                                                  reinterpret_cast<LPARAM>(p.name));
                if (at != CB_ERR) {
                    ::SendMessageW(preset, CB_SETITEMDATA, static_cast<WPARAM>(at),
                                   static_cast<LPARAM>(p.type));
                    if (p.type == g_unitPlacementSeed.type) presetSel = at;
                }
            }
            ::SendMessageW(preset, CB_SETCURSEL, static_cast<WPARAM>(presetSel), 0);

            HWND owner = ::GetDlgItem(dlg, IDC_D195_OWNER);
            for (int i = 0; i < 8; i++) {
                wchar_t label[32];
                ::wsprintfW(label, L"Player %d", i + 1);
                const LRESULT at = ::SendMessageW(owner, CB_ADDSTRING, 0,
                                                  reinterpret_cast<LPARAM>(label));
                if (at != CB_ERR) {
                    ::SendMessageW(owner, CB_SETITEMDATA, static_cast<WPARAM>(at),
                                   static_cast<LPARAM>(i));
                }
            }
            {
                const LRESULT at = ::SendMessageW(owner, CB_ADDSTRING, 0,
                                                  reinterpret_cast<LPARAM>(L"中立 (11)"));
                if (at != CB_ERR) {
                    ::SendMessageW(owner, CB_SETITEMDATA, static_cast<WPARAM>(at), 11);
                }
            }
            int sel = 0;
            const LRESULT count = ::SendMessageW(owner, CB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                if (::SendMessageW(owner, CB_GETITEMDATA, static_cast<WPARAM>(i), 0) ==
                    g_unitPlacementSeed.owner) {
                    sel = i;
                    break;
                }
            }
            ::SendMessageW(owner, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_D195_PRESET && HIWORD(wParam) == CBN_SELCHANGE) {
                HWND preset = ::GetDlgItem(dlg, IDC_D195_PRESET);
                const LRESULT sel = ::SendMessageW(preset, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    const LRESULT type = ::SendMessageW(preset, CB_GETITEMDATA,
                                                        static_cast<WPARAM>(sel), 0);
                    if (type != CB_ERR) {
                        ::SetDlgItemInt(dlg, IDC_D195_TYPE, static_cast<UINT>(type), FALSE);
                    }
                }
                return TRUE;
            }
            if (id == IDC_D195_1) {                 // 确定
                g_unitPlacement.type = ReadClampedInt(dlg, IDC_D195_TYPE, 176, 0, 65535);
                g_unitPlacement.resource = ReadClampedInt(dlg, IDC_D195_RESOURCE, 1500, 0, 0x7FFFFF);
                HWND owner = ::GetDlgItem(dlg, IDC_D195_OWNER);
                const LRESULT sel = ::SendMessageW(owner, CB_GETCURSEL, 0, 0);
                LRESULT data = (sel == CB_ERR) ? 11
                                               : ::SendMessageW(owner, CB_GETITEMDATA,
                                                                static_cast<WPARAM>(sel), 0);
                if (data == CB_ERR) data = 11;
                g_unitPlacement.owner = static_cast<int>(data);
                Log(L"[unit] placement type=%d owner=%d resource=%d", g_unitPlacement.type,
                    g_unitPlacement.owner, g_unitPlacement.resource);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDC_D195_2 || id == IDCANCEL) {
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

// 172：单位名字表。选中条目回填各字段。
namespace {

void PopulateUnitDataList(HWND dlg) {
    UnitDataTable table;
    std::wstring source;
    if (!UnitDataTable::LoadAuto(table, source)) return;
    HWND list = ::GetDlgItem(dlg, IDC_D172_1060);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const UnitData& u : table.items()) {
        wchar_t line[192];
        ::wsprintfW(line, L"#%d  HP %d  SH %d  A %d  矿 %d 气 %d  时 %d  %dx%d", u.id,
                    u.hp, u.shields, u.armor, u.minerals, u.gas, u.buildTime, u.width,
                    u.height);
        const LRESULT idx = ::SendMessageW(list, LB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(line));
        if (idx != CB_ERR) {
            ::SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(idx),
                           static_cast<LPARAM>(u.id));
        }
    }
}

}  // namespace

INT_PTR CALLBACK ProcUnitNames(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            PopulateUnitDataList(dlg);
            SetPercent(dlg, IDC_D172_1078, 100);
            SetPercent(dlg, IDC_D172_1079, 100);
            SetPercent(dlg, IDC_D172_1081, 100);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D172_1060:                              // 列表选择
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        const int sel = ListSelectedIndex(dlg, IDC_D172_1060);
                        if (sel >= 0) {
                            const LRESULT id = ::SendDlgItemMessageW(
                                dlg, IDC_D172_1060, LB_GETITEMDATA,
                                static_cast<WPARAM>(sel), 0);
                            wchar_t name[64];
                            ::wsprintfW(name, L"Unit %d",
                                        id == CB_ERR ? -1 : static_cast<int>(id));
                            ::SetDlgItemTextW(dlg, IDC_D172_1072, name);
                        }
                    }
                    return TRUE;
                case IDC_D172_1073:                              // 无敌
                case IDC_D172_1074:                              // 幻影
                case IDC_D172_1075:                              // 钻地
                case IDC_D172_1076:                              // 隐形
                case IDC_D172_1077:                              // 升起
                    // BS_AUTOCHECKBOX 由系统自动切换，这里只记日志
                    Log(L"[unitname] flag button %d toggled", LOWORD(wParam));
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

// 164：SP 单位属性。列表对应 THG2 记录；Bit0..11 + Flipped(bit13) +
// 禁用(bit15) + 普通SP/单位SP(bit12)；1037 为记录里的未用字节。
namespace {

void FillSpUnitList(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_D164_1019);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < g_spUnits.size(); i++) {
        const SpriteRecord& s = g_spUnits[i];
        wchar_t line[128];
        ::wsprintfW(line, L"#%u  类型 %u  (%u,%u)  P%u%s", static_cast<unsigned>(i),
                    static_cast<unsigned>(s.type), static_cast<unsigned>(s.x),
                    static_cast<unsigned>(s.y), static_cast<unsigned>(s.owner) + 1,
                    s.DrawAsSprite() ? L"  [SPR]" : L"");
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    }
    if (!g_spUnits.empty()) {
        if (g_spSelected < 0) g_spSelected = 0;
        if (static_cast<std::size_t>(g_spSelected) >= g_spUnits.size()) {
            g_spSelected = static_cast<int>(g_spUnits.size()) - 1;
        }
        ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(g_spSelected), 0);
    }
}

void LoadSpUnitFields(HWND dlg) {
    if (g_spUnits.empty() || g_spSelected < 0 ||
        static_cast<std::size_t>(g_spSelected) >= g_spUnits.size()) {
        return;
    }
    const SpriteRecord& s = g_spUnits[g_spSelected];
    HWND combo = ::GetDlgItem(dlg, IDC_D164_1022);
    ::SendMessageW(combo, CB_SETCURSEL, s.owner < 8 ? s.owner : static_cast<WPARAM>(-1), 0);
    for (int i = 0; i < 12; i++) {
        ::CheckDlgButton(dlg, IDC_D164_1023 + i,
                         (s.flags & (1u << i)) ? BST_CHECKED : BST_UNCHECKED);
    }
    ::CheckDlgButton(dlg, IDC_D164_1035, (s.flags & 0x2000u) ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_D164_1036, (s.flags & 0x8000u) ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_D164_1039, s.DrawAsSprite() ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dlg, IDC_D164_1040, s.DrawAsSprite() ? BST_UNCHECKED : BST_CHECKED);
    wchar_t hex[16];
    ::wsprintfW(hex, L"0x%02X", s.unused);
    ::SetDlgItemTextW(dlg, IDC_D164_1037, hex);
}

unsigned ParseHexByte(HWND dlg, int id, unsigned def) {
    wchar_t buf[32] = L"";
    ::GetDlgItemTextW(dlg, id, buf, 32);
    wchar_t* end = nullptr;
    const unsigned long v = std::wcstoul(buf, &end, 16);
    if (end == buf) return def;
    return v > 0xFF ? 0xFF : static_cast<unsigned>(v);
}

void StoreSpUnitFields(HWND dlg) {
    if (g_spUnits.empty() || g_spSelected < 0 ||
        static_cast<std::size_t>(g_spSelected) >= g_spUnits.size()) {
        return;
    }
    SpriteRecord& s = g_spUnits[g_spSelected];
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D164_1022, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < 8) s.owner = static_cast<u8>(sel);
    s.unused = static_cast<u8>(ParseHexByte(dlg, IDC_D164_1037, s.unused));

    unsigned f = s.flags & ~0x0FFFu;
    for (int i = 0; i < 12; i++) {
        if (::IsDlgButtonChecked(dlg, IDC_D164_1023 + i) == BST_CHECKED) f |= 1u << i;
    }
    if (::IsDlgButtonChecked(dlg, IDC_D164_1039) == BST_CHECKED) f |= 0x1000u;   // 普通SP
    else f &= ~0x1000u;
    if (::IsDlgButtonChecked(dlg, IDC_D164_1035) == BST_CHECKED) f |= 0x2000u;   // Flipped
    else f &= ~0x2000u;
    if (::IsDlgButtonChecked(dlg, IDC_D164_1036) == BST_CHECKED) f |= 0x8000u;   // 禁用
    else f &= ~0x8000u;
    s.flags = static_cast<u16>(f);
}

}  // namespace

INT_PTR CALLBACK ProcUnitSpFlags(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            g_spUnits = g_spUnitsSeed;
            HWND combo = ::GetDlgItem(dlg, IDC_D164_1022);
            for (int p = 1; p <= 8; p++) {
                wchar_t label[16];
                ::wsprintfW(label, L"Player %d", p);
                ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            }
            FillSpUnitList(dlg);
            // 资源里 1039/1040 被置灰，这里启用（普通SP / 单位SP 二选一）
            ::EnableWindow(::GetDlgItem(dlg, IDC_D164_1039), TRUE);
            ::EnableWindow(::GetDlgItem(dlg, IDC_D164_1040), TRUE);
            LoadSpUnitFields(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_D164_1019 && HIWORD(wParam) == LBN_SELCHANGE) {
                StoreSpUnitFields(dlg);
                const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D164_1019, LB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    g_spSelected = static_cast<int>(sel);
                    LoadSpUnitFields(dlg);
                }
                return TRUE;
            }
            if (id == IDC_D164_1039 || id == IDC_D164_1040) {
                // 普通SP / 单位SP 二选一：bit12 = 按精灵绘制
                const bool normal = id == IDC_D164_1039;
                ::CheckDlgButton(dlg, IDC_D164_1039, normal ? BST_CHECKED : BST_UNCHECKED);
                ::CheckDlgButton(dlg, IDC_D164_1040, normal ? BST_UNCHECKED : BST_CHECKED);
                return TRUE;
            }
            if (id == IDC_D164_1) {                             // 关闭（应用修改）
                StoreSpUnitFields(dlg);
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            StoreSpUnitFields(dlg);
            ::EndDialog(dlg, IDOK);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace scmd
