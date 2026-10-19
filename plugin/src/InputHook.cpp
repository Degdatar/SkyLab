#include "PCH.h"
#include "InputHook.h"
#include "UIManager.h"
#include "Config.h"
namespace SkyLab {
WNDPROC InputHook::origWndProc=nullptr;
HWND    InputHook::gameHwnd=nullptr;
bool    InputHook::installed=false;

static bool HotkeyMatches(WPARAM vk){
    auto &cfg = Config::Get();
    if((uint32_t)vk != cfg.hotkey) return false;
    bool shift = GetAsyncKeyState(VK_SHIFT)&0x8000;
    bool ctrl  = GetAsyncKeyState(VK_CONTROL)&0x8000;
    bool alt   = GetAsyncKeyState(VK_MENU)&0x8000;
    const std::uint32_t activeModifiers = (shift ? 1u : 0u) | (ctrl ? 2u : 0u) | (alt ? 4u : 0u);
    return activeModifiers == cfg.hotkeyMod;
}

LRESULT InputHook::WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    if(msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN){
        // SkyOS persists module settings in LocalAppData. Poll on input so a
        // changed toggle binding takes effect even while SkyLab is closed.
        Config::PollSkyOSSettings();
        const bool isRepeat = (lParam & (1LL << 30)) != 0;
        // Keep close/toggle keys ahead of ImGui's keyboard capture so there
        // is always a reliable way back to the game.
        if(!isRepeat && wParam==VK_ESCAPE && UIManager::Get().IsVisible()){
            UIManager::Get().SetVisible(false);
            return 0;
        }
        if(!isRepeat && HotkeyMatches(wParam)){
            UIManager::Get().ToggleVisible();
            // If enabled in the SkyViewer module, opening SkyLab also loads
            // the NIF under the crosshair.
            if(UIManager::Get().IsVisible() && Config::Get().autoOpenCrosshair){
                // defer one frame so toggle doesn't steal key
                // Try crosshair auto open if no nif loaded
                // We don't have currentNif check here to keep header clean; just attempt
                UIManager::Get().OpenNifFromCrosshair();
            }
            return 0;
        }
    }
    // Route input after reserved close/toggle keys have been handled.
    if(UIManager::Get().WndProc(hWnd, msg, wParam, lParam)){
        return 0;
    }
    return CallWindowProc(origWndProc, hWnd, msg, wParam, lParam);
}

void InputHook::Install(){
    if(installed) return;
    gameHwnd = FindWindowA("Skyrim", nullptr);
    if(!gameHwnd) gameHwnd = GetActiveWindow();
    if(!gameHwnd) gameHwnd = GetForegroundWindow();
    if(!gameHwnd){
        logger::warn("InputHook: no HWND found");
        return;
    }
    origWndProc = (WNDPROC)SetWindowLongPtr(gameHwnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
    installed = true;
    logger::info("InputHook installed on hwnd {:x} orig {:x}", (uintptr_t)gameHwnd, (uintptr_t)origWndProc);
}
void InputHook::Uninstall(){
    if(!installed || !gameHwnd || !origWndProc) return;
    SetWindowLongPtr(gameHwnd, GWLP_WNDPROC, (LONG_PTR)origWndProc);
    installed=false;
}
bool InputHook::IsTogglePressed(){ return false; }
}
