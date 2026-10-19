#include "PCH.h"
#include "Config.h"
#include "Utils.h"
namespace SkyLab::Config {
    static Settings s_settings;
    namespace {
        std::mutex s_skyOSSettingsMutex;
        std::chrono::steady_clock::time_point s_lastSkyOSPoll{};
        fs::file_time_type s_lastSkyOSWrite{};
        std::uintmax_t s_lastSkyOSSize{};
        bool s_hasSkyOSSettingsSnapshot = false;

        fs::path SkyOSSettingsPath()
        {
            wchar_t localAppData[MAX_PATH]{};
            const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) return {};
            return fs::path(localAppData) / L"SkyOS" / L"SkyViewer" / L"SkyLab" / L"settings.json";
        }

        std::optional<std::string> JsonPrimitive(const std::string& json, std::string_view key)
        {
            const auto keyToken = std::format("\"{}\"", key);
            const auto keyPosition = json.find(keyToken);
            if (keyPosition == std::string::npos) return std::nullopt;
            auto valuePosition = json.find(':', keyPosition + keyToken.size());
            if (valuePosition == std::string::npos) return std::nullopt;
            ++valuePosition;
            while (valuePosition < json.size() && std::isspace(static_cast<unsigned char>(json[valuePosition]))) ++valuePosition;
            if (valuePosition >= json.size()) return std::nullopt;

            if (json[valuePosition] == '"') {
                std::string value;
                for (std::size_t i = valuePosition + 1; i < json.size(); ++i) {
                    const char ch = json[i];
                    if (ch == '"') return value;
                    if (ch == '\\' && i + 1 < json.size()) {
                        const char escaped = json[++i];
                        switch (escaped) {
                        case '"': value.push_back('"'); break;
                        case '\\': value.push_back('\\'); break;
                        case '/': value.push_back('/'); break;
                        case 'n': value.push_back('\n'); break;
                        case 'r': value.push_back('\r'); break;
                        case 't': value.push_back('\t'); break;
                        default: value.push_back(escaped); break;
                        }
                    } else {
                        value.push_back(ch);
                    }
                }
                return std::nullopt;
            }

            const auto end = json.find_first_of(",}", valuePosition);
            auto value = json.substr(valuePosition, end == std::string::npos ? std::string::npos : end - valuePosition);
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::nullopt;
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        std::optional<bool> JsonBoolean(const std::string& json, std::string_view key)
        {
            const auto text = JsonPrimitive(json, key);
            if (!text) return std::nullopt;
            const auto value = Utils::ToLower(*text);
            if (value == "true" || value == "1") return true;
            if (value == "false" || value == "0") return false;
            return std::nullopt;
        }

        std::optional<std::uint32_t> ParseHotkeyKey(std::string value)
        {
            value = Utils::ToLower(std::move(value));
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return std::nullopt;
            const auto last = value.find_last_not_of(" \t\r\n");
            value = value.substr(first, last - first + 1);

            if (value.size() == 1 && value[0] >= 'a' && value[0] <= 'z')
                return static_cast<std::uint32_t>(std::toupper(static_cast<unsigned char>(value[0])));
            if (value.size() == 1 && value[0] >= '0' && value[0] <= '9')
                return static_cast<std::uint32_t>(value[0]);

            if (value.size() >= 2 && value.front() == 'f') {
                try {
                    const auto number = std::stoul(value.substr(1));
                    if (number >= 1 && number <= 24) return VK_F1 + static_cast<std::uint32_t>(number - 1);
                } catch (...) {}
            }

            if (value == "escape") return VK_ESCAPE;
            if (value == "backspace") return VK_BACK;
            if (value == "tab") return VK_TAB;
            if (value == "enter") return VK_RETURN;
            if (value == "space") return VK_SPACE;
            if (value == "caps lock") return VK_CAPITAL;
            if (value == "num lock") return VK_NUMLOCK;
            if (value == "scroll lock") return VK_SCROLL;
            if (value == "home") return VK_HOME;
            if (value == "up") return VK_UP;
            if (value == "page up") return VK_PRIOR;
            if (value == "left") return VK_LEFT;
            if (value == "right") return VK_RIGHT;
            if (value == "end") return VK_END;
            if (value == "down") return VK_DOWN;
            if (value == "page down") return VK_NEXT;
            if (value == "insert") return VK_INSERT;
            if (value == "delete") return VK_DELETE;
            if (value == "numpad *") return VK_MULTIPLY;
            if (value == "numpad -") return VK_SUBTRACT;
            if (value == "numpad +") return VK_ADD;
            if (value == "numpad .") return VK_DECIMAL;
            if (value.starts_with("numpad ") && value.size() == 8 && value.back() >= '0' && value.back() <= '9')
                return VK_NUMPAD0 + static_cast<std::uint32_t>(value.back() - '0');

            if (value == "-") return VK_OEM_MINUS;
            if (value == "=") return VK_OEM_PLUS;
            if (value == "[") return VK_OEM_4;
            if (value == "]") return VK_OEM_6;
            if (value == ";") return VK_OEM_1;
            if (value == "'") return VK_OEM_7;
            if (value == "`") return VK_OEM_3;
            if (value == "\\") return VK_OEM_5;
            if (value == ",") return VK_OEM_COMMA;
            if (value == ".") return VK_OEM_PERIOD;
            if (value == "/") return VK_OEM_2;
            return std::nullopt;
        }

        bool ParseHotkeyChord(std::string value, std::uint32_t& key, std::uint32_t& modifiers)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                key = 0;
                modifiers = 0;
                return true;
            }
            const auto last = value.find_last_not_of(" \t\r\n");
            value = value.substr(first, last - first + 1);

