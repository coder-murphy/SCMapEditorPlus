#include "ui/dlg_procs.h"

#include <commctrl.h>

#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

#include "common/log.h"
#include "common/string_util.h"
#include "app/app_settings.h"
#include "map/map_document.h"
#include "res/control_ids.h"
#include "res/resource.h"
#include "ui/dlg_common.h"
#include "ui/dlg_types.h"
#include "ui/dialogs.h"

namespace scmd {

namespace {

LocationRect g_location;
int g_selectedTrigger = 0;
int g_playerFilter = -1;   // -1 = 全部玩家；0..7 = 仅该玩家的触发

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
        if (sid < strs.Count() && !strs.At(sid).empty()) {
            std::wstring text = Utf8ToWide(strs.At(sid));
            // 真实地图的地点名可能极长（实测出现过 2048 字），必须截断，
            // 否则下游 ConditionText/ActionText/FillDetailList 的固定栈缓冲会溢出
            if (text.size() > 24) text = text.substr(0, 21) + L"…";
            return text;
        }
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
    ::ShowWindow(::GetDlgItem(dlg, IDC_D181_TRIGGER_LIST), listShow);
    ::ShowWindow(::GetDlgItem(dlg, IDC_D181_PLAYER_TREE), listShow);
    Log(L"[trigger] view=%d", mode);
}

// 184 的列表视图改成报表模式并建立列
void SetupDetailList(HWND dlg, const TriggerPanels& panels) {
    HWND list = panels.hwnd[0] ? ::GetDlgItem(panels.hwnd[0], IDC_D184_DETAIL_LIST) : nullptr;
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
    HWND list = panels.hwnd[0] ? ::GetDlgItem(panels.hwnd[0], IDC_D184_DETAIL_LIST) : nullptr;
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

// 列表行 -> 实际触发下标（筛选后行号不再等于触发下标）
int TriggerIndexAtRow(HWND list, int row) {
    const LRESULT data = ::SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
    return data == LB_ERR ? -1 : static_cast<int>(data);
}

// 选中指定实际触发；返回是否找到
bool SelectTriggerRow(HWND list, int triggerIndex) {
    const int count = static_cast<int>(::SendMessageW(list, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; i++) {
        if (TriggerIndexAtRow(list, i) == triggerIndex) {
            ::SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return true;
        }
    }
    return false;
}

void FillTriggerList(HWND dlg) {
    HWND list = ::GetDlgItem(dlg, IDC_D181_TRIGGER_LIST);
    if (!list) return;
    ::SendMessageW(list, LB_RESETCONTENT, 0, 0);
    MapDocument* doc = DialogDocument();
    if (!doc) return;
    const auto& items = doc->triggers().Items();
    int row = 0;
    for (std::size_t i = 0; i < items.size(); i++) {
        const Trigger& t = items[i];
        if (g_playerFilter >= 0 && !t.HasPlayer(g_playerFilter)) continue;
        // 避免 wsprintfW 的 %s 宽窄字符串歧义：直接拼接 std::wstring
        std::wstring label;
        if (AppSettings::Instance().triggerBaseNames()) {
            // “触发使用基本名称”（155-1093）：只显示基础名称
            label = L"触发 " + std::to_wstring(i);
        } else {
            label = L"触发 " + std::to_wstring(i) + L"  " + PlayerSummary(t) +
                    L"  [" + std::to_wstring(t.ConditionCount()) + L" 条件 / " +
                    std::to_wstring(t.ActionCount()) + L" 行动]" +
                    ((t.executionFlags & 0x8) ? L"  已禁用" : L"");
        }
        const LRESULT at = ::SendMessageW(list, LB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(label.c_str()));
        if (at != LB_ERR) {
            ::SendMessageW(list, LB_SETITEMDATA, static_cast<WPARAM>(at),
                           static_cast<LPARAM>(i));
        }
        row++;
    }
    if (row > 0) {
        if (!SelectTriggerRow(list, g_selectedTrigger)) {
            ::SendMessageW(list, LB_SETCURSEL, 0, 0);
            g_selectedTrigger = TriggerIndexAtRow(list, 0);
        }
    } else {
        g_selectedTrigger = -1;
    }
}

void RefreshPanels(HWND dlg, const TriggerPanels& panels) {
    const Trigger* t = SelectedTrigger();
    FillDetailList(dlg, panels, t);
    FillConditionList(panels.hwnd[1] ? ::GetDlgItem(panels.hwnd[1], IDC_D185_COND_LIST) : nullptr, t);
    FillActionList(panels.hwnd[2] ? ::GetDlgItem(panels.hwnd[2], IDC_D186_ACTION_LIST) : nullptr, t);
}

void RefreshAll(HWND dlg, const TriggerPanels& panels) {
    FillTriggerList(dlg);
    RefreshPanels(dlg, panels);
}

void FillPlayerTree(HWND dlg) {
    HWND tree = ::GetDlgItem(dlg, IDC_D181_PLAYER_TREE);
    if (!tree) return;
    TreeView_DeleteAllItems(tree);
    // 第一项“全”= 不筛选；随后 1..8 对应玩家 0..7（控件很窄，使用短标签）
    const wchar_t* labels[] = {L"全", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8"};
    for (int i = 0; i < 9; i++) {
        TVINSERTSTRUCTW ins = {};
        ins.hParent = TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = const_cast<wchar_t*>(labels[i]);
        ins.item.lParam = (i == 0) ? -1 : i - 1;
        TreeView_InsertItem(tree, &ins);
    }
    if (HTREEITEM root = TreeView_GetRoot(tree)) {
        TreeView_SelectItem(tree, root);
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
    const bool isCondition = ::GetDlgItem(dlg, IDC_D185_COND_LIST) != nullptr;
    const int listId = isCondition ? IDC_D185_COND_LIST : IDC_D186_ACTION_LIST;

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
                case IDC_TRIGLIST_NEW: {                           // 新建
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
                case IDC_TRIGLIST_EDIT: {                          // 编辑
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
                case IDC_TRIGLIST_COPY: {                          // 复制
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
                case IDC_TRIGLIST_DELETE: {                        // 删除
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
                case IDC_TRIGLIST_UP:                              // 上移
                case IDC_TRIGLIST_DOWN: {                          // 下移
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
void SeedLocation(const LocationRect& seed) { g_location = seed; }
const TriggerCondition& LastTriggerCondition() { return g_condSeed; }
bool LastTriggerConditionAccepted() { return g_condAccepted; }
const TriggerAction& LastTriggerAction() { return g_actSeed; }
bool LastTriggerActionAccepted() { return g_actAccepted; }

// 181：触发编辑器。列表/条件/行动全部接到 MapDocument::triggers。
INT_PTR CALLBACK ProcTriggerEditor(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            g_playerFilter = -1;
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
                case IDC_D181_TRIGGER_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        const int row = ListSelectedIndex(dlg, IDC_D181_TRIGGER_LIST);
                        if (row >= 0) {
                            const int index = TriggerIndexAtRow(
                                ::GetDlgItem(dlg, IDC_D181_TRIGGER_LIST), row);
                            if (index >= 0) {
                                g_selectedTrigger = index;
                                if (TriggerPanels* p = PanelsOf(dlg)) RefreshPanels(dlg, *p);
                            }
                        }
                        return TRUE;
                    }
                    break;
                case IDC_D181_MODIFY: {                            // 修改：在原始列表/184/185/186 间切换
                    TriggerPanels* panels = PanelsOf(dlg);
                    if (panels) ApplyTriggerMode(dlg, *panels, (panels->mode + 1) % 4);
                    return TRUE;
                }
                case IDC_D181_NEW: {                            // 新建
                    MapDocument* doc = DialogDocument();
                    if (!doc) return TRUE;
                    auto& items = doc->mutableTriggers().MutableItems();
                    Trigger t;
                    t.conditions[0].type = 22;                   // 总是
                    t.effectivePlayers[0] = 1;
                    if (g_playerFilter >= 0 && g_playerFilter < 27) {
                        t.effectivePlayers[g_playerFilter] = 1;  // 保持当前筛选下可见
                    }
                    items.push_back(t);
                    g_selectedTrigger = static_cast<int>(items.size()) - 1;
                    doc->MarkDirty();
                    if (TriggerPanels* p = PanelsOf(dlg)) RefreshAll(dlg, *p);
                    return TRUE;
                }
                case IDC_D181_COPY: {                            // 复制
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
                case IDC_D181_DELETE: {                            // 删除
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
                case IDC_D181_UP:                              // 上移
                case IDC_D181_DOWN: {                            // 下移
                    MapDocument* doc = DialogDocument();
                    if (!doc) return TRUE;
                    auto& items = doc->mutableTriggers().MutableItems();
                    const int delta = LOWORD(wParam) == IDC_D181_UP ? -1 : 1;
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
                case IDC_D181_SWITCH_NAMES:                              // 开关命名
                    ShowRecoveredDialog(IDD_DLG_179, dlg);
                    return TRUE;
                case IDC_D181_CLOSE:                                 // 关闭
                    ::EndDialog(dlg, IDCANCEL);
                    return TRUE;
                default:
                    break;
            }
            break;
        case WM_NOTIFY: {
            LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
            if (hdr && hdr->idFrom == IDC_D181_PLAYER_TREE &&
                hdr->code == TVN_SELCHANGEDW) {
                auto* tv = reinterpret_cast<LPNMTREEVIEWW>(lParam);
                TVITEMW item = {};
                item.mask = TVIF_PARAM;
                item.hItem = tv->itemNew.hItem;
                if (TreeView_GetItem(hdr->hwndFrom, &item)) {
                    g_playerFilter = static_cast<int>(item.lParam);
                    FillTriggerList(dlg);
                    if (TriggerPanels* p = PanelsOf(dlg)) RefreshPanels(dlg, *p);
                }
            }
            break;
        }
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

// 193：类型下拉 <-> 类型号 双向同步（最后一项为“自定义”）
void SyncConditionTypeId(HWND dlg) {
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D193_TYPE, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return;
    if (sel < kConditionNameCount - 1) {
        ::SetDlgItemInt(dlg, IDC_D193_TYPE_ID, static_cast<UINT>(sel) + 1, FALSE);
    }
}

void SyncConditionTypeCombo(HWND dlg) {
    BOOL ok = FALSE;
    const UINT id = ::GetDlgItemInt(dlg, IDC_D193_TYPE_ID, &ok, FALSE);
    if (!ok) return;
    if (id >= 1 && id < static_cast<UINT>(kConditionNameCount)) {
        ::SendDlgItemMessageW(dlg, IDC_D193_TYPE, CB_SETCURSEL, id - 1, 0);
    } else {
        ::SendDlgItemMessageW(dlg, IDC_D193_TYPE, CB_SETCURSEL,
                              static_cast<WPARAM>(kConditionNameCount - 1), 0);
    }
}

// 194：类型下拉 <-> 类型号 双向同步
void SyncActionTypeId(HWND dlg) {
    const LRESULT sel = ::SendDlgItemMessageW(dlg, IDC_D194_TYPE, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return;
    if (sel < kActionNameCount - 1) {
        ::SetDlgItemInt(dlg, IDC_D194_TYPE_ID, static_cast<UINT>(sel) + 1, FALSE);
    }
}

void SyncActionTypeCombo(HWND dlg) {
    BOOL ok = FALSE;
    const UINT id = ::GetDlgItemInt(dlg, IDC_D194_TYPE_ID, &ok, FALSE);
    if (!ok) return;
    if (id >= 1 && id < static_cast<UINT>(kActionNameCount)) {
        ::SendDlgItemMessageW(dlg, IDC_D194_TYPE, CB_SETCURSEL, id - 1, 0);
    } else {
        ::SendDlgItemMessageW(dlg, IDC_D194_TYPE, CB_SETCURSEL,
                              static_cast<WPARAM>(kActionNameCount - 1), 0);
    }
}

// 193：按条件类型启用有意义的字段（掩码为本工程解释，见 doc/05-ui/ui-map.md）
void ApplyConditionFields(HWND dlg, u32 type) {
    enum : unsigned {
        kLoc = 1u, kPlayer = 2u, kAmount = 4u, kUnit = 8u,
        kParam = 16u, kCompare = 32u,
    };
    unsigned mask = kLoc | kPlayer | kAmount | kUnit | kParam | kCompare;   // 自定义/未知：全开
    switch (type) {
        case 1:                                     // 倒计时
        case 12:                                    // 经过时间
            mask = kAmount | kCompare;
            break;
        case 2: case 3: case 5: case 6: case 15: case 16:   // 命令/带来/击杀/死亡
            mask = kPlayer | kAmount | kUnit;
            break;
        case 4: case 10: case 20:                   // 积累/最多资源/最少资源
            mask = kPlayer | kAmount | kParam;
            break;
        case 7: case 17:                            // 命令最多/最少在位置
            mask = kPlayer | kAmount | kUnit | kLoc;
            break;
        case 8: case 18:                            // 最多击杀/最少击杀
        case 14:                                    // 对手
            mask = kPlayer | kAmount;
            break;
        case 9: case 19: case 21:                   // 最高分数/最低分数/分数
            mask = kPlayer | kAmount | kCompare;
            break;
        case 11:                                    // 开关
            mask = kParam | kCompare;
            break;
        case 13: case 22: case 23:                  // 任务简报/总是/从不：无参数
            mask = 0;
            break;
        default:
            break;
    }
    ::EnableWindow(::GetDlgItem(dlg, IDC_D193_LOCATION), (mask & kLoc) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D193_PLAYER), (mask & kPlayer) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D193_AMOUNT), (mask & kAmount) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D193_UNIT), (mask & kUnit) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D193_PARAM), (mask & kParam) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D193_COMPARE), (mask & kCompare) ? TRUE : FALSE);
}

u32 CurrentConditionType(HWND dlg) {
    const int sel = static_cast<int>(
        ::SendDlgItemMessageW(dlg, IDC_D193_TYPE, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < 23) return static_cast<u32>(sel + 1);
    return 0;   // 自定义：类型号可手填，字段全部开放
}

// 194：仅对参数含义完全明确的行动收窄字段；其余保持全开
void ApplyActionFields(HWND dlg, u32 type) {
    enum : unsigned {
        kLoc = 1u, kStr = 2u, kWav = 4u, kTime = 8u,
        kPlayer = 16u, kTarget = 32u, kUnit = 64u, kAmount = 128u,
    };
    unsigned mask = kLoc | kStr | kWav | kTime | kPlayer | kTarget | kUnit | kAmount;
    switch (type) {
        case 4:                                     // 等待：只用到时间
            mask = kTime;
            break;
        case 49:                                    // 注释：只有文本
            mask = kStr;
            break;
        case 9:                                     // 显示文本
            mask = kStr;
            break;
        case 8:                                     // 播放 WAV
            mask = kWav;
            break;
        default:
            break;
    }
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_LOCATION), (mask & kLoc) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_STRING), (mask & kStr) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_WAV), (mask & kWav) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_TIME), (mask & kTime) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_PLAYER), (mask & kPlayer) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_TARGET), (mask & kTarget) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_UNIT), (mask & kUnit) ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(dlg, IDC_D194_AMOUNT), (mask & kAmount) ? TRUE : FALSE);
}

u32 CurrentActionType(HWND dlg) {
    const int sel = static_cast<int>(
        ::SendDlgItemMessageW(dlg, IDC_D194_TYPE, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < 57) return static_cast<u32>(sel + 1);
    return 0;   // 自定义：类型号可手填，字段全部开放
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
            ApplyConditionFields(dlg, CurrentConditionType(dlg));
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D193_TYPE && HIWORD(wParam) == CBN_SELCHANGE) {
                SyncConditionTypeId(dlg);
                ApplyConditionFields(dlg, CurrentConditionType(dlg));
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D193_TYPE_ID && HIWORD(wParam) == EN_CHANGE) {
                SyncConditionTypeCombo(dlg);
                ApplyConditionFields(dlg, CurrentConditionType(dlg));
                return TRUE;
            }
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
            ApplyActionFields(dlg, CurrentActionType(dlg));
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D194_TYPE && HIWORD(wParam) == CBN_SELCHANGE) {
                SyncActionTypeId(dlg);
                ApplyActionFields(dlg, CurrentActionType(dlg));
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D194_TYPE_ID && HIWORD(wParam) == EN_CHANGE) {
                SyncActionTypeCombo(dlg);
                ApplyActionFields(dlg, CurrentActionType(dlg));
                return TRUE;
            }
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

// 206：触发文本视图（可编辑）。格式为本工程自定义：
//   T players=1,2 flags=0
//   C type=1 player=0 amount=100 unit=0 loc=0 cmp=0 res=0 prop=0
//   A type=9 player=0 amount=0 unit=0 loc=0 str=1 wav=0 time=0 target=0 prop=0
// 每行 # 之后为注释；点「应用」整体重建 TRIG 并进入撤销栈。
namespace {

std::wstring PlayersText(const Trigger& t) {
    std::wstring out;
    for (int p = 0; p < 27; p++) {
        if (!t.effectivePlayers[p]) continue;
        if (!out.empty()) out += L",";
        out += std::to_wstring(p + 1);
    }
    return out.empty() ? L"none" : out;
}

std::wstring BuildTriggerText(const MapDocument& doc) {
    const auto& items = doc.triggers().Items();
    std::wstring out;
    out += L"# 触发文本视图（本工程格式）\r\n";
    out += L"# T=触发头  C=条件  A=行动；# 之后为注释\r\n\r\n";
    if (items.empty()) {
        out += L"# 当前地图没有触发。可新增：\r\n";
        out += L"T players=1 flags=0\r\n";
        out += L"C type=1 player=0 amount=100 unit=0 loc=0 cmp=0 res=0 prop=0\r\n";
        return out;
    }
    for (std::size_t i = 0; i < items.size(); i++) {
        const Trigger& t = items[i];
        wchar_t head[256];
        ::wsprintfW(head, L"# 触发 %u/%u  [%s]  条件 %d / 行动 %d%s%s\r\n",
                    static_cast<unsigned>(i) + 1, static_cast<unsigned>(items.size()),
                    PlayerSummary(t).c_str(), t.ConditionCount(), t.ActionCount(),
                    (t.executionFlags & 0x1u) ? L"  [仅一次]" : L"",
                    (t.executionFlags & 0x8u) ? L"  [禁用]" : L"");
        out += head;
        out += L"T players=" + PlayersText(t) + L" flags=" +
               std::to_wstring(t.executionFlags) + L"\r\n";
        for (int c = 0; c < t.ConditionCount(); c++) {
            const TriggerCondition& cond = t.conditions[c];
            out += L"C type=" + std::to_wstring(cond.type) +
                   L" player=" + std::to_wstring(cond.player) +
                   L" amount=" + std::to_wstring(cond.amount) +
                   L" unit=" + std::to_wstring(cond.unitType) +
                   L" loc=" + std::to_wstring(cond.locationId) +
                   L" cmp=" + std::to_wstring(cond.comparison) +
                   L" res=" + std::to_wstring(cond.resourceType) +
                   L" prop=" + std::to_wstring(cond.property) +
                   L"  # " + ConditionName(cond.type) + L"\r\n";
        }
        for (int a = 0; a < t.ActionCount(); a++) {
            const TriggerAction& act = t.actions[a];
            const std::wstring preview = StringPreview(doc, act.stringId);
            out += L"A type=" + std::to_wstring(act.type) +
                   L" player=" + std::to_wstring(act.player) +
                   L" amount=" + std::to_wstring(act.amount) +
                   L" unit=" + std::to_wstring(act.setting) +
                   L" loc=" + std::to_wstring(act.locationId) +
                   L" str=" + std::to_wstring(act.stringId) +
                   L" wav=" + std::to_wstring(act.wavId) +
                   L" time=" + std::to_wstring(act.time) +
                   L" target=" + std::to_wstring(act.target) +
                   L" prop=" + std::to_wstring(act.property) +
                   L"  # " + ActionName(act.type) +
                   (preview.empty() ? L"" : (L"「" + preview + L"」")) + L"\r\n";
        }
        out += L"\r\n";
    }
    return out;
}

std::wstring TrimWide(const std::wstring& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t')) b++;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t')) e--;
    return s.substr(b, e - b);
}

bool ParseU32(const std::wstring& text, unsigned long long limit, u32& out, std::wstring& err) {
    if (text.empty()) {
        err = L"缺少数值";
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long long v = static_cast<unsigned long long>(::_wcstoui64(text.c_str(), &end, 10));
    if (!end || *end != L'\0' || v > limit) {
        err = L"数值非法: " + text;
        return false;
    }
    out = static_cast<u32>(v);
    return true;
}

bool ApplyTriggerToken(Trigger& t, wchar_t kind, int index, const std::wstring& key,
                       const std::wstring& value, std::wstring& err) {
    u32 v = 0;
    if (kind == L'T') {
        if (key == L"flags") {
            if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
            t.executionFlags = v;
            return true;
        }
        if (key == L"players") {
            for (int p = 0; p < 27; p++) t.effectivePlayers[p] = 0;
            if (value != L"none" && !value.empty()) {
                std::size_t pos = 0;
                while (pos <= value.size()) {
                    const std::size_t comma = value.find(L',', pos);
                    const std::wstring one = value.substr(
                        pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
                    if (!one.empty()) {
                        if (!ParseU32(one, 27, v, err) || v == 0) {
                            err = L"players 取值应为 1..27";
                            return false;
                        }
                        t.effectivePlayers[v - 1] = 1;
                    }
                    if (comma == std::wstring::npos) break;
                    pos = comma + 1;
                }
            }
            return true;
        }
        err = L"T 行未知字段: " + key;
        return false;
    }

    TriggerCondition* cond = nullptr;
    TriggerAction* act = nullptr;
    if (kind == L'C') cond = &t.conditions[index];
    if (kind == L'A') act = &t.actions[index];

    if (key == L"type") {
        if (!ParseU32(value, 0xFFull, v, err)) return false;
        if (kind == L'C') cond->type = static_cast<u8>(v);
        else act->type = static_cast<u8>(v);
    } else if (key == L"player") {
        if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
        if (kind == L'C') cond->player = v;
        else act->player = v;
    } else if (key == L"amount") {
        if (!ParseU32(value, kind == L'C' ? 0xFFFFFFFFull : 0xFFull, v, err)) return false;
        if (kind == L'C') cond->amount = v;
        else act->amount = static_cast<u8>(v);
    } else if (key == L"unit") {
        if (!ParseU32(value, 0xFFFFull, v, err)) return false;
        if (kind == L'C') cond->unitType = static_cast<u16>(v);
        else act->setting = static_cast<u16>(v);
    } else if (key == L"loc") {
        if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
        if (kind == L'C') cond->locationId = v;
        else act->locationId = v;
    } else if (key == L"cmp") {
        if (kind != L'C') {
            err = L"cmp 只用于条件";
            return false;
        }
        if (!ParseU32(value, 0xFFull, v, err)) return false;
        cond->comparison = static_cast<u8>(v);
    } else if (key == L"res") {
        if (kind != L'C') {
            err = L"res 只用于条件";
            return false;
        }
        if (!ParseU32(value, 0xFFull, v, err)) return false;
        cond->resourceType = static_cast<u8>(v);
    } else if (key == L"prop") {
        if (!ParseU32(value, 0xFFull, v, err)) return false;
        if (kind == L'C') cond->property = static_cast<u8>(v);
        else act->property = static_cast<u8>(v);
    } else if (key == L"str" && kind == L'A') {
        if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
        act->stringId = v;
    } else if (key == L"wav" && kind == L'A') {
        if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
        act->wavId = v;
    } else if (key == L"time" && kind == L'A') {
        if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
        act->time = v;
    } else if (key == L"target" && kind == L'A') {
        if (!ParseU32(value, 0xFFFFFFFFull, v, err)) return false;
        act->target = v;
    } else {
        err = std::wstring(kind == L'C' ? L"C" : L"A") + L" 行未知字段: " + key;
        return false;
    }
    return true;
}

bool ParseTriggerText(const std::wstring& text, std::vector<Trigger>& out,
                      int& errorLine, std::wstring& errorMsg) {
    out.clear();
    errorLine = 0;
    errorMsg.clear();
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find(L'\n', pos);
        std::wstring line = text.substr(
            pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        pos = (nl == std::wstring::npos) ? text.size() + 1 : nl + 1;
        lineNo++;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = TrimWide(line);
        if (line.empty() || line[0] == L'#') continue;

        const std::size_t space = line.find_first_of(L" \t");
        std::wstring kindText = space == std::wstring::npos ? line : line.substr(0, space);
        if (kindText.size() != 1) {
            errorLine = lineNo;
            errorMsg = L"行首应为 T / C / A";
            return false;
        }
        const wchar_t kind = static_cast<wchar_t>(std::towupper(kindText[0]));
        if (kind != L'T' && kind != L'C' && kind != L'A') {
            errorLine = lineNo;
            errorMsg = L"行首应为 T / C / A";
            return false;
        }
        if (kind != L'T' && out.empty()) {
            errorLine = lineNo;
            errorMsg = L"条件/行动之前缺少 T 行";
            return false;
        }
        if (kind == L'T') out.push_back(Trigger{});
        Trigger& current = out.back();
        int slot = -1;
        if (kind == L'C') {
            slot = current.ConditionCount();
            if (slot >= 16) {
                errorLine = lineNo;
                errorMsg = L"条件超过 16 条";
                return false;
            }
        } else if (kind == L'A') {
            slot = current.ActionCount();
            if (slot >= 64) {
                errorLine = lineNo;
                errorMsg = L"行动超过 64 条";
                return false;
            }
        }

        std::size_t p = (space == std::wstring::npos) ? line.size() : space;
        while (p < line.size()) {
            while (p < line.size() && (line[p] == L' ' || line[p] == L'\t')) p++;
            if (p >= line.size() || line[p] == L'#') break;
            const std::size_t end = line.find_first_of(L" \t#", p);
            const std::wstring token =
                line.substr(p, end == std::wstring::npos ? std::wstring::npos : end - p);
            p = (end == std::wstring::npos) ? line.size() : end;
            const std::size_t eq = token.find(L'=');
            if (eq == std::wstring::npos) {
                errorLine = lineNo;
                errorMsg = L"字段应为 key=value: " + token;
                return false;
            }
            const std::wstring key = token.substr(0, eq);
            const std::wstring value = token.substr(eq + 1);
            std::wstring err;
            if (!ApplyTriggerToken(current, kind, slot, key, value, err)) {
                errorLine = lineNo;
                errorMsg = err;
                return false;
            }
        }
    }
    return true;
}

std::wstring GetDlgTextAll(HWND dlg, int id) {
    HWND h = ::GetDlgItem(dlg, id);
    if (!h) return std::wstring();
    const int len = ::GetWindowTextLengthW(h);
    if (len <= 0) return std::wstring();
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    ::GetWindowTextW(h, &text[0], len + 1);
    text.resize(static_cast<std::size_t>(len));
    return text;
}

}  // namespace

INT_PTR CALLBACK ProcTriggerTextView(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            MapDocument* doc = DialogDocument();
            const std::wstring text =
                doc ? BuildTriggerText(*doc)
                    : std::wstring(L"# 未打开地图\r\n");
            ::SetDlgItemTextW(dlg, IDC_D206_TEXT, text.c_str());
            ::SetDlgItemTextW(dlg, IDC_D206_STATUS, L"修改后点「应用」写回地图");
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_D206_APPLY) {
                MapDocument* doc = DialogDocument();
                if (!doc) {
                    ::SetDlgItemTextW(dlg, IDC_D206_STATUS, L"未打开地图，无法应用");
                    return TRUE;
                }
                std::vector<Trigger> parsed;
                int errorLine = 0;
                std::wstring errorMsg;
                if (!ParseTriggerText(GetDlgTextAll(dlg, IDC_D206_TEXT), parsed, errorLine,
                                      errorMsg)) {
                    wchar_t status[256];
                    ::wsprintfW(status, L"解析失败（第 %d 行）：%s；未修改地图", errorLine,
                                errorMsg.c_str());
                    ::SetDlgItemTextW(dlg, IDC_D206_STATUS, status);
                    return TRUE;
                }
                // 保留原记录中未参与文本化的字节（保留区 / currentAction）
                const auto& old = doc->triggers().Items();
                for (std::size_t i = 0; i < parsed.size() && i < old.size(); i++) {
                    parsed[i].currentAction = old[i].currentAction;
                    for (int c = 0; c < 16; c++) {
                        std::memcpy(parsed[i].conditions[c].reserved,
                                    old[i].conditions[c].reserved, 2);
                    }
                    for (int a = 0; a < 64; a++) {
                        std::memcpy(parsed[i].actions[a].reserved,
                                    old[i].actions[a].reserved, 3);
                    }
                }
                doc->mutableTriggers().MutableItems() = std::move(parsed);
                doc->MarkDirty();
                wchar_t status[96];
                ::wsprintfW(status, L"已应用 %u 条触发（关闭后进入撤销栈）",
                            static_cast<unsigned>(doc->triggers().BlockCount()));
                ::SetDlgItemTextW(dlg, IDC_D206_STATUS, status);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_D206_CLOSE || LOWORD(wParam) == IDOK ||
                LOWORD(wParam) == IDCANCEL) {
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

// 171：编辑 Location。六个地形高度按钮 + 四边坐标 + 名称/STR 下标。
namespace {

// 按钮顺序与 MRGN flags 位一一对应：bit0 凹陷 / bit1 平地 / bit2 高地 /
// bit3 低空 / bit4 普空 / bit5 高空。
constexpr int kLocationElevationButtons[6] = {
    IDC_D171_1065, IDC_D171_1066, IDC_D171_1067,
    IDC_D171_1069, IDC_D171_1068, IDC_D171_1070,
};

void RefreshLocationElevation(HWND dlg) {
    for (int i = 0; i < 6; i++) {
        const bool on = (g_location.elevationMask & (1u << i)) != 0;
        ::SendDlgItemMessageW(dlg, kLocationElevationButtons[i], BM_SETCHECK,
                              on ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

}  // namespace

INT_PTR CALLBACK ProcEditLocation(HWND dlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG: {
            CenterDialog(dlg);
            ::SetDlgItemInt(dlg, IDC_D171_1079, static_cast<UINT>(g_location.left), FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1080, static_cast<UINT>(g_location.top), FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1081, static_cast<UINT>(g_location.right), FALSE);
            ::SetDlgItemInt(dlg, IDC_D171_1082, static_cast<UINT>(g_location.bottom), FALSE);
            ::SetDlgItemTextW(dlg, IDC_D171_1064, g_location.name.c_str());
            ::SetDlgItemInt(dlg, IDC_D171_1071, static_cast<UINT>(g_location.stringId), FALSE);
            RefreshLocationElevation(dlg);
            return TRUE;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            for (int i = 0; i < 6; i++) {
                if (id == kLocationElevationButtons[i]) {
                    g_location.elevationMask = 1u << i;   // 单选：清掉其它位
                    RefreshLocationElevation(dlg);
                    return TRUE;
                }
            }
            switch (id) {
                case IDC_D171_1: {                                // 确定
                    // MRGN 坐标为地图像素（0..8192），right/bottom 为开区间
                    int left = ReadClampedInt(dlg, IDC_D171_1079, g_location.left, 0, 8192);
                    int top = ReadClampedInt(dlg, IDC_D171_1080, g_location.top, 0, 8192);
                    int right = ReadClampedInt(dlg, IDC_D171_1081, g_location.right, 0, 8192);
                    int bottom = ReadClampedInt(dlg, IDC_D171_1082, g_location.bottom, 0, 8192);
                    if (right < left) std::swap(left, right);
                    if (bottom < top) std::swap(top, bottom);
                    g_location.left = left;
                    g_location.top = top;
                    g_location.right = right;
                    g_location.bottom = bottom;
                    g_location.stringId = ReadClampedInt(dlg, IDC_D171_1071, g_location.stringId,
                                                         0, 65535);
                    wchar_t name[512] = L"";
                    ::GetDlgItemTextW(dlg, IDC_D171_1064, name, 512);
                    g_location.name = name;
                    Log(L"[location] rect=(%d,%d)-(%d,%d) flags=0x%02X str=%d name=%s",
                        g_location.left, g_location.top, g_location.right, g_location.bottom,
                        g_location.elevationMask, g_location.stringId, g_location.name.c_str());
                    ::EndDialog(dlg, IDOK);
                    return TRUE;
                }
                default:
                    break;
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

}  // namespace scmd
