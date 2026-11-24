#include "ui/command_table.h"

#include <utility>

namespace scmd {

void CommandTable::Register(int id, Handler handler) {
    handlers_[id] = std::move(handler);
}

void CommandTable::RegisterAll(std::initializer_list<int> ids, Handler handler) {
    for (int id : ids) {
        handlers_[id] = handler;
    }
}

void CommandTable::SetState(int id, StateProvider provider) {
    states_[id] = std::move(provider);
}

void CommandTable::VisitStates(
    const std::function<void(int, const CommandState&)>& visitor) const {
    for (const auto& entry : states_) {
        // 状态查询可能读取当前窗口数据；先求值再回调，避免回调期间数据变化。
        const CommandState state = entry.second();
        visitor(entry.first, state);
    }
}

bool CommandTable::Execute(int id) const {
    const auto it = handlers_.find(id);
    if (it == handlers_.end()) return false;
    // 处理函数可能关闭窗口并销毁本表（如文件→退出），
    // 先复制到局部，保证调用期间可执行对象仍然有效。
    Handler handler = it->second;
    handler(id);
    return true;
}

}  // namespace scmd
