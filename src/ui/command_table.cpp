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
