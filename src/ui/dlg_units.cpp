#include "ui/dlg_procs.h"

#include "common/log.h"
#include "map/decor_model.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"
#include "units/unit_data.h"
#include "units/stat_txt.h"

#include <algorithm>
#include <cwchar>
#include <map>
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
            // 状态位顺序与资源按钮一致：无敌=bit4、幻影=bit3、钻地=bit1、隐形=bit0、升起=bit2
            ::CheckDlgButton(dlg, IDC_D180_1073,
                             (g_unitPropsSeed.flags & 0x10u) ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D180_1074,
                             (g_unitPropsSeed.flags & 0x08u) ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D180_1075,
                             (g_unitPropsSeed.flags & 0x02u) ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D180_1076,
                             (g_unitPropsSeed.flags & 0x01u) ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dlg, IDC_D180_1077,
                             (g_unitPropsSeed.flags & 0x04u) ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D180_1) {                 // 确定
                g_unitProps.hp = ReadClampedInt(dlg, IDC_D180_1078, 100, 0, 100);
                g_unitProps.shield = ReadClampedInt(dlg, IDC_D180_1079, 100, 0, 100);
                g_unitProps.energy = ReadClampedInt(dlg, IDC_D180_1081, 100, 0, 100);
                g_unitProps.resource = ReadClampedInt(dlg, IDC_D180_1080, 0, 0, 0x7FFFFF);
                g_unitProps.ammo = ReadClampedInt(dlg, IDC_D180_1082, 0, 0, 255);
                unsigned f = 0;
                if (::IsDlgButtonChecked(dlg, IDC_D180_1073)) f |= 1u << 4;   // 无敌
                if (::IsDlgButtonChecked(dlg, IDC_D180_1074)) f |= 1u << 3;   // 幻影
                if (::IsDlgButtonChecked(dlg, IDC_D180_1075)) f |= 1u << 1;   // 钻地
                if (::IsDlgButtonChecked(dlg, IDC_D180_1076)) f |= 1u << 0;   // 隐形
                if (::IsDlgButtonChecked(dlg, IDC_D180_1077)) f |= 1u << 2;   // 升起
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

// 172：单位名字 / 属性表。地图打开时按单位类型列出实例，修改后立即写回 UNIT；
// 未打开地图时退回 units.dat 只读预览。
namespace {

struct UnitNamesState {
    int type = -1;         // 当前下拉选中的单位类型
    int unitIndex = -1;    // 当前列表选中单位在 map.units() 中的下标
    bool filling = false;  // 回填字段期间抑制写回
};
UnitNamesState g_names;

// stat_txt.tbl 的 228 个单位名（懒加载，失败则退回 "Unit N"）
const StatTextTable& UnitNameTable() {
    static StatTextTable table;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::wstring source;
        StatTextTable::LoadAuto(table, source);
    }
    return table;
}

std::wstring UnitLabel(int type) {
    const std::wstring& name = UnitNameTable().Name(type);
    if (!name.empty()) return name;
    return L"Unit " + std::to_wstring(type);
}

// 5 个状态位 → UNIT +26 激活状态位（与 180 的位序一致）
struct UnitStateBit {
    int controlId;
    u16 bit;
};
const UnitStateBit kUnitStateBits[] = {
    {IDC_D172_1073, 0x0010},   // 无敌 bit4
    {IDC_D172_1074, 0x0008},   // 幻影 bit3
    {IDC_D172_1075, 0x0002},   // 钻地 bit1
    {IDC_D172_1076, 0x0001},   // 隐形 bit0
    {IDC_D172_1077, 0x0004},   // 升起 bit2
};

void EnableUnitFields(HWND dlg, bool enable) {
    const int kFields[] = {IDC_D172_1078, IDC_D172_1079, IDC_D172_1081,
                           IDC_D172_1080, IDC_D172_1082,
                           IDC_D172_1073, IDC_D172_1074, IDC_D172_1075,
                           IDC_D172_1076, IDC_D172_1077};
    for (int id : kFields) ::EnableWindow(::GetDlgItem(dlg, id), enable ? TRUE : FALSE);
}

void FillTypeCombo(HWND dlg, MapDocument* doc) {
    HWND combo = ::GetDlgItem(dlg, IDC_D172_1072);
    ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (doc) {
        std::map<int, int> counts;
        for (const UnitRecord& u : doc->units()) counts[static_cast<int>(u.type)]++;
        for (const auto& entry : counts) {
            std::wstring label = UnitLabel(entry.first) + L"  (" +
                                 std::to_wstring(entry.second) + L" 个)";
            const LRESULT at = ::SendMessageW(combo, CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(label.c_str()));
            if (at != CB_ERR) {
                ::SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(at),
                               static_cast<LPARAM>(entry.first));
            }
        }
        if (::SendMessageW(combo, CB_GETCOUNT, 0, 0) > 0) {
            ::SendMessageW(combo, CB_SETCURSEL, 0, 0);
        }
        return;
    }
    // 无地图：下拉保持为空，1060 仍显示 units.dat 只读预览
}

