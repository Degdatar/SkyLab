#pragma once
namespace SkyLab::Utils {
    std::string ToLower(std::string s);
    std::string WideToUTF8(const std::wstring& w);
    std::wstring UTF8ToWide(const std::string& s);
    std::string GetDataPath();
    std::vector<std::string> ListFilesRecursive(const fs::path& dir, const std::string& ext);
    std::vector<std::string> ListBsaResources(const fs::path& dataRoot, const std::string& ext);
    std::string FormatVec3(float x,float y,float z);
    bool IsDDS(const fs::path& p);
    // Reads a loose file through the active filesystem (including MO2's
    // USVFS), then falls back to Skyrim's virtual resource system for BSA.
    bool ReadResourceFile(const fs::path& path, std::vector<uint8_t>& bytes, bool* viaGameResourceStream = nullptr);
    // Resolve directly through Skyrim's loaded resource locations, including BSAs.
    bool ReadGameResourceFile(const fs::path& path, std::vector<uint8_t>& bytes);
    // Writes an asset under a chosen manager output root (MO2 Overwrite or
    // Skyrim Data), rejecting paths that escape that root.
    bool WriteExportFile(const fs::path& outputRoot, const fs::path& relativePath,
                         const std::vector<uint8_t>& bytes, fs::path* writtenPath = nullptr);
    // Resolve nif path as SKSE sees it: Data/meshes/...
    fs::path ResolveNifPath(const std::string& rel);
    fs::path ResolveTexturePath(const std::string& rel);
    std::string GetCrosshairRefNifPath();
    RE::TESObjectREFR* GetCrosshairRef();
    void ShowNotification(const std::string& msg);
}
