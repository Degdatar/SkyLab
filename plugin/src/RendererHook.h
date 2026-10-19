#pragma once
namespace SkyLab {
class RendererHook {
public:
    static void Install();
private:
    static bool HookPresent(IDXGISwapChain* swapChain);
    static inline WNDPROC origWndProc=nullptr;
};
}
