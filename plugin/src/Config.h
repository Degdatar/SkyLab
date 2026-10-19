#pragma once
// Central config + INI handling
namespace SkyLab::Config {
    struct Settings {
        uint32_t hotkey = 0x76; // F7 VK code
        uint32_t hotkeyMod = 0; // 0=none, 1=shift, 2=ctrl, 4=alt
        bool     showCollision = true;
        bool     showWireframe = false;
        bool     showNormals = false;
        bool     showUV = false;
        float    fov = 60.f;
        std::string lastNifPath;
        // Empty uses Skyrim Data. Set this to MO2's active instance/overwrite
        // folder to export directly into Overwrite while using MO2.
        std::string exportRoot;
        int      theme = 0; // 0 dark, 1 light
    };
    Settings& Get();
    void Load();
    void Save();
    fs::path GetIniPath();
}