void FillUnitList(HWND dlg, MapDocument* doc) {
    HWND list = ::GetDlgItem(dlg, IDC_D172_1060);
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    g_names.unitIndex = -1;
    if (!doc || g_names.type < 0) {
        // 无地图：退回 units.dat 属性列表
        UnitDataTable table;
        std::wstring source;
        if (!UnitDataTable::LoadAuto(table, source)) return;
        for (const UnitData& u : table.items()) {
            wchar_t line[192];
            ::wsprintfW(line, L"#%d  HP %d  SH %d  A %d  矿 %d 气 %d  时 %d  %dx%d", u.id,
                        u.hp, u.shields, u.armor, u.minerals, u.gas, u.buildTime, u.width,
                        u.height);
            const LRESULT at = ::SendMessageW(list, LB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(line));
            if (at != CB_ERR) {
                ::SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(at),
                               static_cast<LPARAM>(u.id));
            }
        }
        return;
    }
    const auto& units = doc->units();
    for (std::size_t i = 0; i < units.size(); i++) {
        const UnitRecord& u = units[i];
        if (static_cast<int>(u.type) != g_names.type) continue;
        std::wstring label = L"#" + std::to_wstring(u.serial) + L"  " +
                             UnitLabel(g_names.type) + L"  格 " +
                             std::to_wstring(static_cast<int>(u.x) / kTilePixels) + L"," +
                             std::to_wstring(static_cast<int>(u.y) / kTilePixels) +
                             L"  玩家 " + std::to_wstring(u.owner) + L"  生命 " +
                             std::to_wstring(u.hpPercent) + L"%";
        const LRESULT at = ::SendMessageW(list, LB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(label.c_str()));
        if (at != CB_ERR) {
            ::SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(at),
                           static_cast<LPARAM>(i));
        }
    }
    if (::SendMessageW(list, LB_GETCOUNT, 0, 0) > 0) {
        ::SendMessageW(list, LB_SETCURSEL, 0, 0);
        ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDC_D172_1060, LBN_SELCHANGE),
                       reinterpret_cast<LPARAM>(list));
    }
}

UnitRecord* SelectedUnit(MapDocument* doc) {
    if (!doc || g_names.unitIndex < 0) return nullptr;
    auto& units = doc->mutableUnits();
    if (static_cast<std::size_t>(g_names.unitIndex) >= units.size()) return nullptr;
    return &units[static_cast<std::size_t>(g_names.unitIndex)];
}

void LoadUnitFields(HWND dlg, const UnitRecord& u) {
    g_names.filling = true;
    ::SetDlgItemInt(dlg, IDC_D172_1078, u.hpPercent, FALSE);
    ::SetDlgItemInt(dlg, IDC_D172_1079, u.shieldPercent, FALSE);
    ::SetDlgItemInt(dlg, IDC_D172_1081, u.energyPercent, FALSE);
    ::SetDlgItemInt(dlg, IDC_D172_1080, u.resource, FALSE);
    ::SetDlgItemInt(dlg, IDC_D172_1082, u.stackCount, FALSE);
    const u16 flags = static_cast<u16>(u.queueCount & 0x001Fu);
    for (const UnitStateBit& state : kUnitStateBits) {
        ::CheckDlgButton(dlg, state.controlId,
                         (flags & state.bit) ? BST_CHECKED : BST_UNCHECKED);
    }
    g_names.filling = false;
}

