#include "ui/dlg_procs.h"

#include <commctrl.h>
#include <string>
#include <utility>
#include <vector>

#include "common/log.h"
#include "common/string_util.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"

namespace scmd {

namespace {

LocationRect g_location;
int g_selectedTrigger = 0;

// 触发编辑器内部刷新消息：子面板改动后通知 181 重填列表
constexpr UINT kMsgTriggerRefresh = WM_APP + 181;

// 条件类型 1..23（0 表示空槽；>23 为原程序未定义类型）
const wchar_t* const kConditionNames[] = {
    L"",          L"倒计时",    L"命令",      L"带来",      L"积累",
    L"击杀",      L"命令最多",  L"命令最多在位置", L"最多击杀", L"最高分数",
    L"最多资源",  L"开关",      L"经过时间",  L"任务简报",  L"对手",
    L"死亡",      L"命令最少",  L"命令最少在位置", L"最少击杀", L"最低分数",
    L"最少资源",  L"分数",      L"总是",      L"从不",
};
constexpr int kConditionNameCount = static_cast<int>(sizeof(kConditionNames) / sizeof(kConditionNames[0]));

// 行动类型 1..57
const wchar_t* const kActionNames[] = {
    L"",
    L"胜利", L"失败", L"保留触发", L"等待", L"暂停游戏", L"恢复游戏",
    L"传输通讯", L"播放 WAV", L"显示文本", L"居中的视图",
    L"创建带属性单位", L"设置任务目标", L"设置开关", L"设置倒计时",
    L"运行 AI 脚本", L"在位置运行 AI 脚本",
    L"排行榜控制", L"排行榜控制在位置", L"排行榜资源", L"排行榜击杀", L"排行榜分数",
    L"击杀单位", L"在位置击杀单位", L"移除单位", L"在位置移除单位",
    L"设置资源", L"设置分数", L"小地图信号", L"说话头像",
    L"静音单位语音", L"取消静音", L"排行榜电脑玩家",
    L"排行榜目标控制", L"排行榜目标控制在位置", L"排行榜目标资源",
    L"排行榜目标击杀", L"排行榜目标分数",
    L"移动地点", L"移动单位", L"排行榜贪婪",
    L"设置下一场景", L"设置装饰物状态", L"设置无敌",
    L"创建单位", L"设置死亡数", L"下达命令", L"注释",
    L"给予单位", L"修改单位生命", L"修改单位能量", L"修改单位护盾",
    L"修改单位资源量", L"修改单位机库数量", L"暂停计时器", L"恢复计时器",
    L"绘制", L"设置联盟状态",
};
constexpr int kActionNameCount = static_cast<int>(sizeof(kActionNames) / sizeof(kActionNames[0]));

const wchar_t* const kComparisonNames[] = {L"至少", L"至多", L"等于"};

// 181 的「触发」分组框（1113）里嵌入的三个子面板：
//   184 列表视图 / 185 条件编辑 / 186 动作编辑（模板样式本身即 WS_CHILD）
struct TriggerPanels {
    HWND hwnd[3] = {nullptr, nullptr, nullptr};
    int mode = 0;  // 0=原始触发列表；1..3=对应子面板
};

TriggerPanels* PanelsOf(HWND dlg) {
    return reinterpret_cast<TriggerPanels*>(::GetWindowLongPtrW(dlg, GWLP_USERDATA));
}

const wchar_t* ConditionName(u8 type) {
    return type < kConditionNameCount ? kConditionNames[type] : L"未知";
}

const wchar_t* ActionName(u8 type) {
    return type < kActionNameCount ? kActionNames[type] : L"未知";
}

std::wstring PlayerSummary(const Trigger& t) {
    std::wstring out;
    for (int p = 0; p < 27; p++) {
        if (!t.effectivePlayers[p]) continue;
        wchar_t buf[16];
        ::wsprintfW(buf, out.empty() ? L"P%d" : L",P%d", p + 1);
        out += buf;
    }
    return out.empty() ? L"无玩家" : out;
}

std::wstring LocationName(const MapDocument& doc, u32 id) {
    const auto& locs = doc.locations();
    const auto& strs = doc.strings();
    if (id < locs.Count()) {
        const u16 sid = locs.Items()[id].stringId;
        if (sid < strs.Count() && !strs.At(sid).empty()) return Utf8ToWide(strs.At(sid));
    }
    wchar_t buf[24];
    ::wsprintfW(buf, L"#%u", static_cast<unsigned>(id));
    return buf;
}

std::wstring StringPreview(const MapDocument& doc, u32 id) {
    if (id == 0 || id >= doc.strings().Count()) return L"";
    std::wstring text = Utf8ToWide(doc.strings().At(id));
    if (text.size() > 18) text = text.substr(0, 18) + L"…";
    return text;
}

std::wstring ConditionText(const MapDocument& doc, const TriggerCondition& c) {
    wchar_t buf[256];
    ::wsprintfW(buf, L"%s  玩家 %u  数量 %u  单位 %u  地点 %s  比较 %s",
                ConditionName(c.type), static_cast<unsigned>(c.player),
                static_cast<unsigned>(c.amount), static_cast<unsigned>(c.unitType),
                LocationName(doc, c.locationId).c_str(),
                kComparisonNames[c.comparison < 3 ? c.comparison : 0]);
    return buf;
}

std::wstring ActionText(const MapDocument& doc, const TriggerAction& a) {
    const std::wstring text = StringPreview(doc, a.stringId);
    wchar_t buf[320];
    if (text.empty()) {
        ::wsprintfW(buf, L"%s  玩家 %u  数量 %u  单位 %u  地点 %s",
                    ActionName(a.type), static_cast<unsigned>(a.player),
                    static_cast<unsigned>(a.amount), static_cast<unsigned>(a.setting),
                    LocationName(doc, a.locationId).c_str());
    } else {
        ::wsprintfW(buf, L"%s  玩家 %u  数量 %u  单位 %u  地点 %s  文本「%s」",
                    ActionName(a.type), static_cast<unsigned>(a.player),
                    static_cast<unsigned>(a.amount), static_cast<unsigned>(a.setting),
                    LocationName(doc, a.locationId).c_str(), text.c_str());
    }
    return buf;
}

Trigger* SelectedTrigger() {
    MapDocument* doc = DialogDocument();
    if (!doc) return nullptr;
    auto& items = doc->mutableTriggers().MutableItems();
    if (g_selectedTrigger < 0 || static_cast<std::size_t>(g_selectedTrigger) >= items.size()) {
        return nullptr;
    }
    return &items[static_cast<std::size_t>(g_selectedTrigger)];
}

// 子面板放到分组框内部；单位换算走 MapDialogRect，随 DPI/字体变化
void LayoutPanels(HWND dlg, const TriggerPanels& panels) {
    RECT rc = {2, 84, 366, 314};
    ::MapDialogRect(dlg, &rc);
    for (HWND h : panels.hwnd) {
        if (!h) continue;
        ::SetWindowPos(h, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// 面板显示时隐藏会被遮住的原始控件（触发列表 + 左侧树），回到 0 模式再恢复
void ApplyTriggerMode(HWND dlg, TriggerPanels& panels, int mode) {
    panels.mode = mode;
    for (int i = 0; i < 3; i++) {
        if (panels.hwnd[i]) ::ShowWindow(panels.hwnd[i], mode == i + 1 ? SW_SHOW : SW_HIDE);
    }
    const int listShow = mode == 0 ? SW_SHOW : SW_HIDE;
    ::ShowWindow(::GetDlgItem(dlg, IDC_D181_182), listShow);
    ::ShowWindow(::GetDlgItem(dlg, IDC_D181_1110), listShow);
    Log(L"[trigger] view=%d", mode);
}

// 184 的列表视图改成报表模式并建立列
void SetupDetailList(HWND dlg, const TriggerPanels& panels) {
    HWND list = panels.hwnd[0] ? ::GetDlgItem(panels.hwnd[0], IDC_D184_1022) : nullptr;
    if (!list) return;
    LONG_PTR style = ::GetWindowLongPtrW(list, GWL_STYLE);
    style = (style & ~0x0000000F) | LVS_REPORT;
    ::SetWindowLongPtrW(list, GWL_STYLE, style);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    while (ListView_DeleteColumn(list, 0)) {
    }
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.cx = 46;
    col.pszText = const_cast<wchar_t*>(L"类别");
    ListView_InsertColumn(list, 0, &col);
    col.cx = 86;
    col.pszText = const_cast<wchar_t*>(L"名称");
    ListView_InsertColumn(list, 1, &col);
    col.cx = 205;
    col.pszText = const_cast<wchar_t*>(L"内容");
    ListView_InsertColumn(list, 2, &col);
}

void AddDetailRow(HWND list, int row, const wchar_t* kind, const wchar_t* name,
                  const wchar_t* detail) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = const_cast<wchar_t*>(kind);
    ListView_InsertItem(list, &item);
    ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(name));
    ListView_SetItemText(list, row, 2, const_cast<wchar_t*>(detail));
}

void FillDetailList(HWND dlg, const TriggerPanels& panels, const Trigger* t) {
    HWND list = panels.hwnd[0] ? ::GetDlgItem(panels.hwnd[0], IDC_D184_1022) : nullptr;
    if (!list) return;
    ListView_DeleteAllItems(list);
    MapDocument* doc = DialogDocument();
    if (!doc || !t) return;

    int row = 0;
    for (int i = 0; i < t->ConditionCount(); i++) {
        const auto& c = t->conditions[i];
        wchar_t name[16];
        ::wsprintfW(name, L"条件 %d", i + 1);
        wchar_t detail[256];
        ::wsprintfW(detail, L"玩家 %u / 数量 %u / 单位 %u / 地点 %s",
                    static_cast<unsigned>(c.player), static_cast<unsigned>(c.amount),
                    static_cast<unsigned>(c.unitType),
                    LocationName(*doc, c.locationId).c_str());
        AddDetailRow(list, row++, L"条件", name, detail);
    }
    for (int i = 0; i < t->ActionCount(); i++) {
        const auto& a = t->actions[i];
        wchar_t name[16];
        ::wsprintfW(name, L"行动 %d", i + 1);
        wchar_t detail[256];
        ::wsprintfW(detail, L"玩家 %u / 数量 %u / 单位 %u / 地点 %s",
                    static_cast<unsigned>(a.player), static_cast<unsigned>(a.amount),
                    static_cast<unsigned>(a.setting),
                    LocationName(*doc, a.locationId).c_str());
        AddDetailRow(list, row++, L"行动", name, detail);
    }
}

void FillConditionList(HWND list, const Trigger* t) {
    if (!list) return;
    const int keep = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    MapDocument* doc = DialogDocument();
    if (!doc || !t) return;
    const int n = t->ConditionCount();
    for (int i = 0; i < n; i++) {
        wchar_t label[320];
        ::wsprintfW(label, L"[%d] %s", i + 1, ConditionText(*doc, t->conditions[i]).c_str());
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    if (keep >= 0 && keep < n) {
        ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(keep), 0);
    }
}

void FillActionList(HWND list, const Trigger* t) {
    if (!list) return;
    const int keep = static_cast<int>(::SendMessageW(list, LB_GETCURSEL, 0, 0));
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    MapDocument* doc = DialogDocument();
    if (!doc || !t) return;
    const int n = t->ActionCount();
    for (int i = 0; i < n; i++) {
        wchar_t label[384];
        ::wsprintfW(label, L"[%d] %s", i + 1, ActionText(*doc, t->actions[i]).c_str());
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    if (keep >= 0 && keep < n) {
        ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(keep), 0);
    }
}

void FillTriggerList(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_D181_182);
    if (!list) return;
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    MapDocument* doc = DialogDocument();
    if (!doc) return;
    const auto& items = doc->triggers().Items();
    for (std::size_t i = 0; i < items.size(); i++) {
        const Trigger& t = items[i];
        wchar_t label[192];
        ::wsprintfW(label, L"触发 %u  %s  [%d 条件 / %d 行动]%s", static_cast<unsigned>(i),
                    PlayerSummary(t).c_str(), t.ConditionCount(), t.ActionCount(),
                    (t.executionFlags & 0x8) ? L"  已禁用" : L"");
        ::SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    if (!items.empty()) {
        if (g_selectedTrigger < 0) g_selectedTrigger = 0;
        if (static_cast<std::size_t>(g_selectedTrigger) >= items.size()) {
            g_selectedTrigger = static_cast<int>(items.size()) - 1;
        }
        ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(g_selectedTrigger), 0);
    } else {
        g_selectedTrigger = 0;
    }
}

void RefreshPanels(HWND dlg, const TriggerPanels& panels) {
    const Trigger* t = SelectedTrigger();
    FillDetailList(dlg, panels, t);
    FillConditionList(panels.hwnd[1] ? ::GetDlgItem(panels.hwnd[1], IDC_D185_1123) : nullptr, t);
    FillActionList(panels.hwnd[2] ? ::GetDlgItem(panels.hwnd[2], IDC_D186_1115) : nullptr, t);
}

void RefreshAll(HWND dlg, const TriggerPanels& panels) {
    FillTriggerList(dlg);
    RefreshPanels(dlg, panels);
}

void FillPlayerTree(HWND dlg) {
    HWND tree = ::GetDlgItem(dlg, IDC_D181_1110);
    if (!tree) return;
    TreeView_DeleteAllItems(tree);
    for (int p = 1; p <= 8; p++) {
        TVINSERTSTRUCTW ins = {};
        ins.hParent = TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT;
        wchar_t label[32];
        ::wsprintfW(label, L"玩家 %d", p);
        ins.item.pszText = label;
        TreeView_InsertItem(tree, &ins);
    }
}

// ── 条件/行动编辑弹窗 ──
TriggerCondition g_condSeed;
TriggerAction g_actSeed;
bool g_condAccepted = false;
bool g_actAccepted = false;

u32 ReadU32Field(HWND dlg, int id, u32 fallback) {
    BOOL ok = FALSE;
    const UINT v = ::GetDlgItemInt(dlg, id, &ok, FALSE);
    return ok ? static_cast<u32>(v) : fallback;
}

bool EditConditionDialog(HWND owner, TriggerCondition& cond) {
    g_condSeed = cond;
    g_condAccepted = false;
    ::DialogBoxParamW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TRIG_COND), owner,
                      &ProcTriggerCondition, 0);
    if (!g_condAccepted) return false;
    cond = g_condSeed;
    return true;
}

