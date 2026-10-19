#include "PCH.h"
#include "Utils.h"
#include "RE/S/SendHUDMessage.h"
#include <cstring>

#include "RE/C/Console.h"
#include "RE/C/CrosshairPickData.h"
#include "RE/T/TESFurniture.h"
#include "RE/T/TESObjectARMA.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectCONT.h"
#include "RE/T/TESObjectDOOR.h"
#include "RE/T/TESObjectSTAT.h"
#include "RE/T/TESObjectWEAP.h"

namespace SkyLab::Utils
{
    std::string ToLower(std::string s)
    {
        std::transform(
            s.begin(),
            s.end(),
            s.begin(),
            [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

        return s;
    }

    std::string WideToUTF8(const std::wstring& w)
    {
        if (w.empty()) {
            return {};
        }

        const int n = WideCharToMultiByte(
            CP_UTF8,
            0,
            w.c_str(),
            static_cast<int>(w.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

        if (n <= 0) {
            return {};
        }

        std::string result(static_cast<std::size_t>(n), '\0');

        if (WideCharToMultiByte(
                CP_UTF8,
                0,
                w.c_str(),
                static_cast<int>(w.size()),
                result.data(),
                n,
                nullptr,
                nullptr) <= 0) {
            return {};
        }

        return result;
    }

    std::wstring UTF8ToWide(const std::string& s)
    {
        if (s.empty()) {
            return {};
        }

        const int n = MultiByteToWideChar(
            CP_UTF8,
            0,
            s.c_str(),
            static_cast<int>(s.size()),
            nullptr,
            0);

        if (n <= 0) {
            return {};
        }

        std::wstring result(static_cast<std::size_t>(n), L'\0');

        if (MultiByteToWideChar(
                CP_UTF8,
                0,
                s.c_str(),
                static_cast<int>(s.size()),
                result.data(),
                n) <= 0) {
            return {};
        }

        return result;
    }

    std::string GetDataPath()
    {
        char buf[MAX_PATH]{};

        const DWORD length = GetModuleFileNameA(
            nullptr,
            buf,
            MAX_PATH);

        if (length == 0) {
            return {};
        }

        std::string exe(buf, length);

        const auto pos = exe.find_last_of("\\/");

        const std::string dir =
            pos != std::string::npos
                ? exe.substr(0, pos + 1)
                : "";

        return dir + "Data\\";
    }

    std::vector<std::string> ListFilesRecursive(
        const fs::path& dir,
        const std::string& ext)
    {
        std::vector<std::string> out;

        if (!fs::exists(dir)) {
            return out;
        }

        const std::string lowerExt = ToLower(ext);

        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto path = entry.path();
            const auto entryExt =
                ToLower(path.extension().string());

            if (lowerExt.empty() || entryExt == lowerExt) {
                out.push_back(path.string());
            }
        }

        return out;
    }

    std::vector<std::string> ListBsaResources(const fs::path& dataRoot, const std::string& ext)
    {
        std::vector<std::string> results;
        std::unordered_set<std::string> seen;
        const auto wantedExt = ToLower(ext);
        auto readU32 = [](std::istream& in, uint32_t& value) {
            uint8_t b[4]{};
            if (!in.read(reinterpret_cast<char*>(b), 4)) return false;
            value = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
            return true;
        };
        try {
            for (const auto& archive : fs::directory_iterator(dataRoot)) {
                if (!archive.is_regular_file() || ToLower(archive.path().extension().string()) != ".bsa") continue;
                std::ifstream in(archive.path(), std::ios::binary);
                char magic[4]{};
                uint32_t version=0, folderOffset=0, archiveFlags=0, folderCount=0, fileCount=0;
                uint32_t folderNamesLength=0, fileNamesLength=0, fileFlags=0;
                if (!in.read(magic, 4) || std::memcmp(magic, "BSA\0", 4) != 0 ||
                    !readU32(in, version) || (version != 0x68 && version != 0x69) ||
                    !readU32(in, folderOffset) || !readU32(in, archiveFlags) || !readU32(in, folderCount) ||
                    !readU32(in, fileCount) || !readU32(in, folderNamesLength) ||
                    !readU32(in, fileNamesLength) || !readU32(in, fileFlags) ||
                    folderCount > 100000 || fileCount > 2000000) continue;
                (void)archiveFlags; (void)folderNamesLength; (void)fileFlags;
                struct Folder { uint32_t count{}, offset{}; };
                std::vector<Folder> folders(folderCount);
                in.seekg(folderOffset, std::ios::beg);
                bool valid = true;
                for (auto& folder : folders) {
                    uint8_t hash[8]{};
                    if (!in.read(reinterpret_cast<char*>(hash), 8) || !readU32(in, folder.count) || !readU32(in, folder.offset)) { valid=false; break; }
                }
                if (!valid) continue;
                std::vector<std::string> folderNames;
                folderNames.reserve(folderCount);
                uint64_t observedFiles=0;
                for (const auto& folder : folders) {
                    in.seekg(folder.offset, std::ios::beg);
                    uint8_t nameLength=0;
                    if (!in.read(reinterpret_cast<char*>(&nameLength), 1) || nameLength == 0 || nameLength > 255) { valid=false; break; }
                    std::string folderName(nameLength, '\0');
                    if (!in.read(folderName.data(), nameLength)) { valid=false; break; }
                    if (!folderName.empty() && folderName.back() == '\0') folderName.pop_back();
                    in.seekg(static_cast<std::streamoff>(folder.count) * 16, std::ios::cur);
                    if (!in) { valid=false; break; }
                    folderNames.push_back(std::move(folderName));
                    observedFiles += folder.count;
                }
                if (!valid || observedFiles != fileCount) continue;
                const uint64_t fileNameTableOffset = static_cast<uint64_t>(folderOffset) +
                    static_cast<uint64_t>(folderCount) * 16 + folderNamesLength + static_cast<uint64_t>(fileCount) * 16;
                if (fileNameTableOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) continue;
                in.clear();
                in.seekg(static_cast<std::streamoff>(fileNameTableOffset), std::ios::beg);
                if (!in) continue;
                std::vector<std::string> names;
                names.reserve(fileCount);
                for (uint32_t i=0; i<fileCount; ++i) {
                    std::string name;
                    if (!std::getline(in, name, '\0')) { valid=false; break; }
                    names.push_back(std::move(name));
                }
                if (!valid) continue;
                std::size_t nameIndex=0;
                for (std::size_t f=0; f<folders.size(); ++f) {
                    for (uint32_t i=0; i<folders[f].count; ++i, ++nameIndex) {
                        fs::path resource = fs::path(folderNames[f]) / names[nameIndex];
                        if (ToLower(resource.extension().string()) != wantedExt) continue;
                        auto normalized = resource.generic_string();
                        std::replace(normalized.begin(), normalized.end(), '/', '\\');
                        if (seen.insert(ToLower(normalized)).second) results.push_back(std::move(normalized));
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Archive indexing is best effort; inaccessible or malformed archives are skipped.
        }
        return results;
    }

    std::string FormatVec3(float x, float y, float z)
    {
        char buffer[64]{};

        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.2f, %.2f, %.2f",
            x,
            y,
            z);

        return buffer;
    }

    bool IsDDS(const fs::path& p)
    {
        return ToLower(p.extension().string()) == ".dds";
    }

    bool ReadResourceFile(const fs::path& path, std::vector<uint8_t>& bytes, bool* viaGameResourceStream)
    {
        bytes.clear();
        if (viaGameResourceStream) *viaGameResourceStream = false;

        // Normal file access respects MO2's USVFS when Skyrim was launched
        // through MO2, and handles Vortex's deployed files and loose assets.
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (file) {
            const auto end = file.tellg();
            const auto length = static_cast<std::streamoff>(end);
            if (length > 0 && static_cast<uint64_t>(length) <= 512ull * 1024 * 1024) {
                bytes.resize(static_cast<size_t>(length));
                file.seekg(0, std::ios::beg);
                if (file.read(reinterpret_cast<char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()))) {
                    return true;
                }
                bytes.clear();
            }
        }

        if (ReadGameResourceFile(path, bytes)) {
            if (viaGameResourceStream) *viaGameResourceStream = true;
            return true;
        }
        return false;
    }

    bool ReadGameResourceFile(const fs::path& path, std::vector<uint8_t>& bytes)
    {
        bytes.clear();
        // BSResourceNiBinaryStream resolves Data-relative resource names via
        // Skyrim's active resource locations, including loaded BSA archives.
        fs::path resourcePath = path;
        const fs::path dataRoot = fs::path(GetDataPath()).lexically_normal();
        if (resourcePath.is_absolute()) {
            resourcePath = resourcePath.lexically_normal().lexically_relative(dataRoot);
            if (resourcePath.empty() || *resourcePath.begin() == "..") {
                return false;
            }
        }

        std::string resourceName = resourcePath.generic_string();
        if (resourceName.size() >= 5 &&
            ToLower(resourceName.substr(0, 5)) == "data/") {
            resourceName.erase(0, 5);
        }
        if (resourceName.empty()) {
            return false;
        }
        std::replace(resourceName.begin(), resourceName.end(), '/', '\\');

        RE::BSResourceNiBinaryStream stream(resourceName);
        if (!stream.good()) {
            return false;
        }

        // BSResourceNiBinaryStream::get_info is intentionally unimplemented
        // in CommonLibSSE-NG; the opened resource stream exposes totalSize.
        const uint32_t resourceSize = stream.stream->totalSize;
        if (resourceSize == 0 || resourceSize > 512u * 1024u * 1024u) {
            return false;
        }

        bytes.resize(resourceSize);
        stream.seek(0);
        if (!stream.read(bytes.data(), static_cast<uint32_t>(bytes.size()))) {
            bytes.clear();
            return false;
        }

        return true;
    }

    bool WriteExportFile(const fs::path& outputRoot, const fs::path& relativePath,
                         const std::vector<uint8_t>& bytes, fs::path* writtenPath)
    {
        if (outputRoot.empty() || relativePath.empty() || bytes.empty() || relativePath.is_absolute()) {
            return false;
        }

        const fs::path cleanRelative = relativePath.lexically_normal();
        for (const auto& component : cleanRelative) {
            if (component == "..") return false;
        }
        fs::path destination;
        try {
            const fs::path root = fs::absolute(outputRoot).lexically_normal();
            destination = (root / cleanRelative).lexically_normal();
            const fs::path checkRelative = destination.lexically_relative(root);
            if (checkRelative.empty() || *checkRelative.begin() == "..") return false;
            fs::create_directories(destination.parent_path());
            const fs::path temporary = destination.wstring() + L".skylab-tmp";
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out) return false;
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.flush();
                if (!out) {
                    out.close();
                    fs::remove(temporary);
                    return false;
                }
            }
            std::error_code ec;
            if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                // Some mod-manager virtual filesystems redirect CreateFile
                // but do not virtualize MoveFileEx consistently. Fall back
                // to writing the final virtual Data path directly.
                std::ofstream out(destination, std::ios::binary | std::ios::trunc);
                if (!out) {
                    fs::remove(temporary);
                    return false;
                }
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.flush();
                const bool wrote = static_cast<bool>(out);
                out.close();
                fs::remove(temporary);
                if (!wrote) return false;
            }
            if (writtenPath) *writtenPath = destination;
            return true;
        } catch (const std::exception& e) {
            SKSE::log::error("SkyLab export failed for '{}': {}", destination.string(), e.what());
            return false;
        }
    }

    fs::path ResolveNifPath(const std::string& rel)
    {
        std::string path = rel;

        std::replace(
            path.begin(),
            path.end(),
            '/',
            '\\');

        if (path.find(':') != std::string::npos ||
            path.rfind("\\\\", 0) == 0) {
            return fs::path(path);
        }

        if (ToLower(path).rfind("data\\", 0) == 0) {
            path.erase(0, 5);
        }

        if (ToLower(path).rfind("meshes\\", 0) == 0) {
            return fs::path(GetDataPath()) / path;
        }

        return fs::path(GetDataPath()) / "meshes" / path;
    }

    fs::path ResolveTexturePath(const std::string& rel)
    {
        std::string path = rel;

        std::replace(
            path.begin(),
            path.end(),
            '/',
            '\\');

        if (path.find(':') != std::string::npos ||
            path.rfind("\\\\", 0) == 0) {
            return fs::path(path);
        }

        if (ToLower(path).rfind("data\\", 0) == 0) {
            path.erase(0, 5);
        }

        if (ToLower(path).rfind("textures\\", 0) == 0) {
            return fs::path(GetDataPath()) / path;
        }

        return fs::path(GetDataPath()) / "textures" / path;
    }

    RE::TESObjectREFR* GetCrosshairRef()
    {
        // Primary source: the game's crosshair pick data.
        if (auto* pickData = RE::CrosshairPickData::GetSingleton()) {
            if (pickData->target) {
                const auto target = pickData->target.get();

                if (target) {
                    return target.get();
                }
            }
        }

        // Fallback: reference selected in the console.
        const auto selectedRef = RE::Console::GetSelectedRef();

        if (selectedRef) {
            return selectedRef.get();
        }

        return nullptr;
    }

    std::string GetCrosshairRefNifPath()
    {
        auto* ref = GetCrosshairRef();

        if (!ref) {
            return {};
        }

        auto* base = ref->GetBaseObject();

        if (!base) {
            return {};
        }

        // ----------------------------------------------------
        // Static
        // ----------------------------------------------------

        if (auto* stat = base->As<RE::TESObjectSTAT>()) {
            const char* path = stat->GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        // ----------------------------------------------------
        // Armor addon
        //
        // TESObjectARMA contains bipedModels[].
        // Use the male model at index 0.
        // ----------------------------------------------------

        if (auto* arma = base->As<RE::TESObjectARMA>()) {
            const char* path = arma->bipedModels[0].GetModel();

            if (path && *path) {
                return path;
            }

            // Try first-person model as a fallback.
            path = arma->bipedModel1stPersons[0].GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        // ----------------------------------------------------
        // Armor
        //
        // TESObjectARMO inherits TESBipedModelForm, which
        // exposes worldModels[].
        // ----------------------------------------------------

        if (auto* armo = base->As<RE::TESObjectARMO>()) {
            const char* path = armo->worldModels[0].GetModel();

            if (path && *path) {
                return path;
            }

            path = armo->worldModels[1].GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        // ----------------------------------------------------
        // Weapon
        // ----------------------------------------------------

        if (auto* weap = base->As<RE::TESObjectWEAP>()) {
            const char* path = weap->GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        // ----------------------------------------------------
        // Container
        // ----------------------------------------------------

        if (auto* cont = base->As<RE::TESObjectCONT>()) {
            const char* path = cont->GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        // ----------------------------------------------------
        // Door
        // ----------------------------------------------------

        if (auto* door = base->As<RE::TESObjectDOOR>()) {
            const char* path = door->GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        // ----------------------------------------------------
        // Furniture
        //
        // TESFurniture -> TESObjectACTI -> TESModelTextureSwap
        // ----------------------------------------------------

        if (auto* furn = base->As<RE::TESFurniture>()) {
            const char* path = furn->GetModel();

            if (path && *path) {
                return path;
            }

            return {};
        }

        return {};
    }

    void ShowNotification(const std::string& msg)
    {
        RE::SendHUDMessage::ShowHUDMessage(msg.c_str());
        SKSE::log::info("Notify: {}", msg);
    }
}