// 把当前控件值写回选中单位（失焦、勾选状态与关闭对话框时调用）
void StoreUnitFields(HWND dlg) {
    if (g_names.filling) return;
    MapDocument* doc = DialogDocument();
    UnitRecord* u = SelectedUnit(doc);
    if (!doc || !u) return;

    const u8 oldHp = u->hpPercent;
    const u8 oldShield = u->shieldPercent;
    const u8 oldEnergy = u->energyPercent;
    const u32 oldResource = u->resource;
    const u16 oldStack = u->stackCount;
    const u16 oldQueue = u->queueCount;
    const u16 oldValid = u->validState;

    u->hpPercent = static_cast<u8>(ReadClampedInt(dlg, IDC_D172_1078, u->hpPercent, 0, 100));
    u->shieldPercent =
        static_cast<u8>(ReadClampedInt(dlg, IDC_D172_1079, u->shieldPercent, 0, 100));
    u->energyPercent =
        static_cast<u8>(ReadClampedInt(dlg, IDC_D172_1081, u->energyPercent, 0, 100));
    u->resource = static_cast<u32>(ReadClampedInt(dlg, IDC_D172_1080,
                                                  static_cast<int>(u->resource), 0, 0x7FFFFF));
    u->stackCount =
        static_cast<u16>(ReadClampedInt(dlg, IDC_D172_1082, u->stackCount, 0, 65535));
    u16 state = 0;
    for (const UnitStateBit& bit : kUnitStateBits) {
        if (::IsDlgButtonChecked(dlg, bit.controlId) == BST_CHECKED) state |= bit.bit;
    }
    u->queueCount = static_cast<u16>((u->queueCount & ~0x001Fu) | state);
    // 勾选的状态同时标记为“可应用”，否则隐形/钻地等在该单位上会被游戏忽略
    u->validState = static_cast<u16>(u->validState | state);

    const bool changed = oldHp != u->hpPercent || oldShield != u->shieldPercent ||
                         oldEnergy != u->energyPercent || oldResource != u->resource ||
                         oldStack != u->stackCount || oldQueue != u->queueCount ||
                         oldValid != u->validState;
    if (changed) doc->MarkDirty();
}

}  // namespace

INT_PTR CALLBACK ProcUnitNames(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            MapDocument* doc = DialogDocument();
            g_names.type = -1;
            g_names.unitIndex = -1;
            FillTypeCombo(dlg, doc);
            EnableUnitFields(dlg, doc != nullptr);
            if (doc) {
                HWND combo = ::GetDlgItem(dlg, IDC_D172_1072);
                const LRESULT sel = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_names.type = static_cast<int>(
                        ::SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(sel), 0));
                }
            }
            FillUnitList(dlg, doc);
            if (!doc) {
                // 只读预览：与旧版一致显示 100%
                SetPercent(dlg, IDC_D172_1078, 100);
                SetPercent(dlg, IDC_D172_1079, 100);
                SetPercent(dlg, IDC_D172_1081, 100);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            MapDocument* doc = DialogDocument();
            if (id == IDC_D172_1072 && code == CBN_SELCHANGE) {
                HWND combo = ::GetDlgItem(dlg, IDC_D172_1072);
                const LRESULT sel = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
                g_names.type =
                    sel == CB_ERR
                        ? -1
                        : static_cast<int>(::SendMessageW(combo, CB_GETITEMDATA,
                                                          static_cast<WPARAM>(sel), 0));
                g_names.unitIndex = -1;
                FillUnitList(dlg, doc);
                return TRUE;
            }
            if (id == IDC_D172_1060 && code == LBN_SELCHANGE) {
                HWND list = ::GetDlgItem(dlg, IDC_D172_1060);
                const int sel = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
                if (!doc || sel == LB_ERR) return TRUE;
                const LRESULT data =
                    ::SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(sel), 0);
                if (data == LB_ERR ||
                    static_cast<std::size_t>(data) >= doc->units().size()) {
                    g_names.unitIndex = -1;
                    return TRUE;
                }
                g_names.unitIndex = static_cast<int>(data);
                LoadUnitFields(dlg, doc->units()[static_cast<std::size_t>(data)]);
                return TRUE;
            }
            if (id == IDC_D172_1078 || id == IDC_D172_1079 || id == IDC_D172_1081 ||
                id == IDC_D172_1080 || id == IDC_D172_1082) {
                if (code == EN_KILLFOCUS) StoreUnitFields(dlg);
                return TRUE;
            }
            if (id >= IDC_D172_1073 && id <= IDC_D172_1077) {   // 5 个状态位
                StoreUnitFields(dlg);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            StoreUnitFields(dlg);
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