            std::optional<std::uint32_t> parsedKey;
            std::uint32_t parsedModifiers = 0;
            std::size_t start = 0;
            while (start <= value.size()) {
                const auto separator = value.find('+', start);
                const auto end = separator == std::string::npos ? value.size() : separator;
                auto token = value.substr(start, end - start);
                const auto tokenFirst = token.find_first_not_of(" \t\r\n");
                if (tokenFirst == std::string::npos) return false;
                const auto tokenLast = token.find_last_not_of(" \t\r\n");
                token = token.substr(tokenFirst, tokenLast - tokenFirst + 1);
                const auto normalized = Utils::ToLower(token);

                // SkyOS formats the numpad-add key as "Numpad +"; its plus is
                // part of the key name rather than another chord separator.
                if (normalized == "numpad" && separator != std::string::npos && separator + 1 == value.size()) {
                    if (parsedKey) return false;
                    parsedKey = VK_ADD;
                    break;
                }

                if (normalized == "ctrl" || normalized == "control") parsedModifiers |= 2;
                else if (normalized == "shift") parsedModifiers |= 1;
                else if (normalized == "alt") parsedModifiers |= 4;
                else {
                    if (parsedKey) return false;
                    parsedKey = ParseHotkeyKey(std::move(token));
                    if (!parsedKey) return false;
                }

                if (separator == std::string::npos) break;
                start = separator + 1;
            }

