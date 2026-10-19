#include "PCH.h"
#include "TextureManager.h"
#include "Utils.h"
#include <RE/B/BipedAnim.h>

#include <DDSTextureLoader.h>

namespace SkyLab
{
    namespace
    {
        std::string NormalizePath(std::string value)
        {
            std::replace(
                value.begin(),
                value.end(),
                '/',
                '\\');

            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](unsigned char c) {
                    return static_cast<char>(
                        std::tolower(c));
                });

            while (!value.empty() &&
                   (value.front() == '\\' ||
                    value.front() == '/')) {
                value.erase(value.begin());
            }

            return value;
        }

        std::string MakeDisplayPath(const fs::path& path)
        {
            std::string result = path.string();

            std::replace(
                result.begin(),
                result.end(),
                '/',
                '\\');

            return result;
        }
    }

    std::string TextureManager::Normalize(std::string value)
    {
        return NormalizePath(std::move(value));
    }

    void TextureManager::Initialize(ID3D11Device* a_device)
    {
        std::scoped_lock lock(mutex);

        device = a_device;

        cache.clear();
        overrides.clear();

        SKSE::log::info(
            "TextureManager initialized");
    }

    void TextureManager::Shutdown()
    {
        std::scoped_lock lock(mutex);

        cache.clear();
        overrides.clear();

        device = nullptr;

        SKSE::log::info(
            "TextureManager shutdown");
    }

    void TextureManager::Clear()
    {
        std::scoped_lock lock(mutex);

        cache.clear();

        SKSE::log::info(
            "TextureManager cache cleared");
    }

    std::string TextureManager::Resolve(
        const std::string& path) const
    {
        if (path.empty()) {
            return {};
        }

        fs::path requested(path);

        /*
         * Absolute path.
         */
        if (requested.is_absolute() &&
            fs::exists(requested)) {

            return requested.string();
        }

        /*
         * First use SkyLab's existing Skyrim-aware resolver.
         */
        try {
            const fs::path resolved =
                Utils::ResolveTexturePath(path);

            if (!resolved.empty() &&
                fs::exists(resolved)) {

                return resolved.string();
            }
        } catch (...) {
            SKSE::log::warn(
                "Texture resolver threw for '{}'",
                path);
        }

        /*
         * Standard Skyrim Data lookup.
         */
        const fs::path dataDir =
            fs::path("Data");

        const fs::path dataPath =
            dataDir / requested;

        if (fs::exists(dataPath)) {
            return dataPath.string();
        }

        /*
         * Some callers supply:
         *
         *     textures/foo.dds
         *
         * while others supply:
         *
         *     foo.dds
         *
         * Keep both forms working.
         */
        const fs::path texturePath =
            dataDir / "textures" / requested;

        if (fs::exists(texturePath)) {
            return texturePath.string();
        }

        // The file may exist only inside a loaded BSA. Keep the resolved
        // Data path so LoadDDS can ask Skyrim's resource system for bytes.
        return Utils::ResolveTexturePath(path).string();
    }

    std::shared_ptr<TextureManager::Texture>
    TextureManager::Load(const std::string& path)
    {
        if (!device || path.empty()) {
            return nullptr;
        }

        const std::string key =
            Normalize(path);

        {
            std::scoped_lock lock(mutex);

            const auto it = cache.find(key);

            if (it != cache.end()) {
                return it->second;
            }
        }

        const std::string resolved =
            Resolve(path);

        if (resolved.empty()) {
            SKSE::log::warn(
                "TextureManager: texture not found '{}'",
                path);

            return nullptr;
        }

        const fs::path resolvedPath(resolved);

        const std::string extension =
            NormalizePath(
                resolvedPath.extension().string());

        /*
         * The current renderer is DDS based.
         */
        if (extension != ".dds") {
            SKSE::log::warn(
                "TextureManager: unsupported texture format '{}' "
                "for '{}'",
                extension,
                path);

            return nullptr;
        }

        auto texture =
            std::make_shared<Texture>();

        texture->requestedPath = path;
        texture->resolvedPath = resolved;

        if (!LoadDDS(
                resolvedPath,
                *texture)) {

            SKSE::log::warn(
                "TextureManager: failed loading DDS '{}'",
                resolved);

            return nullptr;
        }

        {
            std::scoped_lock lock(mutex);

            /*
             * Another thread may have loaded it while we were doing the
             * actual disk operation.
             */
            const auto [it, inserted] =
                cache.emplace(key, texture);

            if (!inserted) {
                return it->second;
            }
        }

        return texture;
    }

    bool TextureManager::LoadDDS(
        const fs::path& path,
        Texture& texture)
    {
        if (!device) {
            return false;
        }

        std::vector<uint8_t> ddsBytes;
        if (!Utils::ReadResourceFile(path, ddsBytes) || ddsBytes.empty()) {
            SKSE::log::error(
                "TextureManager: DDS could not be read from game resources '{}'",
                path.string());
            return false;
        }

        ComPtr<ID3D11Resource> resource;
        const HRESULT hr = DirectX::CreateDDSTextureFromMemory(
            device,
            ddsBytes.data(),
            ddsBytes.size(),
            resource.ReleaseAndGetAddressOf(),
            texture.srv.ReleaseAndGetAddressOf());

        if (FAILED(hr)) {
            SKSE::log::error(
                "TextureManager: CreateDDSTextureFromMemory failed "
                "for '{}' HRESULT=0x{:08X}",
                path.string(),
                static_cast<unsigned>(hr));

            return false;
        }

        if (resource) {
            texture.resource = resource;

            ComPtr<ID3D11Texture2D> texture2D;
            if (SUCCEEDED(resource.As(&texture2D)) && texture2D) {
                D3D11_TEXTURE2D_DESC description{};
                texture2D->GetDesc(&description);
                texture.width = description.Width;
                texture.height = description.Height;
            }
        } else if (texture.srv) {
            texture.srv->GetResource(
                texture.resource.ReleaseAndGetAddressOf());
        }

        texture.loaded =
            texture.srv != nullptr;

        return texture.loaded;
    }

    ID3D11ShaderResourceView*
    TextureManager::GetSRV(
        const std::string& path)
    {
        if (path.empty()) {
            return nullptr;
        }

        std::string lookupPath = path;

        {
            std::scoped_lock lock(mutex);

            const auto overrideIt =
                overrides.find(Normalize(path));

            if (overrideIt != overrides.end()) {
                lookupPath = overrideIt->second;
            }
        }

        const auto texture =
            Load(lookupPath);

        if (!texture) {
            return nullptr;
        }

        return texture->srv.Get();
    }

    void TextureManager::SetOverride(
        const std::string& original,
        const std::string& replacement)
    {
        if (original.empty() ||
            replacement.empty()) {
            return;
        }

        const std::string originalKey =
            Normalize(original);

        const std::string replacementKey =
            Normalize(replacement);

        {
            std::scoped_lock lock(mutex);

            overrides[originalKey] =
                replacement;

            /*
             * Force the original lookup to resolve through the new
             * replacement on the next request.
             */
            cache.erase(originalKey);
        }

        SKSE::log::info(
            "Texture override: '{}' -> '{}'",
            original,
            replacement);
    }

    bool TextureManager::ReplaceTextureGlobal(
        const std::string& original,
        const std::string& replacement)
    {
        if (original.empty() ||
            replacement.empty()) {
            return false;
        }

        /*
         * Verify that the replacement can actually be loaded before
         * installing the override.
         */
        const auto texture =
            Load(replacement);

        if (!texture ||
            !texture->loaded ||
            !texture->srv) {

            SKSE::log::warn(
                "TextureManager: replacement texture could not "
                "be loaded '{}'",
                replacement);

            return false;
        }

        SetOverride(
            original,
            replacement);

        return true;
    }

    bool TextureManager::ReplaceTextureOnRef(
        RE::TESObjectREFR* ref,
        const std::string& original,
        const std::string& replacement)
    {
        if (!ref) {
            SKSE::log::warn(
                "TextureManager: ReplaceTextureOnRef called "
                "with null reference");

            return false;
        }

        if (original.empty() ||
            replacement.empty()) {
            return false;
        }

        // Confirm the replacement is available to Skyrim before changing
        // any live material. ReadResourceFile honors MO2's process VFS and
        // the game's loaded BSA archives as well as loose files.
        const auto replacementTexture = Load(replacement);
        if (!replacementTexture || !replacementTexture->loaded) {
            SKSE::log::warn("TextureManager: replacement DDS could not be loaded '{}'", replacement);
            return false;
        }

        std::vector<RE::NiAVObject*> roots;
        if (auto* object3D = ref->Get3D()) roots.push_back(object3D);
        if (auto* actor = ref->As<RE::Actor>()) {
            const auto& biped = actor->GetBiped(false);
            if (biped) {
                if (biped->root) roots.push_back(biped->root);
                for (auto& part : biped->objects) {
                    if (auto* clone = part.partClone.get()) roots.push_back(clone);
                }
            }
        }
        if (roots.empty()) {
            SKSE::log::warn("TextureManager: reference {:08X} has no loaded 3D", ref->GetFormID());
            return false;
        }

        const std::string wanted = Normalize(original);
        uint32_t swappedSlots = 0;
        std::unordered_set<RE::BSGeometry*> visited;
        for (auto* root : roots) {
            if (!root) continue;
            RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
                if (!geometry || !visited.insert(geometry).second) return RE::BSVisit::BSVisitControl::kContinue;

                auto* shaderProperty = geometry->lightingShaderProp_cast();
                auto* baseMaterial = shaderProperty ? shaderProperty->GetBaseMaterial() : nullptr;
                auto* material = baseMaterial && baseMaterial->GetType() == RE::BSShaderMaterial::Type::kLighting
                    ? static_cast<RE::BSLightingShaderMaterialBase*>(baseMaterial)
                    : nullptr;
                if (!material) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                auto textureSet = material->GetTextureSet();
                if (!textureSet) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                bool changedGeometry = false;
                for (uint32_t slot = 0; slot < RE::BSTextureSet::Textures::kUsedTotal; ++slot) {
                    const auto textureSlot = static_cast<RE::BSTextureSet::Texture>(slot);
                    const char* currentPath = textureSet->GetTexturePath(textureSlot);
                    if (!currentPath || !*currentPath) {
                        continue;
                    }

                    const std::string current = Normalize(currentPath);
                    const bool matches = wanted.empty()
                        ? textureSlot == RE::BSTextureSet::Textures::kDiffuse
                        : current == wanted || current.ends_with(wanted) || wanted.ends_with(current);
                    if (matches) {
                        textureSet->SetTexturePath(textureSlot, replacement.c_str());
                        ++swappedSlots;
                        changedGeometry = true;
                    }
                }

                if (changedGeometry) {
                    geometry->SetMaterialNeedsUpdate(true);
                }

                return RE::BSVisit::BSVisitControl::kContinue;
            });
        }

        if (swappedSlots == 0) {
            SKSE::log::warn(
                "TextureManager: '{}' was not found on reference {:08X}",
                original,
                ref->GetFormID());
            return false;
        }

        SetOverride(original, replacement);
        SKSE::log::info(
            "TextureManager: replaced {} texture slot(s) on reference {:08X}: '{}' -> '{}'",
            swappedSlots,
            ref->GetFormID(),
            original,
            replacement);
        return true;
    }
}
