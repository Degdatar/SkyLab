#include "PCH.h"
#include "InputHook.h"

extern "C" __declspec(dllexport)
HWND WINAPI SkyOS_App_Launch(const wchar_t* argument)
{
    (void)argument;
    if (!GetModuleHandleW(L"SkyOS.dll")) {
        logger::warn("SkyOS app launch refused because SkyOS.dll is not loaded");
        return nullptr;
    }

    return SkyLab::InputHook::LaunchFromSkyOS();
}
