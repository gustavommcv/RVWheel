#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "LauncherApp.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return rvwheel::tools::launcher::RunLauncher();
}
