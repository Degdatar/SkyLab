#pragma once

#include "PCH.h"

namespace SkyLab
{
    class TextureManager
    {
    public:
        struct Texture
        {
            std::string requestedPath;
            std::string resolvedPath;

            ComPtr<ID3D11Resource> resource;
            ComPtr<ID3D11ShaderResourceView> srv;

            uint32_t width{ 0 };
            uint32_t height{ 0 };

            bool loaded{ false };
        };

        static TextureManager& Get()
        {
            static TextureManager instance;
            return instance;
        }

        void Initialize(ID3D11Device* device);
        void Shutdown();

        ID3D11ShaderResourceView*
            GetSRV(const std::string& path);

        std::shared_ptr<Texture>
            Load(const std::string& path);

        void Clear();

        bool ReplaceTextureOnRef(
            RE::TESObjectREFR* ref,
            const std::string& original,
            const std::string& replacement);

        void SetOverride(
            const std::string& original,
            const std::string& replacement);

        bool ReplaceTextureGlobal(
            const std::string& original,
            const std::string& replacement);

        std::string Resolve(
            const std::string& path) const;

    private:
        TextureManager() = default;
        ~TextureManager() = default;

        TextureManager(
            const TextureManager&) = delete;

        TextureManager& operator=(
            const TextureManager&) = delete;

        bool LoadDDS(
            const fs::path& path,
            Texture& texture);

        static std::string Normalize(
            std::string value);

        ID3D11Device* device{ nullptr };

        std::mutex mutex;

        std::unordered_map<
            std::string,
            std::shared_ptr<Texture>>
            cache;

        std::unordered_map<
            std::string,
            std::string>
            overrides;
    };
}