bool EditActionDialog(HWND owner, TriggerAction& action) {
    g_actSeed = action;
    g_actAccepted = false;
    ::DialogBoxParamW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_TRIG_ACT), owner,
                      &ProcTriggerAction, 0);
    if (!g_actAccepted) return false;
    action = g_actSeed;
    return true;
}

void NotifyChanged(HWND dlg) {
    if (MapDocument* doc = DialogDocument()) doc->MarkDirty();
    ::PostMessageW(::GetParent(dlg), kMsgTriggerRefresh, 0, 0);
}

// 185/186 共用：列表 + 新建/编辑/复制/删除/上移/下移
INT_PTR HandleListEditor(HWND dlg, UINT msg, WPARAM wParam) {
    const bool isCondition = ::GetDlgItem(dlg, IDC_D185_1123) != nullptr;
    const int listId = isCondition ? IDC_D185_1123 : IDC_D186_1115;

    auto refresh = [&] {
        Trigger* t = SelectedTrigger();
        if (isCondition) FillConditionList(::GetDlgItem(dlg, listId), t);
        else FillActionList(::GetDlgItem(dlg, listId), t);
    };

    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            refresh();
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case 1108: {                                       // 新建
                    Trigger* t = SelectedTrigger();
                    if (!t) return TRUE;
                    if (isCondition) {
                        const int n = t->ConditionCount();
                        if (n >= 16) { MessageBeep(MB_ICONWARNING); return TRUE; }
                        t->conditions[n] = TriggerCondition();
                        t->conditions[n].type = 22;                // 默认「总是」
                        refresh();
                        ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                       static_cast<WPARAM>(n), 0);
                    } else {
                        const int n = t->ActionCount();
                        if (n >= 64) { MessageBeep(MB_ICONWARNING); return TRUE; }
                        t->actions[n] = TriggerAction();
                        t->actions[n].type = 1;                    // 默认「胜利」
                        t->actions[n].amount = 1;
                        refresh();
                        ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                       static_cast<WPARAM>(n), 0);
                    }
                    NotifyChanged(dlg);
                    return TRUE;
                }
                case 1106: {                                       // 编辑
                    Trigger* t = SelectedTrigger();
                    const int sel = ListSelectedIndex(dlg, listId);
                    if (!t || sel < 0) return TRUE;
                    if (isCondition) {
                        if (sel >= t->ConditionCount()) return TRUE;
                        if (EditConditionDialog(dlg, t->conditions[sel])) {
                            refresh();
                            ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                           static_cast<WPARAM>(sel), 0);
                            NotifyChanged(dlg);
                        }
                    } else {
                        if (sel >= t->ActionCount()) return TRUE;
                        if (EditActionDialog(dlg, t->actions[sel])) {
                            refresh();
                            ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                           static_cast<WPARAM>(sel), 0);
                            NotifyChanged(dlg);
                        }
                    }
                    return TRUE;
                }
                case 1109: {                                       // 复制
                    Trigger* t = SelectedTrigger();
                    const int sel = ListSelectedIndex(dlg, listId);
                    if (!t || sel < 0) return TRUE;
                    if (isCondition) {
                        const int n = t->ConditionCount();
                        if (n >= 16 || sel >= n) { MessageBeep(MB_ICONWARNING); return TRUE; }
                        for (int i = n; i > sel + 1; i--) t->conditions[i] = t->conditions[i - 1];
                        t->conditions[sel + 1] = t->conditions[sel];
                        refresh();
                        ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                       static_cast<WPARAM>(sel + 1), 0);
                    } else {
                        const int n = t->ActionCount();
                        if (n >= 64 || sel >= n) { MessageBeep(MB_ICONWARNING); return TRUE; }
                        for (int i = n; i > sel + 1; i--) t->actions[i] = t->actions[i - 1];
                        t->actions[sel + 1] = t->actions[sel];
                        refresh();
                        ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                       static_cast<WPARAM>(sel + 1), 0);
                    }
                    NotifyChanged(dlg);
                    return TRUE;
                }
                case 1107: {                                       // 删除
                    Trigger* t = SelectedTrigger();
                    const int sel = ListSelectedIndex(dlg, listId);
                    if (!t || sel < 0) return TRUE;
                    if (isCondition) {
                        const int n = t->ConditionCount();
                        if (sel >= n) return TRUE;
                        for (int i = sel; i < n - 1; i++) t->conditions[i] = t->conditions[i + 1];
                        t->ClearCondition(n - 1);
                        refresh();
                    } else {
                        const int n = t->ActionCount();
                        if (sel >= n) return TRUE;
                        for (int i = sel; i < n - 1; i++) t->actions[i] = t->actions[i + 1];
                        t->ClearAction(n - 1);
                        refresh();
                    }
                    NotifyChanged(dlg);
                    return TRUE;
                }
                case 1001:                                         // 上移
                case 1002: {                                       // 下移
                    Trigger* t = SelectedTrigger();
                    const int sel = ListSelectedIndex(dlg, listId);
                    const int delta = LOWORD(wParam) == 1001 ? -1 : 1;
                    if (!t || sel < 0) return TRUE;
                    if (isCondition) {
                        const int n = t->ConditionCount();
                        if (sel + delta < 0 || sel + delta >= n) return TRUE;
                        std::swap(t->conditions[sel], t->conditions[sel + delta]);
                        refresh();
                    } else {
                        const int n = t->ActionCount();
                        if (sel + delta < 0 || sel + delta >= n) return TRUE;
                        std::swap(t->actions[sel], t->actions[sel + delta]);
                        refresh();
                    }
                    ::SendMessageW(::GetDlgItem(dlg, listId), LB_SETCURSEL,
                                   static_cast<WPARAM>(sel + delta), 0);
                    NotifyChanged(dlg);
                    return TRUE;
                }
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

}  // namespace

