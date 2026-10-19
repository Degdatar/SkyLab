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
    if(cfg.hotkeyMod==1 && !shift) return false;
    if(cfg.hotkeyMod==2 && !ctrl) return false;
    if(cfg.hotkeyMod==4 && !alt) return false;
    if(cfg.hotkeyMod==0 && (shift||ctrl||alt)) {
        // allow without modifier even if other keys held? be lenient — require exact
        // but for F-keys we ignore modifiers
        if(vk>=VK_F1 && vk<=VK_F24) { /* allow */ } else return false;
    }
    return true;
}

LRESULT InputHook::WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    if(msg==WM_KEYDOWN || msg==WM_SYSKEYDOWN){
        const bool isRepeat = (lParam & (1LL << 30)) != 0;
        // Keep close/toggle keys ahead of ImGui's keyboard capture so there
        // is always a reliable way back to the game.
        if(!isRepeat && wParam==VK_ESCAPE && UIManager::Get().IsVisible()){
            UIManager::Get().SetVisible(false);
            return 0;
        }
        if(!isRepeat && HotkeyMatches(wParam)){
            UIManager::Get().ToggleVisible();
            // If becoming visible, try to auto-open crosshair nif once
            if(UIManager::Get().IsVisible()){
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