            if (!parsedKey) return false;
            key = *parsedKey;
            modifiers = parsedModifiers;
            return true;
        }
    }

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
            if(section=="View"){
                if(k=="ShowCollision") s_settings.showCollision = (v=="1"||v=="true");
                if(k=="ShowWireframe") s_settings.showWireframe = (v=="1"||v=="true");
                if(k=="ShowNormals") s_settings.showNormals = (v=="1"||v=="true");
                if(k=="ShowUV") s_settings.showUV = (v=="1"||v=="true");
                if(k=="FOV") { try { s_settings.fov = std::clamp(std::stof(v), 30.0f, 100.0f); } catch (...) {} }
            } else if(section=="General"){
                if(k=="LastNif") s_settings.lastNifPath = v;
                if(k=="Theme") s_settings.theme = std::stoi(v);
                if(k=="AutoOpenCrosshair") s_settings.autoOpenCrosshair = (v=="1"||v=="true");
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
        f << "[View]\nShowCollision=" << (s_settings.showCollision?1:0)
          << "\nShowWireframe=" << (s_settings.showWireframe?1:0)
          << "\nShowNormals=" << (s_settings.showNormals?1:0)
          << "\nShowUV=" << (s_settings.showUV?1:0)
          << "\nFOV=" << s_settings.fov
          << "\n\n[General]\nLastNif=" << s_settings.lastNifPath << "\nTheme=" << s_settings.theme
          << "\nAutoOpenCrosshair=" << (s_settings.autoOpenCrosshair?1:0)
          << "\n\n[Export]\nRoot=" << s_settings.exportRoot << "\n";
        logger::info("Config saved to {}", path.string());
    }

    bool PollSkyOSSettings(bool force)
    {
        std::scoped_lock lock(s_skyOSSettingsMutex);
        const auto now = std::chrono::steady_clock::now();
        if (!force && s_lastSkyOSPoll.time_since_epoch().count() != 0 &&
            now - s_lastSkyOSPoll < std::chrono::milliseconds(500)) return false;
        s_lastSkyOSPoll = now;

        const auto path = SkyOSSettingsPath();
        if (path.empty()) return false;
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) return false;
        const auto writeTime = fs::last_write_time(path, ec);
        if (ec) return false;
        const auto fileSize = fs::file_size(path, ec);
        if (ec || fileSize > 64 * 1024) return false;
        if (!force && s_hasSkyOSSettingsSnapshot && writeTime == s_lastSkyOSWrite && fileSize == s_lastSkyOSSize)
            return false;

        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (json.empty()) return false;

        bool changed = false;
        auto applyBool = [&](std::string_view key, bool& setting) {
            if (const auto value = JsonBoolean(json, key); value && *value != setting) {
                setting = *value;
                changed = true;
            }
        };
        if (const auto value = JsonPrimitive(json, "openHotkey")) {
            std::uint32_t parsedKey = s_settings.hotkey;
            std::uint32_t parsedModifiers = s_settings.hotkeyMod;
            if (ParseHotkeyChord(*value, parsedKey, parsedModifiers)) {
                if (parsedKey != s_settings.hotkey || parsedModifiers != s_settings.hotkeyMod) {
                    s_settings.hotkey = parsedKey;
                    s_settings.hotkeyMod = parsedModifiers;
                    changed = true;
                }
            } else {
                logger::warn("Config: invalid SkyOS hotkey '{}'; keeping current setting", *value);
            }
        } else if (s_settings.hotkey != VK_F7 || s_settings.hotkeyMod != 0) {
            // The SkyOS module's native hotkey row is authoritative. Its declared
            // default is F7, so old INI/custom-page values cannot disagree with it.
            s_settings.hotkey = VK_F7;
            s_settings.hotkeyMod = 0;
            changed = true;
        }
        applyBool("autoOpenCrosshair", s_settings.autoOpenCrosshair);
        applyBool("showCollision", s_settings.showCollision);
        applyBool("wireframe", s_settings.showWireframe);
        applyBool("vertexNormals", s_settings.showNormals);
        applyBool("uvOverlay", s_settings.showUV);
        if (const auto value = JsonPrimitive(json, "viewportFov")) {
            try {
                const float parsed = std::clamp(std::stof(*value), 30.0f, 100.0f);
                if (std::abs(parsed - s_settings.fov) > 0.01f) { s_settings.fov = parsed; changed = true; }
            } catch (...) {}
        }

        s_lastSkyOSWrite = writeTime;
        s_lastSkyOSSize = fileSize;
        s_hasSkyOSSettingsSnapshot = true;
        if (changed) logger::info("SkyLab: applied settings from SkyOS module '{}': {}", path.string(), HotkeyLabel());
        return changed;
    }

    std::string HotkeyLabel()
    {
        if (s_settings.hotkey == 0) return "Unassigned";
        std::string label;
        if (s_settings.hotkeyMod & 2) label += "Ctrl + ";
        if (s_settings.hotkeyMod & 1) label += "Shift + ";
        if (s_settings.hotkeyMod & 4) label += "Alt + ";
        if (s_settings.hotkey >= VK_F1 && s_settings.hotkey <= VK_F24)
            label += std::format("F{}", s_settings.hotkey - VK_F1 + 1);
        else if (s_settings.hotkey >= 'A' && s_settings.hotkey <= 'Z')
            label.push_back(static_cast<char>(s_settings.hotkey));
        else
            label += std::format("VK 0x{:02X}", s_settings.hotkey);
        return label;
    }
}