const LocationRect& LastLocation() { return g_location; }

// 181：触发编辑器。列表/条件/行动全部接到 MapDocument::triggers。
INT_PTR CALLBACK ProcTriggerEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            auto* panels = new TriggerPanels();
            ::SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(panels));
            const HINSTANCE inst =
                reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(dlg, GWLP_HINSTANCE));
            panels->hwnd[0] = ::CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_DLG_184), dlg,
                                                   &ProcListViewHost, 0);
            panels->hwnd[1] = ::CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_DLG_185), dlg,
                                                   &ProcListEditorA, 0);
            panels->hwnd[2] = ::CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_DLG_186), dlg,
                                                   &ProcListEditorB, 0);
            LayoutPanels(dlg, *panels);
            ApplyTriggerMode(dlg, *panels, 0);
            SetupDetailList(dlg, *panels);
            FillPlayerTree(dlg);
            RefreshAll(dlg, *panels);
            return TRUE;
        }
        case kMsgTriggerRefresh: {
            TriggerPanels* panels = PanelsOf(dlg);
            if (panels) RefreshAll(dlg, *panels);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D181_182:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        const int sel = ListSelectedIndex(dlg, IDC_D181_182);
                        if (sel >= 0) {
                            g_selectedTrigger = sel;
                            if (TriggerPanels* p = PanelsOf(dlg)) RefreshPanels(dlg, *p);
                        }
                        return TRUE;
                    }
                    break;
                case IDC_D181_1101: {                            // 修改：在原始列表/184/185/186 间切换
                    TriggerPanels* panels = PanelsOf(dlg);
                    if (panels) ApplyTriggerMode(dlg, *panels, (panels->mode + 1) % 4);
                    return TRUE;
                }
                case IDC_D181_1100: {                            // 新建
                    MapDocument* doc = DialogDocument();
                    if (!doc) return TRUE;
                    auto& items = doc->mutableTriggers().MutableItems();
                    Trigger t;
                    t.conditions[0].type = 22;                   // 总是
                    t.effectivePlayers[0] = 1;
                    items.push_back(t);
                    g_selectedTrigger = static_cast<int>(items.size()) - 1;
                    doc->MarkDirty();
                    if (TriggerPanels* p = PanelsOf(dlg)) RefreshAll(dlg, *p);
                    return TRUE;
                }
                case IDC_D181_1105: {                            // 复制
                    MapDocument* doc = DialogDocument();
                    const Trigger* src = SelectedTrigger();
                    if (!doc || !src) return TRUE;
                    auto& items = doc->mutableTriggers().MutableItems();
                    Trigger copy = *src;
                    const int at = g_selectedTrigger + 1;
                    items.insert(items.begin() + at, copy);
                    g_selectedTrigger = at;
                    doc->MarkDirty();
                    if (TriggerPanels* p = PanelsOf(dlg)) RefreshAll(dlg, *p);
                    return TRUE;
                }
                case IDC_D181_1104: {                            // 删除
                    MapDocument* doc = DialogDocument();
                    if (!doc) return TRUE;
                    auto& items = doc->mutableTriggers().MutableItems();
                    if (g_selectedTrigger < 0 || static_cast<std::size_t>(g_selectedTrigger) >= items.size()) {
                        return TRUE;
                    }
                    items.erase(items.begin() + g_selectedTrigger);
                    if (g_selectedTrigger >= static_cast<int>(items.size())) {
                        g_selectedTrigger = static_cast<int>(items.size()) - 1;
                    }
                    doc->MarkDirty();
                    if (TriggerPanels* p = PanelsOf(dlg)) RefreshAll(dlg, *p);
                    return TRUE;
                }
                case IDC_D181_1001:                              // 上移
                case IDC_D181_1002: {                            // 下移
                    MapDocument* doc = DialogDocument();
                    if (!doc) return TRUE;
                    auto& items = doc->mutableTriggers().MutableItems();
                    const int delta = LOWORD(wParam) == IDC_D181_1001 ? -1 : 1;
                    const int dst = g_selectedTrigger + delta;
                    if (g_selectedTrigger < 0 || dst < 0 ||
                        static_cast<std::size_t>(dst) >= items.size()) {
                        return TRUE;
                    }
                    std::swap(items[g_selectedTrigger], items[dst]);
                    g_selectedTrigger = dst;
                    doc->MarkDirty();
                    if (TriggerPanels* p = PanelsOf(dlg)) RefreshAll(dlg, *p);
                    return TRUE;
                }
                case IDC_D181_1102:                              // 开关命名
                    Log(L"[trigger] 开关命名暂未接入（SWNM）");
                    return TRUE;
                case IDC_D181_1:                                 // 关闭
                    ::EndDialog(dlg, IDCANCEL);
                    return TRUE;
                default:
                    break;
            }
            break;
        case WM_DESTROY: {
            TriggerPanels* panels = PanelsOf(dlg);
            ::SetWindowLongPtrW(dlg, GWLP_USERDATA, 0);
            delete panels;                                   // 子面板随父对话框销毁
            return TRUE;
        }
        case WM_CLOSE:
            ::EndDialog(dlg, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

// 193：编辑条件
INT_PTR CALLBACK ProcTriggerCondition(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            HWND combo = ::GetDlgItem(dlg, IDC_D193_TYPE);
            for (int i = 1; i < kConditionNameCount; i++) {
                ::SendMessageW(combo, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(kConditionNames[i]));
            }
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自定义"));
            const u32 type = g_condSeed.type;
            const int sel = (type >= 1 && type <= 23) ? static_cast<int>(type) - 1 : 23;
            ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
            ::SetDlgItemInt(dlg, IDC_D193_TYPE_ID, type, FALSE);
            ::SetDlgItemInt(dlg, IDC_D193_LOCATION, g_condSeed.locationId, FALSE);
            ::SetDlgItemInt(dlg, IDC_D193_PLAYER, g_condSeed.player, FALSE);
            ::SetDlgItemInt(dlg, IDC_D193_AMOUNT, g_condSeed.amount, FALSE);
            ::SetDlgItemInt(dlg, IDC_D193_UNIT, g_condSeed.unitType, FALSE);
            ::SetDlgItemInt(dlg, IDC_D193_PARAM, g_condSeed.resourceType, FALSE);
            ::SetDlgItemInt(dlg, IDC_D193_PROP, g_condSeed.property, FALSE);
            HWND cmp = ::GetDlgItem(dlg, IDC_D193_COMPARE);
            for (const wchar_t* name : kComparisonNames) {
                ::SendMessageW(cmp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            }
            ::SendMessageW(cmp, CB_SETCURSEL,
                           static_cast<WPARAM>(g_condSeed.comparison < 3 ? g_condSeed.comparison : 0),
                           0);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D193_1) {
                const int sel = static_cast<int>(
                    ::SendDlgItemMessageW(dlg, IDC_D193_TYPE, CB_GETCURSEL, 0, 0));
                u32 type = 0;
                if (sel >= 0 && sel < 23) {
                    type = static_cast<u32>(sel + 1);
                } else {
                    type = ReadU32Field(dlg, IDC_D193_TYPE_ID, g_condSeed.type);
                }
                if (type == 0) type = 1;
                g_condSeed.type = static_cast<u8>(type);
                g_condSeed.locationId = ReadU32Field(dlg, IDC_D193_LOCATION, g_condSeed.locationId);
                g_condSeed.player = ReadU32Field(dlg, IDC_D193_PLAYER, g_condSeed.player);
                g_condSeed.amount = ReadU32Field(dlg, IDC_D193_AMOUNT, g_condSeed.amount);
                g_condSeed.unitType = static_cast<u16>(ReadU32Field(dlg, IDC_D193_UNIT, g_condSeed.unitType));
                g_condSeed.resourceType = static_cast<u8>(ReadU32Field(dlg, IDC_D193_PARAM, g_condSeed.resourceType));
                g_condSeed.property = static_cast<u8>(ReadU32Field(dlg, IDC_D193_PROP, g_condSeed.property));
                const int cmp = static_cast<int>(
                    ::SendDlgItemMessageW(dlg, IDC_D193_COMPARE, CB_GETCURSEL, 0, 0));
                g_condSeed.comparison = static_cast<u8>(cmp >= 0 && cmp < 3 ? cmp : 0);
                g_condAccepted = true;
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D193_2) {
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

// 194：编辑行动
INT_PTR CALLBACK ProcTriggerAction(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            HWND combo = ::GetDlgItem(dlg, IDC_D194_TYPE);
            for (int i = 1; i < kActionNameCount; i++) {
                ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kActionNames[i]));
            }
            ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自定义"));
            const u32 type = g_actSeed.type;
            const int sel = (type >= 1 && type <= 57) ? static_cast<int>(type) - 1 : 57;
            ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
            ::SetDlgItemInt(dlg, IDC_D194_TYPE_ID, type, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_LOCATION, g_actSeed.locationId, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_STRING, g_actSeed.stringId, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_WAV, g_actSeed.wavId, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_TIME, g_actSeed.time, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_PLAYER, g_actSeed.player, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_TARGET, g_actSeed.target, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_UNIT, g_actSeed.setting, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_AMOUNT, g_actSeed.amount, FALSE);
            ::SetDlgItemInt(dlg, IDC_D194_PROP, g_actSeed.property, FALSE);
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D194_1) {
                const int sel = static_cast<int>(
                    ::SendDlgItemMessageW(dlg, IDC_D194_TYPE, CB_GETCURSEL, 0, 0));
                u32 type = 0;
                if (sel >= 0 && sel < 57) {
                    type = static_cast<u32>(sel + 1);
                } else {
                    type = ReadU32Field(dlg, IDC_D194_TYPE_ID, g_actSeed.type);
                }
                if (type == 0) type = 1;
                g_actSeed.type = static_cast<u8>(type);
                g_actSeed.locationId = ReadU32Field(dlg, IDC_D194_LOCATION, g_actSeed.locationId);
                g_actSeed.stringId = ReadU32Field(dlg, IDC_D194_STRING, g_actSeed.stringId);
                g_actSeed.wavId = ReadU32Field(dlg, IDC_D194_WAV, g_actSeed.wavId);
                g_actSeed.time = ReadU32Field(dlg, IDC_D194_TIME, g_actSeed.time);
                g_actSeed.player = ReadU32Field(dlg, IDC_D194_PLAYER, g_actSeed.player);
                g_actSeed.target = ReadU32Field(dlg, IDC_D194_TARGET, g_actSeed.target);
                g_actSeed.setting = static_cast<u16>(ReadU32Field(dlg, IDC_D194_UNIT, g_actSeed.setting));
                g_actSeed.amount = static_cast<u8>(ReadU32Field(dlg, IDC_D194_AMOUNT, g_actSeed.amount));
                g_actSeed.property = static_cast<u8>(ReadU32Field(dlg, IDC_D194_PROP, g_actSeed.property));
                g_actAccepted = true;
                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D194_2) {
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

// 185/186 共用实现（处理过程在 dlg_misc.cpp，转发到这里）
INT_PTR HandleTriggerListEditor(HWND dlg, UINT msg, WPARAM wParam) {
    return HandleListEditor(dlg, msg, wParam);
}

// 171：编辑 Location。六个地形高度按钮 + 四边坐标。
INT_PTR CALLBACK ProcEditLocation(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D171_1079, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1080, 0, FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1081, 16, FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1082, 16, FALSE);
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_D171_1065: g_location.elevation = 0; return TRUE;   // 凹陷
                case IDC_D171_1066: g_location.elevation = 1; return TRUE;   // 平地
                case IDC_D171_1067: g_location.elevation = 2; return TRUE;   // 高地
                case IDC_D171_1069: g_location.elevation = 3; return TRUE;   // 较低空中
                case IDC_D171_1068: g_location.elevation = 4; return TRUE;   // 普通空中
                case IDC_D171_1070: g_location.elevation = 5; return TRUE;   // 较高空中
                case IDC_D171_1:                                             // 确定
                    g_location.left = ReadClampedInt(dlg, IDC_D171_1079, 0, 0, 255);
                    g_location.top = ReadClampedInt(dlg, IDC_D171_1080, 0, 0, 255);
                    g_location.right = ReadClampedInt(dlg, IDC_D171_1081, 16, 0, 255);
                    g_location.bottom = ReadClampedInt(dlg, IDC_D171_1082, 16, 0, 255);
                    Log(L"[location] rect=(%d,%d)-(%d,%d) elev=%d", g_location.left, g_location.top,
                        g_location.right, g_location.bottom, g_location.elevation);
                    ::EndDialog(dlg, IDOK);
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

}  // namespace scmd
