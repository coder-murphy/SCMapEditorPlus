#include "ui/dialog_registry.h"

#include "common/log.h"
#include "ui/dlg_procs.h"

namespace scmd {

namespace {

// 25 个对话框全部接上专用处理过程（status 均为 impl）
const DialogEntry kEntries[] = {
    {103, L"Scmdraft2.0.8.0[CsgA]NaKa汉化版", "impl", false, &ProcAbout},
    {155, L"Scmdraft 2.0基本设置", "impl", false, &ProcSettings},
    {163, L"兼容性警告", "impl", false, &ProcCompatWarning},
    {164, L"SP单位属性", "impl", false, &ProcUnitSpFlags},
    {166, L"方格设置", "impl", false, &ProcGridSettings},
    {167, L"Scmdraft 2.0.8.0 [CsgA]NaKa汉化版", "impl", false, &ProcMpqList},
    {169, L"新建地图", "impl", false, &ProcNewMap},
    {171, L"编辑Location", "impl", false, &ProcEditLocation},
    {172, L"单位名字", "impl", false, &ProcUnitNames},
    {173, L"查看备份数据", "impl", false, &ProcBackupViewer},
    {174, L"在线升级", "impl", false, &ProcUpdater},
    {175, L"重设地图大小", "impl", false, &ProcResizeMap},
    {179, L"重命名开关", "impl", false, &ProcRenameSwitch},
    {180, L"设置单位属性", "impl", false, &ProcUnitProps},
    {181, L"触发编辑器", "impl", false, &ProcTriggerEditor},
    {182, L"自定义刷子", "impl", false, &ProcCustomBrush},
    {183, L"对话框", "impl", false, &ProcTabHost},
    {184, L"(无标题)", "impl", true, &ProcListViewHost},
    {185, L"(无标题)", "impl", true, &ProcListEditorA},
    {186, L"(无标题)", "impl", true, &ProcListEditorB},
    {187, L"对话框", "impl", false, &ProcRichText},
    {188, L"输入数量", "impl", false, &ProcInputCount},
    {189, L"编辑字符串", "impl", false, &ProcStringEditor},
    {191, L"重叠单位", "impl", false, &ProcOverlapUnits},
    {192, L"Iscript测试", "impl", false, &ProcIscriptTest},
    {193, L"编辑条件", "impl", false, &ProcTriggerCondition},
    {194, L"编辑行动", "impl", false, &ProcTriggerAction},
    {195, L"单位放置参数", "impl", false, &ProcUnitPlacement},
    {196, L"玩家 / 势力设定", "impl", false, &ProcPlayerRoster},
    {197, L"升级 / 科技限制", "impl", false, &ProcRestrictions},
    {198, L"编辑声音", "impl", false, &ProcSoundList},
    {199, L"任务简报", "impl", false, &ProcMissionBriefing},
};

int g_lastDialogError = 0;
INT_PTR g_lastDialogResult = 0;

}  // namespace

int LastDialogError() { return g_lastDialogError; }
INT_PTR LastDialogResult() { return g_lastDialogResult; }

const DialogEntry* FindDialog(int id) {
    for (const auto& e : kEntries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

int DialogCount() { return static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0])); }

bool ShowDialog(int id, HWND owner) {
    const DialogEntry* e = FindDialog(id);
    if (!e) {
        Log(L"[dlg] no entry for %d", id);
        return false;
    }
    if (e->childPanel) {
        // 184/185/186 是嵌在触发编辑器里的子面板，只能 CreateDialog 到父窗口
        Log(L"[dlg] %d is a child panel, must be embedded", id);
        return false;
    }
    ::SetLastError(0);
    const INT_PTR r = ::DialogBoxParamW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), owner, e->proc, 0);
    g_lastDialogResult = r;
    if (r == -1) {
        g_lastDialogError = static_cast<int>(::GetLastError());
        Log(L"[dlg] DialogBoxParam failed for %d (err=%d)", id, g_lastDialogError);
        return false;
    }
    return true;
}

}  // namespace scmd
