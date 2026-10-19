#include "PCH.h"
#include "Papyrus.h"
#include "UIManager.h"
#include "Utils.h"
#include "NifParser.h"
#include "TextureManager.h"

namespace SkyLab::Papyrus {

namespace {
    bool SkyOSAvailable()
    {
        return GetModuleHandleW(L"SkyOS.dll") != nullptr;
    }
}

void ShowSkyLab(RE::StaticFunctionTag*){ if (SkyOSAvailable()) SkyLab::UIManager::Get().SetVisible(true); }
void HideSkyLab(RE::StaticFunctionTag*){ if (SkyOSAvailable()) SkyLab::UIManager::Get().SetVisible(false); }
void ToggleSkyLab(RE::StaticFunctionTag*){ if (SkyOSAvailable()) SkyLab::UIManager::Get().ToggleVisible(); }
bool IsVisible(RE::StaticFunctionTag*){ return SkyOSAvailable() && SkyLab::UIManager::Get().IsVisible(); }
void OpenNif(RE::StaticFunctionTag*, RE::BSFixedString path){
    if (!SkyOSAvailable()) return;
    fs::path p = SkyLab::Utils::ResolveNifPath(path.c_str());
    SkyLab::UIManager::Get().OpenNif(p);
    SkyLab::UIManager::Get().SetVisible(true);
}
void ReplaceTexture(RE::StaticFunctionTag*, RE::TESObjectREFR* ref, RE::BSFixedString orig, RE::BSFixedString repl){
    if (!SkyOSAvailable()) return;
    if(!ref) ref = SkyLab::Utils::GetCrosshairRef();
    if(!ref) { SkyLab::Utils::ShowNotification("SkyLab: No target ref"); return; }
    SkyLab::TextureManager::Get().ReplaceTextureOnRef(ref, orig.c_str(), repl.c_str());
}

bool Register(RE::BSScript::IVirtualMachine* vm){
    if (!SkyOSAvailable()) {
        SKSE::log::error("SkyLab Papyrus registration skipped because SkyOS.dll is not loaded");
        return false;
    }
    vm->RegisterFunction("Show", "SkyLab", ShowSkyLab);
    vm->RegisterFunction("Hide", "SkyLab", HideSkyLab);
    vm->RegisterFunction("Toggle", "SkyLab", ToggleSkyLab);
    vm->RegisterFunction("IsVisible", "SkyLab", IsVisible);
    vm->RegisterFunction("OpenNif", "SkyLab", OpenNif);
    vm->RegisterFunction("ReplaceTexture", "SkyLab", ReplaceTexture);
    SKSE::log::info("Papyrus SkyLab registered");
    return true;
}
}
