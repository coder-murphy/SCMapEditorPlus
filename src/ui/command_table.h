#pragma once
#include <functional>
#include <initializer_list>
#include <unordered_map>

namespace scmd {

// 命令的界面状态（当前用于菜单勾选；后续可扩展 enabled）
struct CommandState {
    bool checked = false;
};

// 命令表：把命令 ID 映射到处理函数和状态查询。
// 处理函数接收命令 ID，便于同一函数服务一组命令（缩放、显示开关等）。
class CommandTable {
public:
    using Handler = std::function<void(int)>;
    using StateProvider = std::function<CommandState()>;

    void Register(int id, Handler handler);
    // 为一组命令注册同一处理函数（如“编辑层”的 6 项）
    void RegisterAll(std::initializer_list<int> ids, Handler handler);
    // 为可勾选命令注册状态查询；每次同步菜单时调用。
    void SetState(int id, StateProvider provider);
    // 遍历已注册状态的命令：状态查询 → visitor。
    void VisitStates(const std::function<void(int, const CommandState&)>& visitor) const;
    // 已注册则执行并返回 true；否则返回 false。
    bool Execute(int id) const;

private:
    std::unordered_map<int, Handler> handlers_;
    std::unordered_map<int, StateProvider> states_;
};

}  // namespace scmd
