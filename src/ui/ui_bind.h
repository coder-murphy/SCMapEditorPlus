#pragma once
#include <functional>
#include <string>
#include <vector>

#include <windows.h>

namespace scmd {

// 事件绑定：把 WM_COMMAND 的（控件 ID，通知码）映射到处理函数。
// 仅用于 UI 线程内的局部连接，不提供跨线程/反射能力。
// 该类型不拥有窗口；通常作为对话框状态的一部分保存。
class EventBinder {
public:
    using Handler = std::function<void(HWND)>;

    void On(int controlId, UINT notification, Handler handler);
    // 已注册则调用处理函数并返回 true；否则返回 false。
    bool Dispatch(HWND dlg, WPARAM wParam) const;

private:
    struct Entry {
        int controlId = 0;
        UINT notification = 0;
        Handler handler;
    };
    std::vector<Entry> entries_;
};

// 表单描述：把“加载 / 提交 / 重置默认”集中到一张字段表里。
// 数值字段提交时按 [minValue, maxValue] 钳制；文本字段提交当前控件内容。
class FormSpec {
public:
    void AddInt(int controlId, int value, int minValue, int maxValue, int defaultValue,
                std::function<void(int)> apply);
    void AddText(int controlId, std::wstring value, std::function<void(std::wstring)> apply);
    void AddCheck(int controlId, bool value, bool defaultValue,
                  std::function<void(bool)> apply);

    void Load(HWND dlg) const;            // 字段当前值 → 控件
    void Commit(HWND dlg) const;          // 控件 → 钳制/取值 → apply
    void ResetDefaults(HWND dlg) const;   // 字段默认值 → 控件（文本保持不变）

private:
    enum class Kind { Int, Text, Check };
    struct Field {
        Kind kind = Kind::Int;
        int controlId = 0;
        int minValue = 0;
        int maxValue = 0;
        int intValue = 0;
        int defaultValue = 0;
        std::wstring textValue;
        bool checkValue = false;
        bool checkDefault = false;
        std::function<void(int)> applyInt;
        std::function<void(std::wstring)> applyText;
        std::function<void(bool)> applyCheck;
    };
    std::vector<Field> fields_;
};

}  // namespace scmd
