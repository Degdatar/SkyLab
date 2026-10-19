#pragma once
namespace SkyLab {
class InputHook {
public:
    static void Install();
    static void Uninstall();
    static bool IsTogglePressed();
    // Queue a SkyOS launch request on Skyrim's window thread and return its host HWND.
    static HWND LaunchFromSkyOS();
private:
    static LRESULT WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static WNDPROC origWndProc;
    static HWND    gameHwnd;
    static bool    installed;
};
}
