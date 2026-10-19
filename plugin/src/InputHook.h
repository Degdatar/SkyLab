#pragma once
namespace SkyLab {
class InputHook {
public:
    static void Install();
    static void Uninstall();
    static bool IsTogglePressed();
private:
    static LRESULT WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static WNDPROC origWndProc;
    static HWND    gameHwnd;
    static bool    installed;
};
}
