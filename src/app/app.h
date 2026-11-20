#pragma once
#include <windows.h>

#include "ui/main_window.h"

namespace scmd {

// 应用生命周期：注册、创建主窗口、消息循环
class App {
public:
    static App& Instance();

    int Run(HINSTANCE hInstance);

private:
    MainWindow mainWindow_;
};

}  // namespace scmd
