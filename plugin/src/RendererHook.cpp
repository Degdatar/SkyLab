#include "PCH.h"
#include "RendererHook.h"
#include "UIManager.h"
#include "RE/R/Renderer.h"
// Hook the game's DXGI Present and initialize the overlay from Skyrim's renderer swapchain.
namespace SkyLab {

static ComPtr<ID3D11Device>           g_device;
static ComPtr<ID3D11DeviceContext>    g_context;
static ComPtr<IDXGISwapChain>         g_swapchain;
static ComPtr<ID3D11RenderTargetView> g_rtv;
static HWND g_hwnd=nullptr;
static void* origPresent = nullptr;
static bool  inited = false;
static std::atomic_bool firstPresentLogged{ false };
static std::mutex initMutex;
using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*,UINT,UINT);

static HRESULT WINAPI PresentHook(IDXGISwapChain* chain, UINT sync, UINT flags){
    if (!firstPresentLogged.exchange(true)) {
        logger::info("RendererHook: Skyrim Present callback reached");
    }

    // Lazy init device/RTV from the game's swapchain. The backbuffer RTV can change on
    // resize/alt-tab, so refresh it when needed rather than holding a stale view.
    {
        std::lock_guard<std::mutex> lk(initMutex);
        bool needInit = !inited || !g_device || !g_context || !g_rtv;
        if (needInit) {
            if (!g_device || !g_context) {
                chain->GetDevice(__uuidof(ID3D11Device), (void**)g_device.GetAddressOf());
                if (g_device) g_device->GetImmediateContext(&g_context);
                g_swapchain = chain;
            }
            if (g_device) {
                ComPtr<ID3D11Texture2D> back;
                if (SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()))) {
                    g_rtv.Reset();
                    g_device->CreateRenderTargetView(back.Get(), nullptr, &g_rtv);
                }
            }
            if (!inited && g_device && g_context) {
                if (HWND gameWindow = FindWindowA("Skyrim", nullptr)) {
                    g_hwnd = gameWindow;
                }
                if (!g_hwnd) g_hwnd = GetActiveWindow();
                if (g_hwnd) {
                    inited = UIManager::Get().Init(g_device.Get(), g_context.Get(), g_hwnd);
                }
                if (inited) {
                    logger::info("RendererHook PresentHook inited device {:x}", (uintptr_t)g_device.Get());
                }
            }
        }
    }
    // Render overlay on top of game's frame
    if(g_device && g_context && g_rtv && UIManager::Get().IsVisible()){
        UIManager::Get().OnRender(g_device.Get(), g_context.Get(), g_rtv.Get());
    }
    auto orig = (PresentFn)origPresent;
    return orig(chain, sync, flags);
}

void RendererHook::Install(){
    auto* rendererData = RE::BSGraphics::Renderer::GetRendererDataSingleton();
    if (!rendererData) {
        logger::warn("RendererHook: Skyrim renderer is not ready; will retry after load");
        return;
    }

    for (auto& window : rendererData->renderWindows) {
        if (!window.swapChain) {
            continue;
        }

        auto* swapChain = reinterpret_cast<IDXGISwapChain*>(window.swapChain);
        if (window.hWnd) {
            g_hwnd = reinterpret_cast<HWND>(window.hWnd);
        }

        if (HookPresent(swapChain)) {
            g_swapchain = swapChain;
            logger::info("RendererHook: hooked Skyrim swapchain {:x}", reinterpret_cast<uintptr_t>(swapChain));
            return;
        }
    }

    logger::warn("RendererHook: Skyrim renderer has no active swapchain; will retry after load");
}
bool RendererHook::HookPresent(IDXGISwapChain* swapChain){
    if (!swapChain) {
        return false;
    }

    // Hook IDXGISwapChain::Present so Skyrim's swapchain reaches our callback.
    void** vtable = *reinterpret_cast<void***>(swapChain);
    if (!vtable || !vtable[8]) {
        return false;
    }

    if (vtable[8] == reinterpret_cast<void*>(&PresentHook)) {
        return true;
    }

    origPresent = vtable[8];
    DWORD oldProtection = 0;
    if (!VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtection)) {
        origPresent = nullptr;
        return false;
    }

    InterlockedExchangePointer(
        reinterpret_cast<void* volatile*>(&vtable[8]),
        reinterpret_cast<void*>(&PresentHook));

    DWORD ignoredProtection = 0;
    VirtualProtect(&vtable[8], sizeof(void*), oldProtection, &ignoredProtection);
    FlushInstructionCache(GetCurrentProcess(), &vtable[8], sizeof(void*));

    logger::info("RendererHook: Present hooked at {:x}", reinterpret_cast<uintptr_t>(origPresent));
    return true;
}

}
