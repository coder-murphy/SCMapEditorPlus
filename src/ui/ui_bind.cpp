#include "ui/ui_bind.h"

#include <algorithm>

namespace scmd {

namespace {

std::wstring ControlText(HWND dlg, int controlId) {
    HWND control = ::GetDlgItem(dlg, controlId);
    if (!control) return std::wstring();
    const int length = ::GetWindowTextLengthW(control);
    if (length <= 0) return std::wstring();
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    const int copied = ::GetWindowTextW(control, text.data(), length + 1);
    text.resize(copied > 0 ? static_cast<std::size_t>(copied) : 0);
    return text;
}

}  // namespace

void EventBinder::On(int controlId, UINT notification, Handler handler) {
    entries_.push_back({controlId, notification, std::move(handler)});
}

bool EventBinder::Dispatch(HWND dlg, WPARAM wParam) const {
    const int controlId = LOWORD(wParam);
    const UINT notification = HIWORD(wParam);
    for (const Entry& entry : entries_) {
        if (entry.controlId != controlId || entry.notification != notification) continue;
        // 处理函数可能 EndDialog 并销毁持有本对象的对话框状态，
        // 先用局部副本保证调用期间可执行对象仍然有效。
        Handler handler = entry.handler;
        handler(dlg);
        return true;
    }
    return false;
}

void FormSpec::AddInt(int controlId, int value, int minValue, int maxValue, int defaultValue,
                      std::function<void(int)> apply) {
    Field field;
    field.kind = Kind::Int;
    field.controlId = controlId;
    field.minValue = minValue;
    field.maxValue = maxValue;
    field.intValue = value;
    field.defaultValue = defaultValue;
    field.applyInt = std::move(apply);
    fields_.push_back(std::move(field));
}

void FormSpec::AddText(int controlId, std::wstring value, std::function<void(std::wstring)> apply) {
    Field field;
    field.kind = Kind::Text;
    field.controlId = controlId;
    field.textValue = std::move(value);
    field.applyText = std::move(apply);
    fields_.push_back(std::move(field));
}

void FormSpec::AddCheck(int controlId, bool value, bool defaultValue,
                        std::function<void(bool)> apply) {
    Field field;
    field.kind = Kind::Check;
    field.controlId = controlId;
    field.checkValue = value;
    field.checkDefault = defaultValue;
    field.applyCheck = std::move(apply);
    fields_.push_back(std::move(field));
}

void FormSpec::Load(HWND dlg) const {
    for (const Field& field : fields_) {
        switch (field.kind) {
            case Kind::Int:
                ::SetDlgItemInt(dlg, field.controlId, static_cast<UINT>(field.intValue), FALSE);
                break;
            case Kind::Text:
                ::SetDlgItemTextW(dlg, field.controlId, field.textValue.c_str());
                break;
            case Kind::Check:
                ::CheckDlgButton(dlg, field.controlId,
                                 field.checkValue ? BST_CHECKED : BST_UNCHECKED);
                break;
        }
    }
}

void FormSpec::Commit(HWND dlg) const {
    for (const Field& field : fields_) {
        switch (field.kind) {
            case Kind::Int: {
                BOOL ok = FALSE;
                int value = static_cast<int>(::GetDlgItemInt(dlg, field.controlId, &ok, FALSE));
                if (!ok) value = field.intValue;
                value = std::clamp(value, field.minValue, field.maxValue);
                if (field.applyInt) field.applyInt(value);
                break;
            }
            case Kind::Text:
                if (field.applyText) field.applyText(ControlText(dlg, field.controlId));
                break;
            case Kind::Check:
                if (field.applyCheck) {
                    field.applyCheck(::IsDlgButtonChecked(dlg, field.controlId) == BST_CHECKED);
                }
                break;
        }
    }
}

void FormSpec::ResetDefaults(HWND dlg) const {
    for (const Field& field : fields_) {
        switch (field.kind) {
            case Kind::Int:
                ::SetDlgItemInt(dlg, field.controlId, static_cast<UINT>(field.defaultValue), FALSE);
                break;
            case Kind::Text:
                break;   // 文本字段没有默认值，重置时保持用户输入
            case Kind::Check:
                ::CheckDlgButton(dlg, field.controlId,
                                 field.checkDefault ? BST_CHECKED : BST_UNCHECKED);
                break;
        }
    }
}

}  // namespace scmd
