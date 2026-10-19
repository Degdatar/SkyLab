#include <spdlog/sinks/basic_file_sink.h>

#include "PCH.h"
#include "Config.h"
#include "InputHook.h"
#include "RendererHook.h"
#include "UIManager.h"
#include "Papyrus.h"
#include "RE/S/SendHUDMessage.h"

using namespace SKSE;
using namespace SKSE::log;
using namespace SKSE::stl;

namespace SKSE::log
{
    void SetupLogging();
}

namespace
{
    bool g_skyOSReady = false;

    bool IsSkyOSLoaded()
    {
        return GetModuleHandleW(L"SkyOS.dll") != nullptr;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::log::SetupLogging();

    SKSE::Init(skse);

    auto* plugin = SKSE::PluginDeclaration::GetSingleton();

    if (!plugin) {
        SKSE::log::error("SkyLab: PluginDeclaration singleton is null.");
        return false;
    }

    const auto version = plugin->GetVersion();

    SKSE::log::info(
        "{} {} is loading...",
        plugin->GetName(),
        version.string());

    // ========================================================
    // Config
    // ========================================================

    SkyLab::Config::Load();
    SkyLab::Config::PollSkyOSSettings(true);

    SKSE::log::info(
        "Hotkey VK {:x} mod {}",
        SkyLab::Config::Get().hotkey,
        SkyLab::Config::Get().hotkeyMod);

    // ========================================================
    // Papyrus
    // ========================================================

    if (auto* papyrus = SKSE::GetPapyrusInterface()) {
        if (!papyrus->Register(SkyLab::Papyrus::Register)) {
            SKSE::log::error("SkyLab: Papyrus registration failed.");
        }
    }

    // ========================================================
    // Messaging
    // ========================================================

    auto* messaging = SKSE::GetMessagingInterface();

    if (!messaging) {
        SKSE::log::error("SkyLab: Messaging interface unavailable.");
        return false;
    }

    messaging->RegisterListener(
        [](SKSE::MessagingInterface::Message* msg) {

            if (!msg) {
                return;
            }

            switch (msg->type) {

            case SKSE::MessagingInterface::kDataLoaded:

                if (!IsSkyOSLoaded()) {
                    SKSE::log::critical("SkyLab requires SkyOS.dll. SkyLab input and renderer hooks will remain disabled.");
                    RE::SendHUDMessage::ShowHUDMessage("SkyLab requires SkyOS.dll and has been disabled.");
                    break;
                }

                g_skyOSReady = true;

                SKSE::log::info(
                    "kDataLoaded - installing SkyLab hooks");

                SkyLab::InputHook::Install();
                SkyLab::RendererHook::Install();

                {
                    const auto message = std::format("SkyLab loaded - press {} on an object", SkyLab::Config::HotkeyLabel());
                    RE::SendHUDMessage::ShowHUDMessage(message.c_str());
                }

                break;

            case SKSE::MessagingInterface::kPostLoadGame:
                if (!g_skyOSReady || !IsSkyOSLoaded()) break;
                SkyLab::RendererHook::Install();
                SKSE::log::info(
                    "Game loaded - SkyLab ready");
                break;

            case SKSE::MessagingInterface::kNewGame:
                if (!g_skyOSReady || !IsSkyOSLoaded()) break;
                SkyLab::RendererHook::Install();
                SKSE::log::info(
                    "New game - SkyLab ready");
                break;

            default:
                break;
            }
        });

    SKSE::log::info(
        "{} loaded. Press {} to open SkyLab editor.",
        plugin->GetName(),
        SkyLab::Config::HotkeyLabel());

    return true;
}

// ============================================================
// Logging
// ============================================================

namespace SKSE::log
{
    void SetupLogging()
    {
        auto logsFolder = SKSE::log::log_directory();

        if (!logsFolder) {
            SKSE::stl::report_and_fail(
                "SKSE log_directory not found");
        }

        auto pluginName =
            SKSE::PluginDeclaration::GetSingleton()->GetName();

        auto logFilePath =
            *logsFolder / std::format(
                "{}.log",
                pluginName);

        auto fileLoggerPtr =
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                logFilePath.string(),
                true);

        auto loggerPtr =
            std::make_shared<spdlog::logger>(
                "log",
                std::move(fileLoggerPtr));

        spdlog::set_default_logger(
            std::move(loggerPtr));

        spdlog::set_level(
            spdlog::level::info);

        spdlog::flush_on(
            spdlog::level::info);
    }
}

// ============================================================
// Legacy PluginVersionData declaration
//
// This is retained so the existing project structure can stay
// intact while moving to CommonLibSSE-NG v10.x.
//
// UsesAddressLibrary() now takes NO argument.
// The v10.x default also carries the Address Library V5 flag.
// ============================================================

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version =
    []() noexcept {

        SKSE::PluginVersionData v;

        v.PluginVersion(
            { 1, 0, 0, 0 });

        v.PluginName(
            "SkyLab");

        v.AuthorName(
            "Degdatar");

        v.UsesAddressLibrary();

        // SkyLab accesses Skyrim runtime structures.
        v.UsesUpdatedStructs();

        return v;
    }();
