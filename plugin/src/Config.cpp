#include "PCH.h"
#include "Config.h"
namespace SkyLab::Config {
    static Settings s_settings;
    Settings& Get() { return s_settings; }

    fs::path GetIniPath() {
        // Resolve Data/SKSE/Plugins/SkyLab.ini relative to game exe
        // SKSE GetPluginConfigPath isn't stable across CLib versions, so derive from exe path.
        try {
            char exeBuf[MAX_PATH]{};
            GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
            fs::path exePath(exeBuf);
            auto dataDir = exePath.parent_path() / "Data" / "SKSE" / "Plugins" / "SkyLab.ini";
            return dataDir;
        } catch (...) {}
        return fs::path("Data/SKSE/Plugins/SkyLab.ini");
    }

    void Load() {
        auto path = GetIniPath();
        if (!fs::exists(path)) return;
        std::ifstream f(path);
        std::string line;
        auto trim = [](std::string s){
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c){return !std::isspace(c);}));
            s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c){return !std::isspace(c);}).base(), s.end());
            return s;
        };
        std::string section;
        for(; std::getline(f,line);){
            line = trim(line);
            if(line.empty() || line[0]==';'||line[0]=='#') continue;
            if(line.front()=='[' && line.back()==']'){ section=line.substr(1,line.size()-2); continue; }
            auto eq=line.find('=');
            if(eq==std::string::npos) continue;
            auto k=trim(line.substr(0,eq));
            auto v=trim(line.substr(eq+1));
            if(section=="Hotkey"){
                if(k=="KeyCode") s_settings.hotkey = std::stoul(v, nullptr, 0);
                if(k=="Modifier") s_settings.hotkeyMod = std::stoul(v,nullptr,0);
            } else if(section=="View"){
                if(k=="ShowCollision") s_settings.showCollision = (v=="1"||v=="true");
                if(k=="ShowWireframe") s_settings.showWireframe = (v=="1"||v=="true");
                if(k=="FOV") s_settings.fov = std::stof(v);
            } else if(section=="General"){
                if(k=="LastNif") s_settings.lastNifPath = v;
                if(k=="Theme") s_settings.theme = std::stoi(v);
            } else if(section=="Export"){
                if(k=="Root") s_settings.exportRoot = v;
            }
        }
        logger::info("Config loaded from {}", path.string());
    }
    void Save(){
        auto path = GetIniPath();
        fs::create_directories(path.parent_path());
        std::ofstream f(path);
        f << "[Hotkey]\nKeyCode=" << s_settings.hotkey << "\nModifier=" << s_settings.hotkeyMod
          << "\n\n[View]\nShowCollision=" << (s_settings.showCollision?1:0)
          << "\nShowWireframe=" << (s_settings.showWireframe?1:0)
          << "\nFOV=" << s_settings.fov
          << "\n\n[General]\nLastNif=" << s_settings.lastNifPath << "\nTheme=" << s_settings.theme
          << "\n\n[Export]\nRoot=" << s_settings.exportRoot << "\n";
        logger::info("Config saved to {}", path.string());
    }
}
