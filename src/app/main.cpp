#include <windows.h>

#include "app/app.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    return scmd::App::Instance().Run(hInstance);
}
