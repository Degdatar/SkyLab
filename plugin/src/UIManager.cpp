#include "PCH.h"
#include "UIManager.h"
#include "Config.h"
#include "Utils.h"
#include "TextureManager.h"
#include "NifParser.h"
#include "ObjNifImporter.h"
#include <imgui_internal.h>
#include <cstddef>
#include <cmath>
#include <RE/B/BipedAnim.h>
#include <RE/B/BGSHeadPart.h>
#include <RE/B/BSModelDB.h>
#include <RE/N/NiNode.h>
#include <RE/B/BGSTextureSet.h>
#include <RE/B/BSTextureSet.h>
#include <RE/T/TESModel.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/T/TESObjectARMA.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectSTAT.h>
#include <RE/T/TESObjectCONT.h>
#include <RE/T/TESObjectDOOR.h>
#include <RE/T/TESFurniture.h>
#include <RE/B/BGSOutfit.h>

namespace SkyLab {

namespace {
    std::string g_imguiLayoutPath;

    fs::path ExportRoot()
    {
        const auto& configuredRoot = Config::Get().exportRoot;
        return configuredRoot.empty() ? fs::path(Utils::GetDataPath()) : fs::path(configuredRoot);
    }

    bool HasResourceFolderPrefix(std::string path, std::string_view folder);

    void ApplyNifToWorldReference(const std::string& resourcePath)
    {
        auto* reference = Utils::GetCrosshairRef();
        if (!reference) {
            Utils::ShowNotification("SkyLab: aim at a world object before applying a NIF.");
            return;
        }
        if (reference->As<RE::Actor>()) {
            Utils::ShowNotification("SkyLab: live NIF replacement is for non-actor world objects; actor rigs need a part-level replacement.");
            return;
        }
        if (reference->GetBaseObject() && reference->GetBaseObject()->As<RE::TESObjectCONT>()) {
            Utils::ShowNotification("SkyLab: live model swaps are disabled for containers so their interaction and collision stay intact. Export the NIF override to change its appearance safely.");
            return;
        }
        if (!HasResourceFolderPrefix(resourcePath, "meshes")) {
            Utils::ShowNotification("SkyLab: NIF path must start with meshes\\.");
            return;
        }
        auto* taskInterface = SKSE::GetTaskInterface();
        if (!taskInterface) {
            Utils::ShowNotification("SkyLab: Skyrim's main-thread task queue is unavailable.");
            return;
        }
        const auto handle = reference->GetHandle();
        taskInterface->AddTask([handle, resourcePath] {
            auto referenceHandle = handle.get();
            if (!referenceHandle) return;
            RE::NiPointer<RE::NiNode> model;
            RE::BSModelDB::DBTraits::ArgsType args{};
            const auto result = RE::BSModelDB::Demand(resourcePath.c_str(), model, args);
            if (result != RE::BSResource::ErrorCode::kNone || !model) {
                Utils::ShowNotification("SkyLab: Skyrim could not load the replacement NIF.");
                return;
            }
            referenceHandle->Set3D(model.get(), true);
            Utils::ShowNotification("SkyLab: replacement NIF applied to the world object.");
        });
        Utils::ShowNotification("SkyLab: applying replacement NIF to the world object...");
    }

    bool HasResourceFolderPrefix(std::string path, std::string_view folder)
    {
        std::replace(path.begin(), path.end(), '/', '\\');
        const std::string prefix = Utils::ToLower(std::string(folder) + "\\");
        return path.size() > prefix.size() && Utils::ToLower(path).rfind(prefix, 0) == 0;
    }

    void AppendFallbackMeshes(NifFile& destination, NifFile&& fallback,
                              const std::unordered_set<std::string>& alreadyCaptured = {})
    {
        if (!fallback.isValid || fallback.meshes.empty() || destination.nodes.empty()) return;
        for (auto& mesh : fallback.meshes) {
            if (alreadyCaptured.contains(Utils::ToLower(mesh.name))) continue;
            const auto meshIndex = static_cast<std::uint32_t>(destination.meshes.size());
            mesh.blockIndex = meshIndex;
            destination.totalVerts += static_cast<std::uint32_t>(mesh.vertices.size());
            destination.totalTris += static_cast<std::uint32_t>(mesh.indices.size() / 3);
            for (const auto& vertex : mesh.vertices) {
                destination.boundsMin.x = std::min(destination.boundsMin.x, vertex.pos.x);
                destination.boundsMin.y = std::min(destination.boundsMin.y, vertex.pos.y);
                destination.boundsMin.z = std::min(destination.boundsMin.z, vertex.pos.z);
                destination.boundsMax.x = std::max(destination.boundsMax.x, vertex.pos.x);
                destination.boundsMax.y = std::max(destination.boundsMax.y, vertex.pos.y);
                destination.boundsMax.z = std::max(destination.boundsMax.z, vertex.pos.z);
            }
            for (const auto* texture : { &mesh.diffuseTexture, &mesh.normalTexture, &mesh.glowTexture, &mesh.specularTexture }) {
                if (!texture->empty() && std::find(destination.textureFiles.begin(), destination.textureFiles.end(), *texture) == destination.textureFiles.end()) {
                    destination.textureFiles.push_back(*texture);
                }
            }
            NifNode node;
            node.name = mesh.name;
            node.blockType = mesh.blockType;
            node.blockIndex = meshIndex;
            node.parent = 0;
            node.meshIndices.push_back(static_cast<std::int32_t>(meshIndex));
            destination.nodes.front().children.push_back(static_cast<std::int32_t>(destination.nodes.size()));
            destination.nodes.push_back(std::move(node));
            destination.meshes.push_back(std::move(mesh));
        }
    }

    std::string NormalizeAssetPath(std::string path, std::string_view category)
    {
        std::replace(path.begin(), path.end(), '/', '\\');
        const std::string lower = Utils::ToLower(path);
        const std::string dataPrefix = Utils::ToLower(Utils::GetDataPath());
        if (!dataPrefix.empty() && lower.rfind(dataPrefix, 0) == 0) path.erase(0, dataPrefix.size());
        else if (lower.rfind("data\\", 0) == 0) path.erase(0, 5);
        while (!path.empty() && path.front() == '\\') path.erase(path.begin());
        if (path.find(':') != std::string::npos) path = fs::path(path).filename().string();
        const std::string expected = std::string(category) + "\\";
        const std::string lowerExpected = Utils::ToLower(expected);
        if (Utils::ToLower(path).rfind(lowerExpected, 0) == 0) path.replace(0, expected.size(), expected);
        else path = expected + path;
        return path;
    }

    std::vector<std::string> NpcFaceGeometryPaths(const RE::TESNPC* npc)
    {
        std::vector<std::string> paths;
        if (!npc) return paths;
        // FaceGen filenames usually use the plugin-local ID. Light-plugin
        // assets may retain the light index in the filename, and some mod
        // outputs use the runtime FormID verbatim, so try all three forms.
        std::vector<std::uint32_t> localIDs{
            npc->GetLocalFormID(),
            npc->GetFormID() & 0x00FFFFFF,
            npc->GetFormID()
        };
        std::sort(localIDs.begin(), localIDs.end());
        localIDs.erase(std::unique(localIDs.begin(), localIDs.end()), localIDs.end());
        std::unordered_set<std::string> owners;
        auto addOwner = [&](const RE::TESFile* owner) {
            if (!owner || owner->GetFilename().empty()) return;
            const auto plugin = Utils::ToLower(std::string(owner->GetFilename()));
            if (!owners.insert(plugin).second) return;
            for (const auto id : localIDs) {
                paths.push_back(std::format("meshes\\actors\\character\\FaceGenData\\FaceGeom\\{}\\{:08X}.nif",
                                            owner->GetFilename(), id));
            }
        };
        // Prefer the winning override, then check the original/master files.
        addOwner(npc->GetFile());
        if (npc->sourceFiles.array) {
            for (std::size_t i = npc->sourceFiles.array->size(); i > 0; --i)
                addOwner(npc->GetFile(static_cast<std::int32_t>(i - 1)));
        }
        return paths;
    }

    std::string NpcFaceTintPathFromGeometry(std::string geometryPath)
    {
        constexpr std::string_view meshPrefix = "meshes\\actors\\character\\FaceGenData\\FaceGeom\\";
        if (Utils::ToLower(geometryPath).rfind(Utils::ToLower(std::string(meshPrefix)), 0) != 0) return {};
        geometryPath.replace(0, meshPrefix.size(), "textures\\actors\\character\\FaceGenData\\FaceTint\\");
        if (geometryPath.size() >= 4 && Utils::ToLower(geometryPath.substr(geometryPath.size() - 4)) == ".nif")
            geometryPath.replace(geometryPath.size() - 4, 4, ".dds");
        return geometryPath;
    }

    void SetFaceTint(NifFile& nif, const std::string& tintPath)
    {
        if (tintPath.empty()) return;
        for (auto& mesh : nif.meshes) {
            if (mesh.diffuseTexture.empty()) mesh.diffuseTexture = tintPath;
        }
        if (std::find(nif.textureFiles.begin(), nif.textureFiles.end(), tintPath) == nif.textureFiles.end())
            nif.textureFiles.push_back(tintPath);
    }

    void RecalculateSceneBounds(NifFile& scene)
    {
        scene.boundsMin = {FLT_MAX, FLT_MAX, FLT_MAX};
        scene.boundsMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        scene.totalVerts = 0;
        scene.totalTris = 0;
        for (const auto& mesh : scene.meshes) {
            scene.totalVerts += static_cast<std::uint32_t>(mesh.vertices.size());
            scene.totalTris += static_cast<std::uint32_t>(mesh.indices.size() / 3);
            for (const auto& vertex : mesh.vertices) {
                const auto position = TransformPreviewPosition(vertex.pos, mesh.previewTransform);
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) continue;
                scene.boundsMin.x = std::min(scene.boundsMin.x, position.x); scene.boundsMin.y = std::min(scene.boundsMin.y, position.y); scene.boundsMin.z = std::min(scene.boundsMin.z, position.z);
                scene.boundsMax.x = std::max(scene.boundsMax.x, position.x); scene.boundsMax.y = std::max(scene.boundsMax.y, position.y); scene.boundsMax.z = std::max(scene.boundsMax.z, position.z);
            }
        }
    }

    struct MaterialPaths {
        std::string diffuse, normal, glow, specular;
    };

    void EnrichSceneTextures(NifFile& scene, const std::vector<std::pair<std::string, RE::BSTextureSet*>>& sources)
    {
        std::unordered_map<std::string, MaterialPaths> byMeshName;
        std::unordered_map<std::string, std::unordered_map<std::string, MaterialPaths>> materialCache;
        auto addTexture = [](std::string& destination, const char* path) {
            if (destination.empty() && path && *path) destination = NormalizeAssetPath(path, "textures");
        };
        for (const auto& [modelPath, skinSet] : sources) {
            std::unordered_map<std::string, MaterialPaths> sourceMaterials;
            if (!modelPath.empty()) {
                const auto key = Utils::ToLower(modelPath);
                auto cached = materialCache.find(key);
                if (cached == materialCache.end()) {
                    auto& entries = materialCache[key];
                    const auto parsed = NifParser::Parse(Utils::ResolveNifPath(modelPath));
                    for (const auto& mesh : parsed.meshes) {
                        auto& paths = entries[Utils::ToLower(mesh.name)];
                        addTexture(paths.diffuse, mesh.diffuseTexture.c_str());
                        addTexture(paths.normal, mesh.normalTexture.c_str());
                        addTexture(paths.glow, mesh.glowTexture.c_str());
                        addTexture(paths.specular, mesh.specularTexture.c_str());
                    }
                    cached = materialCache.find(key);
                }
                sourceMaterials = cached->second;
            }

            if (skinSet) {
                MaterialPaths skin;
                addTexture(skin.diffuse, skinSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse));
                addTexture(skin.normal, skinSet->GetTexturePath(RE::BSTextureSet::Texture::kNormal));
                addTexture(skin.glow, skinSet->GetTexturePath(RE::BSTextureSet::Texture::kGlowMap));
                addTexture(skin.specular, skinSet->GetTexturePath(RE::BSTextureSet::Texture::kSpecular));
                if (!skin.diffuse.empty() || !skin.normal.empty() || !skin.glow.empty() || !skin.specular.empty()) {
                    for (auto& [_, paths] : sourceMaterials) {
                        if (paths.diffuse.empty()) paths.diffuse = skin.diffuse;
                        if (paths.normal.empty()) paths.normal = skin.normal;
                        if (paths.glow.empty()) paths.glow = skin.glow;
                        if (paths.specular.empty()) paths.specular = skin.specular;
                    }
                }
            }
            for (auto& [name, paths] : sourceMaterials) {
                auto& destination = byMeshName[name];
                if (destination.diffuse.empty()) destination.diffuse = paths.diffuse;
                if (destination.normal.empty()) destination.normal = paths.normal;
                if (destination.glow.empty()) destination.glow = paths.glow;
                if (destination.specular.empty()) destination.specular = paths.specular;
            }
        }

        for (auto& mesh : scene.meshes) {
            const auto it = byMeshName.find(Utils::ToLower(mesh.name));
            if (it == byMeshName.end()) continue;
            if (mesh.diffuseTexture.empty()) mesh.diffuseTexture = it->second.diffuse;
            if (mesh.normalTexture.empty()) mesh.normalTexture = it->second.normal;
            if (mesh.glowTexture.empty()) mesh.glowTexture = it->second.glow;
            if (mesh.specularTexture.empty()) mesh.specularTexture = it->second.specular;
            for (const auto* path : {&mesh.diffuseTexture, &mesh.normalTexture, &mesh.glowTexture, &mesh.specularTexture}) {
                if (!path->empty() && std::find(scene.textureFiles.begin(), scene.textureFiles.end(), *path) == scene.textureFiles.end()) {
                    scene.textureFiles.push_back(*path);
                }
            }
        }
    }
}

UIManager& UIManager::Get(){ static UIManager s; return s; }

bool UIManager::Init(ID3D11Device* device_, ID3D11DeviceContext* ctx_, HWND hwnd_){
    std::scoped_lock lock(stateMutex);
    if(initialized) return true;
    device=device_; ctx=ctx_; hwnd=hwnd_;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Skyrim uses a single game HWND and a hidden/recentered hardware cursor.
    // Keep ImGui on that HWND and draw its software cursor in client space.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const fs::path layoutPath = fs::path(Utils::GetDataPath()) / "SKSE" / "Plugins" / "SkyLab_Layout.ini";
    std::error_code layoutError;
    fs::create_directories(layoutPath.parent_path(), layoutError);
    if (!layoutError) {
        g_imguiLayoutPath = layoutPath.string();
        io.IniFilename = g_imguiLayoutPath.c_str();
    } else {
        io.IniFilename = nullptr;
        logger::warn("SkyLab could not create ImGui layout directory '{}': {}", layoutPath.parent_path().string(), layoutError.message());
    }
    ImGui::StyleColorsDark();
    // Skyrim-friendly style
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding=6; s.FrameRounding=4; s.GrabRounding=4; s.ScrollbarRounding=6;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f,0.12f,0.13f,0.96f);
    s.Colors[ImGuiCol_Header]   = ImVec4(0.22f,0.34f,0.68f,0.6f);
    s.Colors[ImGuiCol_TitleBgActive]=ImVec4(0.18f,0.30f,0.60f,1.f);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);
    renderer = std::make_unique<PreviewRenderer>();
    if (!renderer->Init(device)) {
        logger::error("SkyLab preview renderer initialization failed; 3D preview is unavailable");
    }
    TextureManager::Get().Initialize(device_);
    // Load last NIF if any
    auto& cfg = Config::Get();
    if(!cfg.lastNifPath.empty()){
        fs::path p = Utils::ResolveNifPath(cfg.lastNifPath);
        OpenNif(p);
    }
    initialized=true;
    logger::info("SkyLab UI initialized");
    return true;
}
void UIManager::Shutdown(){
    std::scoped_lock lock(stateMutex);
    if(!initialized) return;
    if(gamePauseClaimed){
        if(auto* ui = RE::UI::GetSingleton(); ui && ui->numPausesGame > 0) --ui->numPausesGame;
        gamePauseClaimed=false;
    }
    if(renderer) renderer->Shutdown();
    TextureManager::Get().Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized=false;
}
bool UIManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    std::scoped_lock lock(stateMutex);
    if(!visible || !initialized) return false;
    ImGuiIO& io = ImGui::GetIO();
    extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

    // Skyrim can switch input modes during focus changes (for example after
    // Alt+Tab). Forget the old Raw Input mode so legacy mouse messages can
    // take over until relative Raw Input starts arriving again.
    if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATEAPP && !wParam)) {
        rawMouseActive = false;
        lastRawMouseTick = 0;
        polledMouseButtons.fill(false);
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return false;
    }
    if (msg == WM_SETFOCUS || (msg == WM_ACTIVATEAPP && wParam)) {
        rawMouseActive = false;
        lastRawMouseTick = 0;
        virtualMouseInitialized = false;
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return false;
    }

    const auto hasFreshRawMouse = [&]() {
        if (!rawMouseActive) return false;
        if (GetTickCount64() - lastRawMouseTick > 500) {
            rawMouseActive = false;
            return false;
        }
        return true;
    };
    // Drain raw input so foreground WM_INPUT is released correctly. Mouse
    // buttons are routed through ImGui's Win32 backend; relative deltas drive
    // a software cursor because Skyrim recenters the hardware cursor.
    if(msg == WM_INPUT){
        UINT bytes = 0;
        const auto input = reinterpret_cast<HRAWINPUT>(lParam);
        if (GetRawInputData(input, RID_INPUT, nullptr, &bytes, sizeof(RAWINPUTHEADER)) == 0 && bytes >= sizeof(RAWINPUT)) {
            std::vector<std::byte> storage(bytes);
            UINT readBytes = bytes;
            if (GetRawInputData(input, RID_INPUT, storage.data(), &readBytes, sizeof(RAWINPUTHEADER)) == bytes) {
                const auto* raw = reinterpret_cast<const RAWINPUT*>(storage.data());
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    rawMouseActive = true;
                    lastRawMouseTick = GetTickCount64();
                    if (!virtualMouseInitialized) {
                        POINT p{};
                        if (hwnd && GetCursorPos(&p) && ScreenToClient(hwnd, &p)) virtualMouse = ImVec2(static_cast<float>(p.x), static_cast<float>(p.y));
                        else virtualMouse = ImVec2(1, 1);
                        virtualMouseInitialized = true;
                    }
                    RECT client{};
                    if (hwnd && GetClientRect(hwnd, &client)) {
                        if (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
                            POINT p{};
                            if (GetCursorPos(&p) && ScreenToClient(hwnd, &p)) {
                                virtualMouse.x = static_cast<float>(p.x);
                                virtualMouse.y = static_cast<float>(p.y);
                            }
                        } else {
                            virtualMouse.x += static_cast<float>(raw->data.mouse.lLastX);
                            virtualMouse.y += static_cast<float>(raw->data.mouse.lLastY);
                        }
                        virtualMouse.x = std::clamp(virtualMouse.x, 0.0f, static_cast<float>(std::max<LONG>(0, client.right - 1)));
                        virtualMouse.y = std::clamp(virtualMouse.y, 0.0f, static_cast<float>(std::max<LONG>(0, client.bottom - 1)));
                        io.AddMousePosEvent(virtualMouse.x, virtualMouse.y);
                    }

                    // Skyrim's input layer can consume the legacy mouse
                    // messages before they reach our window hook. Raw Input
                    // still contains the actual button transitions, so route
                    // them to ImGui alongside the relative cursor movement.
                    const USHORT buttons = raw->data.mouse.usButtonFlags;
                    if (buttons & RI_MOUSE_LEFT_BUTTON_DOWN)   io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    if (buttons & RI_MOUSE_LEFT_BUTTON_UP)     io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                    if (buttons & RI_MOUSE_RIGHT_BUTTON_DOWN)  io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
                    if (buttons & RI_MOUSE_RIGHT_BUTTON_UP)    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
                    if (buttons & RI_MOUSE_MIDDLE_BUTTON_DOWN) io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
                    if (buttons & RI_MOUSE_MIDDLE_BUTTON_UP)   io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
                    if (buttons & RI_MOUSE_BUTTON_4_DOWN)      io.AddMouseButtonEvent(3, true);
                    if (buttons & RI_MOUSE_BUTTON_4_UP)        io.AddMouseButtonEvent(3, false);
                    if (buttons & RI_MOUSE_BUTTON_5_DOWN)      io.AddMouseButtonEvent(4, true);
                    if (buttons & RI_MOUSE_BUTTON_5_UP)        io.AddMouseButtonEvent(4, false);
                    if (buttons & RI_MOUSE_WHEEL) {
                        const float wheelDelta = static_cast<float>(static_cast<SHORT>(raw->data.mouse.usButtonData)) / WHEEL_DELTA;
                        io.AddMouseWheelEvent(0.0f, wheelDelta);
                    }
                    if (buttons & RI_MOUSE_HWHEEL) {
                        const float wheelDelta = static_cast<float>(static_cast<SHORT>(raw->data.mouse.usButtonData)) / WHEEL_DELTA;
                        io.AddMouseWheelEvent(wheelDelta, 0.0f);
                    }
                }
            }
        }
        DefWindowProc(hWnd, msg, wParam, lParam);
        return true;
    }

    switch(msg){
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        if (!hasFreshRawMouse()) ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return true;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_MBUTTONUP:
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
        // Prefer Raw Input once Skyrim's relative mouse stream is active;
        // forwarding both paths would enqueue duplicate clicks.
        if (!hasFreshRawMouse()) {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        }
        return true;
    case WM_MOUSEMOVE:
        if (!hasFreshRawMouse()) {
            virtualMouse.x = static_cast<float>(static_cast<short>(LOWORD(lParam)));
            virtualMouse.y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
            virtualMouseInitialized = true;
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        }
        return true;
    case WM_SETCURSOR:
        SetCursor(visible ? nullptr : LoadCursor(nullptr, IDC_ARROW));
        return true;
    default:
        ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam);
        // Once the editor is open, gameplay must not receive key presses.
        // F7 and Escape are handled before this function by InputHook.
        return (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
               (msg >= WM_SYSKEYDOWN && msg <= WM_SYSDEADCHAR) ||
               io.WantCaptureKeyboard;
    }
}

void UIManager::SetVisible(bool value){
    std::scoped_lock lock(stateMutex);
    if(visible == value) return;
    visible = value;
    if(value){
        ClipCursor(nullptr);
        rawMouseActive = false;
        lastRawMouseTick = 0;
        POINT point{};
        if (hwnd && GetCursorPos(&point) && ScreenToClient(hwnd, &point)) {
            virtualMouse = ImVec2(static_cast<float>(point.x), static_cast<float>(point.y));
            virtualMouseInitialized = true;
        }
        if(initialized) ImGui::GetIO().MouseDrawCursor = true;
        if (hwnd) SetCapture(hwnd);
    } else {
        ClipCursor(nullptr);
        if (GetCapture() == hwnd) ReleaseCapture();
        if(initialized){
            ImGuiIO& io = ImGui::GetIO();
            io.MouseDrawCursor = false;
            for (int button = 0; button < 5; ++button) io.AddMouseButtonEvent(button, false);
            polledMouseButtons.fill(false);
        }
    }
    // Mirror a pause-owning Skyrim menu while SkyLab has focus. Only release
    // our own pause increment so another open menu remains paused.
    if(value && !gamePauseClaimed){
        if(auto* ui = RE::UI::GetSingleton()){
            ++ui->numPausesGame;
            gamePauseClaimed=true;
        }
    } else if(!value && gamePauseClaimed){
        if(auto* ui = RE::UI::GetSingleton(); ui && ui->numPausesGame > 0) --ui->numPausesGame;
        gamePauseClaimed=false;
    }
    SKSE::log::info("SkyLab visible {}", visible.load());
}
void UIManager::OnNewFrame(){
    std::scoped_lock lock(stateMutex);
    if(!initialized) return;
    if(visible){
        ClipCursor(nullptr);
        ImGui::GetIO().MouseDrawCursor = true;
        if (hwnd && GetCapture() != hwnd) SetCapture(hwnd);
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (visible && rawMouseActive && virtualMouseInitialized) {
        ImGui::GetIO().AddMousePosEvent(virtualMouse.x, virtualMouse.y);
    }
    if (visible) {
        // Poll as a fallback because Skyrim's input stack may consume both
        // WM_*BUTTON messages and Raw Input button transitions before the
        // overlay hook sees them.
        constexpr int virtualKeys[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        ImGuiIO& io = ImGui::GetIO();
        for (int button = 0; button < 5; ++button) {
            const bool down = (GetAsyncKeyState(virtualKeys[button]) & 0x8000) != 0;
            if (down != polledMouseButtons[button]) {
                io.AddMouseButtonEvent(button, down);
                polledMouseButtons[button] = down;
            }
        }
    }
    ImGui::NewFrame();
}

void UIManager::OpenNif(const fs::path& path){
    std::scoped_lock lock(stateMutex);
    logger::info("Opening NIF {}", path.string());
    errorMsg.clear();
    auto nf = NifParser::Parse(path);

    const auto renderableMeshCount = [](const NifFile& file) {
        return std::count_if(file.meshes.begin(), file.meshes.end(), [](const NifMesh& mesh) {
            return !mesh.vertices.empty() && !mesh.indices.empty();
        });
    };
    bool hasUndecodedMeshes = std::any_of(nf.meshes.begin(), nf.meshes.end(), [](const NifMesh& mesh) {
        return mesh.vertices.empty() || mesh.indices.empty();
    });
    for (const auto& block : nf.blocks) {
        const bool isGeometry = block.type.ends_with("TriShape") || block.type == "NiTriStrips";
        if (isGeometry && std::none_of(nf.meshes.begin(), nf.meshes.end(), [&](const NifMesh& mesh) {
                return mesh.blockIndex == block.index;
            })) {
            hasUndecodedMeshes = true;
            break;
        }
    }

    // Ask Skyrim's own resource/model loader to decode meshes the lightweight
    // parser cannot handle. This also follows the game's active VFS and BSA
    // load order, including MO2's USVFS and Vortex-deployed files.
    if (!nf.isValid || nf.totalVerts == 0 || hasUndecodedMeshes) {
        auto runtimeNif = NifParser::ParseGameResource(path);
        if (runtimeNif.isValid &&
            (!nf.isValid || nf.totalVerts == 0 ||
             runtimeNif.totalVerts > nf.totalVerts ||
             runtimeNif.meshes.size() > static_cast<std::size_t>(renderableMeshCount(nf)))) {
            if (nf.isValid) {
                std::unordered_map<std::string, const NifMesh*> materialByName;
                for (const auto& parsedMesh : nf.meshes)
                    materialByName.insert_or_assign(Utils::ToLower(parsedMesh.name), &parsedMesh);
                for (auto& runtimeMesh : runtimeNif.meshes) {
                    const auto source = materialByName.find(Utils::ToLower(runtimeMesh.name));
                    if (source == materialByName.end()) continue;
                    const auto& material = *source->second;
                    if (runtimeMesh.diffuseTexture.empty()) runtimeMesh.diffuseTexture = material.diffuseTexture;
                    if (runtimeMesh.normalTexture.empty()) runtimeMesh.normalTexture = material.normalTexture;
                    if (runtimeMesh.glowTexture.empty()) runtimeMesh.glowTexture = material.glowTexture;
                    if (runtimeMesh.specularTexture.empty()) runtimeMesh.specularTexture = material.specularTexture;
                }
                runtimeNif.headerString = nf.headerString;
                runtimeNif.versionString = nf.versionString;
                runtimeNif.version = nf.version;
                runtimeNif.userVersion = nf.userVersion;
                runtimeNif.bsVersion = nf.bsVersion;
                runtimeNif.endian = nf.endian;
                runtimeNif.creator = nf.creator;
                runtimeNif.exportScript = nf.exportScript;
                runtimeNif.processScript = nf.processScript;
                runtimeNif.numBlocks = nf.numBlocks;
                runtimeNif.blockTypes = std::move(nf.blockTypes);
                runtimeNif.blocks = std::move(nf.blocks);
                runtimeNif.strings = std::move(nf.strings);
                runtimeNif.collisions = std::move(nf.collisions);
                runtimeNif.bones = std::move(nf.bones);
                runtimeNif.numBones = nf.numBones;
                for (const auto& texture : nf.textureFiles) {
                    if (std::find(runtimeNif.textureFiles.begin(), runtimeNif.textureFiles.end(), texture) == runtimeNif.textureFiles.end()) {
                        runtimeNif.textureFiles.push_back(texture);
                    }
                }
            }
            logger::info("Using Skyrim runtime geometry for {} ({} meshes, {} vertices)",
                path.string(), runtimeNif.meshes.size(), runtimeNif.totalVerts);
            nf = std::move(runtimeNif);
        } else if (!runtimeNif.isValid) {
            logger::warn("Skyrim runtime geometry fallback failed for {}: {}", path.string(), runtimeNif.error);
        }
    }

    if(!nf.isValid){
        errorMsg = nf.error.empty()? "Failed to parse NIF" : nf.error;
        logger::error("NIF parse failed: {}", errorMsg);
        return;
    }
    if (nf.totalVerts == 0) {
        errorMsg = "NIF metadata was read, but Skyrim could not provide preview geometry for it";
        logger::error("{}: {}", path.string(), errorMsg);
        return;
    }
    targetNifPath = NormalizeAssetPath(path.string(), "meshes");
    exportNifPaths = { targetNifPath };
    strncpy_s(nifReplaceSource, targetNifPath.string().c_str(), _TRUNCATE);
    SetCurrentNif(std::move(nf), path, true);
}

void UIManager::SetCurrentNif(NifFile&& nf, const fs::path& path, bool rememberPath){
    std::scoped_lock lock(stateMutex);
    currentNif = std::make_unique<NifFile>(std::move(nf));
    currentPath = path;
    selectedMesh = 0; selectedNode=0;
    if(renderer) renderer->SetNif(currentNif.get());
    // update camera target to bounds center
    camTarget = {(currentNif->boundsMin.x+currentNif->boundsMax.x)*0.5f,
                 (currentNif->boundsMin.y+currentNif->boundsMax.y)*0.5f,
                 (currentNif->boundsMin.z+currentNif->boundsMax.z)*0.5f};
    float ext = std::max({currentNif->boundsMax.x-currentNif->boundsMin.x,
                          currentNif->boundsMax.y-currentNif->boundsMin.y,
                          currentNif->boundsMax.z-currentNif->boundsMin.z});
    camDist = std::max(60.f, ext*1.8f);
    if(rememberPath && !path.empty()){
        Config::Get().lastNifPath = path.string();
        Config::Save();
        recentFiles.insert(recentFiles.begin(), path.string());
        if(recentFiles.size()>10) recentFiles.pop_back();
    }
}
void UIManager::OpenNifFromCrosshair(){
    std::scoped_lock lock(stateMutex);

    // A live reference can be made of several NIFs. NPCs in particular have
    // separate body, head, hair, and equipment shapes rather than one model
    // path, so snapshot the loaded scenegraph first.
    if (auto* ref = Utils::GetCrosshairRef()) {
        if (auto* actor = ref->As<RE::Actor>()) {
            if (auto* npc = actor->GetActorBase()) {
                if (OpenNpcPreview(npc, actor)) {
                    SetVisible(true);
                    return;
                }
            }
        }
        const char* displayName = ref->GetDisplayFullName();
        const std::string label = displayName && *displayName ? displayName : "Selected reference";
        std::vector<RE::NiAVObject*> roots;
        std::vector<RE::NiAVObject*> supplementalRoots;
        std::vector<std::pair<std::string, RE::BSTextureSet*>> materialSources;
        std::vector<std::pair<RE::NiAVObject*, std::string>> bipedPartModels;
        if (auto* actor = ref->As<RE::Actor>()) {
            const auto& biped = actor->GetBiped(false);
            if (biped) {
                std::size_t partIndex = 0;
                for (auto& part : biped->objects) {
                    // Capture the assembled actor and its biped clones. The
                    // capture walker de-duplicates shared scene nodes, while
                    // these roots cover slots Skyrim keeps outside Get3D().
                    if (part.partClone)
                        supplementalRoots.push_back(part.partClone.get());
                    const char* model = part.part ? part.part->GetModel() : nullptr;
                    if (model && *model) materialSources.emplace_back(model, part.skinTexture);
                    if (part.partClone && model && *model) bipedPartModels.emplace_back(part.partClone.get(), model);
                    ++partIndex;
                }
            }
            for (std::uint32_t headPart = 0;
                 headPart < static_cast<std::uint32_t>(RE::BGSHeadPart::HeadPartType::kTotal);
                 ++headPart) {
                if (auto* headObject = actor->GetHeadPartObject(static_cast<RE::BGSHeadPart::HeadPartType>(headPart)))
                    supplementalRoots.push_back(headObject);
            }
        }
        if (auto* reference3D = ref->Get3D()) {
            // Capture the actor's assembled scenegraph once. Adding its biped
            // clones as independent roots duplicates or misplaces body parts.
            roots.push_back(reference3D);
        } else if (auto* actor = ref->As<RE::Actor>()) {
            const auto& biped = actor->GetBiped(false);
            if (biped && biped->root) roots.push_back(biped->root);
        }
        roots.insert(roots.end(), supplementalRoots.begin(), supplementalRoots.end());
        const std::string sourceNif = Utils::GetCrosshairRefNifPath();
        if (!sourceNif.empty()) materialSources.emplace_back(sourceNif, nullptr);
        auto liveScene = NifParser::CaptureScene(roots, label);
        if (liveScene.isValid) {
            // Some actor parts expose no CPU-readable runtime vertex buffer.
            // Fall back to the corresponding source NIF for each unavailable
            // biped slot (head, body, hands, feet, and worn armor), not only
            // the body slot. Keep live geometry when it was captured.
            for (const auto& [partClone, modelPath] : bipedPartModels) {
                const auto livePart = NifParser::CaptureScene(partClone, label + " part");
                std::unordered_set<std::string> capturedNames;
                if (livePart.isValid) {
                    for (const auto& mesh : livePart.meshes)
                        if (!mesh.vertices.empty() && !mesh.indices.empty()) capturedNames.insert(Utils::ToLower(mesh.name));
                }
                const auto partPath = Utils::ResolveNifPath(modelPath);
                auto fallback = NifParser::Parse(partPath);
                const bool hasUndecodedPart = std::any_of(fallback.meshes.begin(), fallback.meshes.end(), [](const NifMesh& mesh) {
                    return mesh.vertices.empty() || mesh.indices.empty();
                }) || std::any_of(fallback.blocks.begin(), fallback.blocks.end(), [&](const NifBlockInfo& block) {
                    return (block.type.ends_with("TriShape") || block.type == "NiTriStrips") &&
                        std::none_of(fallback.meshes.begin(), fallback.meshes.end(), [&](const NifMesh& mesh) { return mesh.blockIndex == block.index; });
                });
                if (!fallback.isValid || fallback.totalVerts == 0 || hasUndecodedPart) {
                    auto runtime = NifParser::ParseGameResource(partPath);
                    if (runtime.isValid && (runtime.totalVerts > fallback.totalVerts || runtime.meshes.size() > fallback.meshes.size())) fallback = std::move(runtime);
                }
                const auto decodedCount = std::count_if(fallback.meshes.begin(), fallback.meshes.end(), [&](const NifMesh& mesh) {
                    return !mesh.vertices.empty() && !mesh.indices.empty() && !capturedNames.contains(Utils::ToLower(mesh.name));
                });
                if (fallback.isValid && fallback.totalVerts > 0 && decodedCount > 0) {
                    logger::info("Using bind-pose actor-part fallback for '{}': {}", label, modelPath);
                    AppendFallbackMeshes(liveScene, std::move(fallback), capturedNames);
                } else {
                    logger::warn("Actor part '{}' has no preview geometry; source NIF '{}' could not be decoded", label, modelPath);
                }
            }
            EnrichSceneTextures(liveScene, materialSources);
            logger::info("Opening live reference preview: {}", label);
            errorMsg.clear();
            targetNifPath = sourceNif.empty() ? fs::path{} : fs::path(NormalizeAssetPath(sourceNif, "meshes"));
            exportNifPaths.clear();
            auto addExportNif = [&](const std::string& source) {
                if (source.empty()) return;
                const fs::path relative = NormalizeAssetPath(source, "meshes");
                if (Utils::ToLower(relative.extension().string()) != ".nif") return;
                if (std::none_of(exportNifPaths.begin(), exportNifPaths.end(), [&](const fs::path& existing) {
                        return Utils::ToLower(existing.string()) == Utils::ToLower(relative.string());
                    })) exportNifPaths.push_back(relative);
            };
            for (const auto& [modelPath, _] : materialSources) addExportNif(modelPath);
            addExportNif(sourceNif);
            strncpy_s(nifReplaceSource, targetNifPath.string().c_str(), _TRUNCATE);
            SetCurrentNif(std::move(liveScene), {}, false);
            SetVisible(true);
            return;
        }
        logger::debug("Live reference snapshot unavailable for '{}': {}", label, liveScene.error);
    }

    std::string p = Utils::GetCrosshairRefNifPath();
    if(p.empty()){
        Utils::ShowNotification("SkyLab: This reference has no loaded mesh or supported NIF path.");
        return;
    }
    fs::path fp = Utils::ResolveNifPath(p);
    OpenNif(fp);
    SetVisible(true);
}

void UIManager::DrawMenuBar(){
    if(ImGui::BeginMenuBar()){
        if(ImGui::BeginMenu("File")){
            if(ImGui::MenuItem("Open NIF / Game Resource...","Ctrl+O")){
                showOpenNif=true;
            }
            if(ImGui::MenuItem("Import OBJ as Skyrim SE NIF...")){
                showObjImport=true;
            }
            if(ImGui::MenuItem("Open from Crosshair","F7")) OpenNifFromCrosshair();
            if(ImGui::MenuItem("List loose NIFs in Data/meshes...")){
                recentFiles = Utils::ListFilesRecursive(fs::path(Utils::GetDataPath())/"meshes", ".nif");
                if(recentFiles.size()>100) recentFiles.resize(100);
                showLooseNifList=true;
            }
            if(ImGui::MenuItem("Export Current NIF and Textures...")) ExportCurrentPreview();
            ImGui::Separator();
            if(ImGui::MenuItem("Exit Overlay","Esc")) SetVisible(false);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("View")){
            ImGui::MenuItem("Wireframe","", &optWireframe);
            ImGui::MenuItem("Collision","", &optCollision);
            ImGui::MenuItem("Vertex Normals","", &optNormals);
            ImGui::MenuItem("UV Overlay","", &optUV);
            if(renderer) renderer->SetOptions(optWireframe, optCollision, optNormals, optUV);
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Tools")){
            if(ImGui::MenuItem("Replace Texture...")) {
                if (currentNif && selectedMesh >= 0 && selectedMesh < static_cast<int>(currentNif->meshes.size()))
                    strncpy_s(texReplaceOrig, currentNif->meshes[selectedMesh].diffuseTexture.c_str(), _TRUNCATE);
                const auto source = !targetNifPath.empty() ? targetNifPath.string() :
                    (!exportNifPaths.empty() ? exportNifPaths.front().string() : std::string{});
                strncpy_s(texReplaceNifSource, source.c_str(), _TRUNCATE);
                showTexReplace=true;
            }
            if(ImGui::MenuItem("Replace NIF...")) {
                const auto source = !targetNifPath.empty() ? targetNifPath.string() :
                    (!exportNifPaths.empty() ? exportNifPaths.front().string() : std::string{});
                strncpy_s(nifReplaceSource, source.c_str(), _TRUNCATE);
                nifReplaceDestination[0] = '\0';
                showNifReplace=true;
            }
            if(ImGui::MenuItem("Export Settings...")) {
                strncpy_s(exportRootInput, Config::Get().exportRoot.c_str(), _TRUNCATE);
                showExportSettings=true;
            }
            if(ImGui::MenuItem("Reload Current NIF")){
                if(!currentPath.empty()) OpenNif(currentPath);
            }
            ImGui::EndMenu();
        }
        if(ImGui::BeginMenu("Help")){
            if(ImGui::MenuItem("About SkyLab")){
                showAbout = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void UIManager::ExportCurrentPreview()
{
    if (!currentNif) {
        Utils::ShowNotification("SkyLab: no source NIF is available to export for this preview.");
        return;
    }
    std::vector<fs::path> nifsToExport;
    auto addNifPath = [&](std::string path) {
        if (path.empty()) return;
        const auto relative = fs::path(NormalizeAssetPath(std::move(path), "meshes"));
        const auto key = Utils::ToLower(relative.string());
        if (std::none_of(nifsToExport.begin(), nifsToExport.end(), [&](const fs::path& existing) {
                return Utils::ToLower(existing.string()) == key;
            })) nifsToExport.push_back(relative);
    };
    std::vector<fs::path> exportTexturePaths;
    auto addTexturePath = [&](std::string path) {
        if (path.empty()) return;
        const auto relative = fs::path(NormalizeAssetPath(std::move(path), "textures"));
        const std::string key = Utils::ToLower(relative.string());
        if (std::none_of(exportTexturePaths.begin(), exportTexturePaths.end(), [&](const fs::path& existing) {
                return Utils::ToLower(existing.string()) == key;
            })) exportTexturePaths.push_back(relative);
    };
    for (const auto& mesh : currentNif->meshes) {
        addNifPath(mesh.sourcePath);
        addTexturePath(mesh.diffuseTexture);
        addTexturePath(mesh.normalTexture);
        addTexturePath(mesh.glowTexture);
        addTexturePath(mesh.specularTexture);
    }
    // File-backed previews may not have per-shape source metadata. In that
    // case retain the original whole-NIF copy behavior.
    if (nifsToExport.empty()) {
        for (const auto& path : exportNifPaths) addNifPath(path.string());
    }
    if (nifsToExport.empty()) {
        Utils::ShowNotification("SkyLab: no source NIF is available to export.");
        return;
    }
    for (const auto& texture : currentNif->textureFiles) addTexturePath(texture);

    std::size_t nifsWritten = 0;
    std::size_t texturesWritten = 0;
    bool complete = true;
    for (const auto& relativeNif : nifsToExport) {
        const auto cleanNif = fs::path(NormalizeAssetPath(relativeNif.string(), "meshes"));
        std::vector<std::uint8_t> bytes;
        if (!Utils::ReadResourceFile(Utils::ResolveNifPath(cleanNif.string()), bytes) || bytes.empty() ||
            !Utils::WriteExportFile(ExportRoot(), cleanNif, bytes)) {
            complete = false;
            continue;
        }
        ++nifsWritten;
    }
    for (const auto& relativeTexture : exportTexturePaths) {
        std::vector<std::uint8_t> bytes;
        if (!Utils::ReadResourceFile(Utils::ResolveTexturePath(relativeTexture.string()), bytes) || bytes.empty() ||
            !Utils::WriteExportFile(ExportRoot(), relativeTexture, bytes)) {
            complete = false;
            continue;
        }
        ++texturesWritten;
    }
    if (nifsWritten == 0) {
        Utils::ShowNotification("SkyLab: source NIFs could not be read; nothing was exported.");
        return;
    }
    Utils::ShowNotification(complete
        ? "SkyLab: exported " + std::to_string(nifsWritten) + " NIF(s) and " + std::to_string(texturesWritten) + " texture(s) to " + ExportRoot().string()
        : "SkyLab: exported " + std::to_string(nifsWritten) + " NIF(s) and " + std::to_string(texturesWritten) + " texture(s); some source files were unavailable.");
}

void UIManager::DrawSceneTree(){
    ImGui::InputTextWithHint("##search","Search nodes/meshes...",searchBuf, sizeof(searchBuf));
    ImGui::Separator();
    if(!currentNif){
        ImGui::TextDisabled("No NIF loaded.");
        ImGui::TextWrapped("Press F7 on a crosshair object, or File -> Open NIF.");
        if(!errorMsg.empty()){ ImGui::Spacing(); ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"%s",errorMsg.c_str());}
        return;
    }
    std::string lowSearch = Utils::ToLower(searchBuf);
    auto matches = [&](const std::string& s)->bool{
        if(lowSearch.empty()) return true;
        return Utils::ToLower(s).find(lowSearch)!=std::string::npos;
    };
    if(ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextDisabled("%s", currentPath.filename().string().c_str());
        ImGui::TreePop();
    }
    if(ImGui::CollapsingHeader("Scene Tree", ImGuiTreeNodeFlags_DefaultOpen)){
        for(size_t i=0;i<currentNif->nodes.size();++i){
            auto &n = currentNif->nodes[i];
            if(!matches(n.name) && !matches(n.blockType)) continue;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if((int)i==selectedNode) flags |= ImGuiTreeNodeFlags_Selected;
            bool open = ImGui::TreeNodeEx((void*)(intptr_t)i, flags, "%s  [%s]", n.name.empty()? "<unnamed>":n.name.c_str(), n.blockType.c_str());
            if(ImGui::IsItemClicked()) selectedNode=(int)i;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Block #%u  Children:%zu  Meshes:%zu", n.blockIndex, n.children.size(), n.meshIndices.size());
            (void)open;
            // drop meshes under node
            for(int mi: n.meshIndices){
                if(mi<0||mi>=(int)currentNif->meshes.size()) continue;
                auto &m = currentNif->meshes[mi];
                if(!matches(m.name) && !matches(m.blockType)) continue;
                ImGui::Indent(16);
                ImGuiTreeNodeFlags mf = ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if(mi==selectedMesh) mf|=ImGuiTreeNodeFlags_Selected;
                ImGui::TreeNodeEx((void*)(intptr_t)(10000+mi), mf, "  %s  (%s)  %zu verts", m.name.c_str(), m.blockType.c_str(), m.vertices.size());
                if(ImGui::IsItemClicked()){ selectedMesh=mi; if(renderer) renderer->SetSelectedMesh(mi);} 
                if(ImGui::BeginPopupContextItem()){
                    if(ImGui::MenuItem("Replace Texture...")){ pendingOrig=m.diffuseTexture; strncpy_s(texReplaceOrig, pendingOrig.c_str(), _TRUNCATE); showTexReplace=true; }
                    if(ImGui::MenuItem("Copy Name")) ImGui::SetClipboardText(m.name.c_str());
                    ImGui::EndPopup();
                }
                ImGui::Unindent(16);
            }
        }
    }
    if(ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)){
        if(currentNif->meshes.empty()){
            ImGui::TextDisabled("No mesh blocks found in this NIF.");
        }
        for(size_t i=0;i<currentNif->meshes.size();++i){
            auto& m=currentNif->meshes[i];
            if(!matches(m.name) && !matches(m.blockType)) continue;
            ImGui::PushID(static_cast<int>(i));
            const bool selected = selectedMesh == static_cast<int>(i);
            if(ImGui::Selectable((m.name + "  [" + m.blockType + "]").c_str(), selected)){
                selectedMesh=static_cast<int>(i);
                if(renderer) renderer->SetSelectedMesh(selectedMesh);
            }
            if(ImGui::BeginPopupContextItem()){
                if(ImGui::MenuItem("Select")){
                    selectedMesh=static_cast<int>(i);
                    if(renderer) renderer->SetSelectedMesh(selectedMesh);
                }
                if(ImGui::MenuItem("Replace Texture...")){
                    selectedMesh=static_cast<int>(i);
                    if(renderer) renderer->SetSelectedMesh(selectedMesh);
                    pendingOrig=m.diffuseTexture;
                    strncpy_s(texReplaceOrig, pendingOrig.c_str(), _TRUNCATE);
                    showTexReplace=true;
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu verts / %zu tris", m.vertices.size(), m.indices.size()/3);
            ImGui::PopID();
        }
    }
    if(ImGui::CollapsingHeader("Collision", optCollision? ImGuiTreeNodeFlags_DefaultOpen:0)){
        if(currentNif->collisions.empty()) ImGui::TextDisabled("No collision (bhk) blocks.");
        else for(size_t i=0;i<currentNif->collisions.size();++i){
            auto &c=currentNif->collisions[i];
            ImGui::BulletText("%s  verts:%zu", c.shapeType.c_str(), c.vertices.size());
        }
    }
    if(ImGui::CollapsingHeader("Bones / Skin")){
        if(currentNif->bones.empty()) ImGui::TextDisabled("No skin / skeleton (%u bones).", currentNif->numBones);
        else {
            ImGui::Text("%u bones", (uint32_t)currentNif->bones.size());
            ImGui::BeginChild("bones", ImVec2(0,80), true);
            for(auto &b: currentNif->bones) ImGui::TextUnformatted(b.c_str());
            ImGui::EndChild();
        }
    }
}
void UIManager::DrawProperties(){
    if(!currentNif){ ImGui::TextDisabled("No selection."); return; }
    // Mesh properties
    if(selectedMesh>=0 && selectedMesh < (int)currentNif->meshes.size()){
        auto &m = currentNif->meshes[selectedMesh];
        ImGui::TextUnformatted("Mesh");
        ImGui::Separator();
        ImGui::Text("Name: %s", m.name.c_str());
        ImGui::Text("Type: %s (#%u)", m.blockType.c_str(), m.blockIndex);
        std::string sourcePath = m.sourcePath;
        if (!sourcePath.empty()) {
            std::replace(sourcePath.begin(), sourcePath.end(), '/', '\\');
            const auto meshFolder = Utils::ToLower(sourcePath).find("meshes\\");
            if (meshFolder != std::string::npos) sourcePath.erase(0, meshFolder);
            else sourcePath = NormalizeAssetPath(sourcePath, "meshes");
        }
        else if (!targetNifPath.empty()) sourcePath = targetNifPath.string();
        if (sourcePath.empty()) sourcePath = "Runtime scene snapshot (source NIF varies)";
        ImGui::TextWrapped("NIF path: %s", sourcePath.c_str());
        if (ImGui::SmallButton("Copy NIF path")) ImGui::SetClipboardText(sourcePath.c_str());
        ImGui::Text("Verts: %zu  Tris: %zu", m.vertices.size(), m.indices.size()/3);
        if(m.vertices.empty() || m.indices.empty()){
            ImGui::TextWrapped("This NIF block is recognized, but its geometry payload has not been decoded yet. Texture replacement can be configured, but there is no mesh geometry to display in the preview.");
        }
        ImGui::Text("Shader: %s", m.shaderType.empty()?"Default":m.shaderType.c_str());
        ImGui::Text("Collision: %s", optCollision?"YES":"NO");
        ImGui::Text("Bones: %u", currentNif->numBones);
        ImGui::Spacing();
        ImGui::TextUnformatted("Textures");
        ImGui::Separator();
        auto texRow = [&](const char* label, const std::string& p){
            ImGui::Text("%s:", label);
            if(p.empty()) ImGui::TextDisabled("<none>");
            else {
                ImGui::TextWrapped("%s", p.c_str());
                ImGui::SameLine();
                char id[64]; snprintf(id,64,"copy##%s",label);
                if(ImGui::SmallButton(id)) ImGui::SetClipboardText(p.c_str());
            }
        };
        texRow("Diffuse", m.diffuseTexture);
        texRow("Normal ", m.normalTexture);
        texRow("Specular", m.specularTexture);
        if(ImGui::Button("Replace Diffuse...")){
            pendingOrig=m.diffuseTexture; strncpy_s(texReplaceOrig, pendingOrig.c_str(), _TRUNCATE); showTexReplace=true;
            const auto source = !targetNifPath.empty() ? targetNifPath.string() :
                (!exportNifPaths.empty() ? exportNifPaths.front().string() : std::string{});
            strncpy_s(texReplaceNifSource, source.c_str(), _TRUNCATE);
        }
        ImGui::SameLine();
        if(ImGui::Button("Replace NIF...")) {
            const auto source = !targetNifPath.empty() ? targetNifPath.string() :
                (!exportNifPaths.empty() ? exportNifPaths.front().string() : std::string{});
            strncpy_s(nifReplaceSource, source.c_str(), _TRUNCATE);
            nifReplaceDestination[0] = '\0';
            showNifReplace=true;
        }
        ImGui::Spacing();
        if(ImGui::CollapsingHeader("Transform")){
            bool changed = false;
            changed |= ImGui::DragFloat3("Translation", &m.previewTransform.translation.x, 0.1f, -100000.0f, 100000.0f, "%.2f");
            changed |= ImGui::DragFloat3("Rotation (degrees)", &m.previewTransform.rotationDegrees.x, 0.5f, -180.0f, 180.0f, "%.1f");
            changed |= ImGui::DragFloat("Scale", &m.previewTransform.scale, 0.01f, 0.01f, 100.0f, "%.3f");
            changed |= ImGui::DragFloat3("Axis Scale", &m.previewTransform.axisScale.x, 0.01f, 0.01f, 100.0f, "%.3f");
            changed |= ImGui::Checkbox("Mirror X", &m.previewTransform.mirrorX);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Mirror Y", &m.previewTransform.mirrorY);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Mirror Z", &m.previewTransform.mirrorZ);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset##meshTransform")) {
                m.previewTransform = {};
                changed = true;
            }
            if (changed && renderer) renderer->SetNif(currentNif.get());
        }
        if(ImGui::CollapsingHeader("Vertex Sample")){
            ImGui::BeginChild("verts", ImVec2(0,100), true);
            for(size_t i=0;i<std::min<size_t>(m.vertices.size(), 50); ++i){
                auto &v=m.vertices[i];
                ImGui::Text("%3zu: pos %.1f %.1f %.1f  uv %.2f %.2f", i, v.pos.x,v.pos.y,v.pos.z, v.uv.u, v.uv.v);
            }
            ImGui::EndChild();
        }
    } else if(selectedNode>=0 && selectedNode < (int)currentNif->nodes.size()){
        auto &n=currentNif->nodes[selectedNode];
        ImGui::TextUnformatted("Node");
        ImGui::Separator();
        ImGui::Text("Name: %s", n.name.c_str());
        ImGui::Text("Type: %s", n.blockType.c_str());
        ImGui::Text("Children: %zu  Meshes: %zu", n.children.size(), n.meshIndices.size());
        ImGui::Text("Translation: %s", Utils::FormatVec3(n.transform.translation.x,n.transform.translation.y,n.transform.translation.z).c_str());
    } else {
        ImGui::TextDisabled("Select a mesh or node in Scene Tree.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    // Global stats
    ImGui::TextDisabled("%s", NifParser::Summarize(*currentNif).c_str());
    if(renderer){
        auto st=renderer->GetStats();
        ImGui::TextDisabled("Rendered: %u verts %u tris", st.verts, st.tris);
    }
}

void UIManager::DrawViewport(ID3D11Device* device_, ID3D11DeviceContext* ctx_){
    // Create viewport RT if needed
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float vw = std::max(320.f, avail.x);
    float vh = std::max(240.f, avail.y);
    if(!viewportTex || viewportW!=vw || viewportH!=vh){
        viewportW=vw; viewportH=vh;
        D3D11_TEXTURE2D_DESC td{}; td.Width=(UINT)vw; td.Height=(UINT)vh; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        viewportTex.Reset();
        viewportRTV.Reset();
        viewportSRV.Reset();
        HRESULT hr = device_->CreateTexture2D(&td,nullptr,&viewportTex);
        if (SUCCEEDED(hr)) hr = device_->CreateRenderTargetView(viewportTex.Get(),nullptr,&viewportRTV);
        if (SUCCEEDED(hr)) hr = device_->CreateShaderResourceView(viewportTex.Get(),nullptr,&viewportSRV);
        if (FAILED(hr)) {
            viewportTex.Reset();
            viewportRTV.Reset();
            viewportSRV.Reset();
            logger::error("SkyLab viewport: failed to create render target ({}x{}): 0x{:08X}", td.Width, td.Height, static_cast<uint32_t>(hr));
        }
    }
    // Controls hint
    ImGui::TextDisabled("LMB drag: orbit | LMB click: select mesh | RMB drag: pan");
    // Render mesh into RT
    if(renderer && currentNif){
        renderer->UpdateCamera(camYaw,camPitch,camDist,camTarget);
        renderer->Render(ctx_, viewportRTV.Get(), vw, vh);
    } else {
        float col[4]={0.10f,0.10f,0.12f,1}; ctx_->ClearRenderTargetView(viewportRTV.Get(), col);
    }
    // Display RT as image
    ImGui::Image((ImTextureID)viewportSRV.Get(), ImVec2(vw, vh));
    ImVec2 imageMin = ImGui::GetItemRectMin();
    const auto previewStats = renderer ? renderer->GetStats() : PreviewRenderer::Stats{};
    bool cpuPreviewDrawn = false;
    if (currentNif && previewStats.meshes == 0) {
        // Keep a usable geometry preview even if Skyrim's D3D device rejects
        // SkyLab's vertex/index buffers. Project the parsed triangles into
        // the viewport's ImGui draw list as a shaded CPU fallback.
        struct CpuFace { ImVec2 a, b, c; ImVec2 uvA, uvB, uvC; float depth; ImU32 color; ImTextureID texture; };
        std::vector<CpuFace> faces;
        const float eyeX = camTarget.x + cosf(camPitch) * sinf(camYaw) * camDist;
        const float eyeY = camTarget.y + cosf(camPitch) * cosf(camYaw) * camDist;
        const float eyeZ = camTarget.z + sinf(camPitch) * camDist;
        struct V3 { float x, y, z; };
        auto sub = [](V3 a, V3 b) { return V3{a.x-b.x, a.y-b.y, a.z-b.z}; };
        auto dot = [](V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
        auto cross = [](V3 a, V3 b) { return V3{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; };
        auto normalize = [&](V3 v) {
            const float length = sqrtf(std::max(1.0e-20f, dot(v, v)));
            return V3{v.x/length, v.y/length, v.z/length};
        };
        const V3 eye{eyeX, eyeY, eyeZ};
        const V3 forward = normalize(sub({camTarget.x, camTarget.y, camTarget.z}, eye));
        const V3 right = normalize(cross({0,0,1}, forward));
        const V3 cameraUp = normalize(cross(forward, right));
        const float aspect = vw / std::max(1.0f, vh);
        const float focal = 1.0f / tanf(DirectX::XMConvertToRadians(60.0f) * 0.5f);
        auto project = [&](const NifVertex& vertex, ImVec2& screen, float& depth) {
            const V3 relative{vertex.pos.x-eye.x, vertex.pos.y-eye.y, vertex.pos.z-eye.z};
            depth = dot(relative, forward);
            if (depth <= 0.01f) return false;
            const float nx = dot(relative, right) * focal / (depth * aspect);
            const float ny = dot(relative, cameraUp) * focal / depth;
            if (!std::isfinite(nx) || !std::isfinite(ny) || fabsf(nx) > 4.0f || fabsf(ny) > 4.0f) return false;
            screen = ImVec2(imageMin.x + (nx * 0.5f + 0.5f) * vw,
                            imageMin.y + (0.5f - ny * 0.5f) * vh);
            return true;
        };
        constexpr size_t kMaxCpuTriangles = 100000;
        for (size_t meshIndex = 0; meshIndex < currentNif->meshes.size() && faces.size() < kMaxCpuTriangles; ++meshIndex) {
            const auto& mesh = currentNif->meshes[meshIndex];
            ID3D11ShaderResourceView* diffuse = TextureManager::Get().GetSRV(mesh.diffuseTexture);
            for (size_t i = 0; i + 2 < mesh.indices.size() && faces.size() < kMaxCpuTriangles; i += 3) {
                const uint32_t ia = mesh.indices[i], ib = mesh.indices[i+1], ic = mesh.indices[i+2];
                if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
                NifVertex vertexA = mesh.vertices[ia];
                NifVertex vertexB = mesh.vertices[ib];
                NifVertex vertexC = mesh.vertices[ic];
                vertexA.pos = TransformPreviewPosition(vertexA.pos, mesh.previewTransform);
                vertexB.pos = TransformPreviewPosition(vertexB.pos, mesh.previewTransform);
                vertexC.pos = TransformPreviewPosition(vertexC.pos, mesh.previewTransform);
                ImVec2 a{}, b{}, c{}; float da=0, db=0, dc=0;
                if (!project(vertexA, a, da) || !project(vertexB, b, db) || !project(vertexC, c, dc)) continue;
                const V3 pa{vertexA.pos.x, vertexA.pos.y, vertexA.pos.z};
                const V3 pb{vertexB.pos.x, vertexB.pos.y, vertexB.pos.z};
                const V3 pc{vertexC.pos.x, vertexC.pos.y, vertexC.pos.z};
                const V3 normal = normalize(cross(sub(pb, pa), sub(pc, pa)));
                const float light = 0.28f + 0.72f * fabsf(dot(normal, normalize({0.35f, 0.55f, 0.76f})));
                const bool selected = static_cast<int>(meshIndex) == selectedMesh;
                const int baseR = diffuse ? static_cast<int>(255*light) : (selected ? 80 : static_cast<int>(165*light));
                const int baseG = diffuse ? static_cast<int>(255*light) : (selected ? 155 : static_cast<int>(175*light));
                const int baseB = diffuse ? static_cast<int>(255*light) : (selected ? 235 : static_cast<int>(185*light));
                const ImU32 color = IM_COL32(baseR, baseG, baseB, 255);
                faces.push_back({a, b, c,
                    ImVec2(mesh.vertices[ia].uv.u, mesh.vertices[ia].uv.v),
                    ImVec2(mesh.vertices[ib].uv.u, mesh.vertices[ib].uv.v),
                    ImVec2(mesh.vertices[ic].uv.u, mesh.vertices[ic].uv.v),
                    (da+db+dc)/3.0f, color, reinterpret_cast<ImTextureID>(diffuse)});
            }
        }
        if (!faces.empty()) {
            std::sort(faces.begin(), faces.end(), [](const CpuFace& a, const CpuFace& b) { return a.depth > b.depth; });
            ImDrawList* fallbackDraw = ImGui::GetWindowDrawList();
            fallbackDraw->PushClipRect(imageMin, ImVec2(imageMin.x + vw, imageMin.y + vh), true);
            for (const auto& face : faces) {
                if (face.texture) {
                    fallbackDraw->AddImageQuad(face.texture, face.a, face.b, face.c, face.c,
                        face.uvA, face.uvB, face.uvC, face.uvC, face.color);
                } else {
                    fallbackDraw->AddTriangleFilled(face.a, face.b, face.c, face.color);
                }
            }
            fallbackDraw->PopClipRect();
            cpuPreviewDrawn = true;
        }
    }
    // Interaction
    if(ImGui::IsItemHovered()){
        ImGuiIO& io=ImGui::GetIO();
        if (currentNif && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            struct PickVec3 { float x, y, z; };
            const auto subtract = [](PickVec3 a, PickVec3 b) { return PickVec3{a.x-b.x, a.y-b.y, a.z-b.z}; };
            const auto dot = [](PickVec3 a, PickVec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; };
            const auto cross = [](PickVec3 a, PickVec3 b) { return PickVec3{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; };
            const float eyeX = camTarget.x + cosf(camPitch) * sinf(camYaw) * camDist;
            const float eyeY = camTarget.y + cosf(camPitch) * cosf(camYaw) * camDist;
            const float eyeZ = camTarget.z + sinf(camPitch) * camDist;
            const PickVec3 eye{eyeX, eyeY, eyeZ};
            auto normalize = [&](PickVec3 v) {
                const float length = sqrtf(std::max(1.0e-20f, dot(v, v)));
                return PickVec3{v.x/length, v.y/length, v.z/length};
            };
            const PickVec3 forward = normalize(subtract({camTarget.x, camTarget.y, camTarget.z}, eye));
            const PickVec3 right = normalize(cross({0,0,1}, forward));
            const PickVec3 cameraUp = normalize(cross(forward, right));
            const float aspect = vw / std::max(1.0f, vh);
            const float focal = 1.0f / tanf(DirectX::XMConvertToRadians(60.0f) * 0.5f);
            const ImVec2 mouse = io.MousePos;
            float nearestDepth = std::numeric_limits<float>::max();
            int pickedMesh = -1;
            auto projectPickVertex = [&](const NifVertex& vertex, ImVec2& screen, float& depth) {
                const PickVec3 relative{vertex.pos.x-eye.x, vertex.pos.y-eye.y, vertex.pos.z-eye.z};
                depth = dot(relative, forward);
                if (depth <= 0.01f) return false;
                const float nx = dot(relative, right) * focal / (depth * aspect);
                const float ny = dot(relative, cameraUp) * focal / depth;
                if (!std::isfinite(nx) || !std::isfinite(ny) || fabsf(nx) > 4.0f || fabsf(ny) > 4.0f) return false;
                screen = ImVec2(imageMin.x + (nx * 0.5f + 0.5f) * vw,
                                imageMin.y + (0.5f - ny * 0.5f) * vh);
                return true;
            };
            for (std::size_t meshIndex = 0; meshIndex < currentNif->meshes.size(); ++meshIndex) {
                const auto& mesh = currentNif->meshes[meshIndex];
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    const auto ia = mesh.indices[i], ib = mesh.indices[i+1], ic = mesh.indices[i+2];
                    if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) continue;
                    NifVertex va = mesh.vertices[ia], vb = mesh.vertices[ib], vc = mesh.vertices[ic];
                    va.pos = TransformPreviewPosition(va.pos, mesh.previewTransform);
                    vb.pos = TransformPreviewPosition(vb.pos, mesh.previewTransform);
                    vc.pos = TransformPreviewPosition(vc.pos, mesh.previewTransform);
                    ImVec2 a{}, b{}, c{}; float da=0.0f, db=0.0f, dc=0.0f;
                    if (!projectPickVertex(va, a, da) || !projectPickVertex(vb, b, db) || !projectPickVertex(vc, c, dc)) continue;
                    const float denominator = (b.y-c.y)*(a.x-c.x) + (c.x-b.x)*(a.y-c.y);
                    if (fabsf(denominator) < 1.0e-6f) continue;
                    const float wa = ((b.y-c.y)*(mouse.x-c.x) + (c.x-b.x)*(mouse.y-c.y)) / denominator;
                    const float wb = ((c.y-a.y)*(mouse.x-c.x) + (a.x-c.x)*(mouse.y-c.y)) / denominator;
                    const float wc = 1.0f - wa - wb;
                    if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
                    const float inverseDepth = wa/da + wb/db + wc/dc;
                    if (inverseDepth <= 0.0f) continue;
                    const float depth = 1.0f / inverseDepth;
                    if (depth < nearestDepth) {
                        nearestDepth = depth;
                        pickedMesh = static_cast<int>(meshIndex);
                    }
                }
            }
            if (pickedMesh >= 0) {
                selectedMesh = pickedMesh;
                if (renderer) renderer->SetSelectedMesh(selectedMesh);
            }
        }
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
            camYaw   += io.MouseDelta.x * 0.008f;
            camPitch = std::clamp(camPitch - io.MouseDelta.y*0.008f, -1.50f, 1.50f);
        }
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)){
            // simple pan in view plane
            float panSpeed = camDist * 0.0012f;
            camTarget.x -= io.MouseDelta.x * panSpeed * cosf(camYaw);
            camTarget.y += io.MouseDelta.x * panSpeed * sinf(camYaw);
            camTarget.z += io.MouseDelta.y * panSpeed;
        }
    }
    // Overlay stats on viewport
    ImVec2 p0 = imageMin;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char overlay[128];
    snprintf(overlay,128,"Yaw %.1f  Pitch %.1f  Dist %.0f", camYaw, camPitch, camDist);
    dl->AddText(ImVec2(p0.x+8, p0.y+8), IM_COL32(220,220,220,255), overlay);
    if(currentNif && previewStats.meshes == 0){
        const char* msg = cpuPreviewDrawn ? "CPU geometry preview (GPU renderer unavailable)." :
            (currentNif->totalVerts == 0 ? "No supported triangle geometry decoded from this NIF." : "Parsed mesh has no valid preview triangles; check SkyLab.log.");
        if (cpuPreviewDrawn) {
            dl->AddText(ImVec2(p0.x+8, p0.y+44), IM_COL32(230,190,130,255), msg);
        } else {
            ImVec2 sz=ImGui::CalcTextSize(msg);
            dl->AddText(ImVec2(p0.x+(vw-sz.x)*0.5f, p0.y+vh*0.5f), IM_COL32(230,190,130,255), msg);
        }
    }
    char statsText[128];
    snprintf(statsText, sizeof(statsText), "Parsed %u verts / %u tris | GPU %u meshes | CPU %s", currentNif ? currentNif->totalVerts : 0, currentNif ? currentNif->totalTris : 0, previewStats.meshes, cpuPreviewDrawn ? "on" : "off");
    dl->AddText(ImVec2(p0.x+8, p0.y+26), IM_COL32(190,195,205,255), statsText);
    if(!currentNif) {
        const char* msg="No NIF — Open one from File menu";
        ImVec2 sz=ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(p0.x+(vw-sz.x)*0.5f, p0.y+vh*0.5f), IM_COL32(180,180,180,255), msg);
    }
}

void UIManager::DrawTextureReplacePopup(){
    if(!showTexReplace) return;
    ImGui::OpenPopup("Replace Texture");
    if(ImGui::BeginPopupModal("Replace Texture", &showTexReplace, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Preview a DDS replacement, or export it over the mesh's original texture path and apply it to the crosshair object.");
        ImGui::Separator();
        ImGui::InputText("Source DDS", texReplaceOrig, sizeof(texReplaceOrig), ImGuiInputTextFlags_ReadOnly);
        ImGui::InputText("New DDS", texReplaceNew, sizeof(texReplaceNew));
        ImGui::InputText("Source NIF", texReplaceNifSource, sizeof(texReplaceNifSource), ImGuiInputTextFlags_ReadOnly);
        ImGui::TextWrapped("Copy and paste a Data-relative asset path. Both paths must start with textures\\ (for example, textures\\armor\\my.dds). Loose files, MO2/Vortex files, and loaded BSAs are supported.");
        ImGui::Separator();
        if(ImGui::Button("Apply Preview")){
            if(currentNif && selectedMesh>=0 && selectedMesh < static_cast<int>(currentNif->meshes.size())){
                auto &m = currentNif->meshes[selectedMesh];
                std::string orig = texReplaceOrig[0]? texReplaceOrig : m.diffuseTexture;
                std::string repl = texReplaceNew;
                if (!HasResourceFolderPrefix(orig, "textures") || !HasResourceFolderPrefix(repl, "textures")) {
                    Utils::ShowNotification("SkyLab: texture paths must start with textures\\.");
                } else if(!repl.empty()){
                    const auto replacementPath = Utils::ResolveTexturePath(repl);
                    if (!Utils::IsDDS(replacementPath) || !TextureManager::Get().Load(repl)) {
                        Utils::ShowNotification("SkyLab: could not load the replacement DDS.");
                    } else {
                        if (m.diffuseTexture.empty() && !orig.empty()) m.diffuseTexture = orig;
                        if(renderer) renderer->SetTextureOverride(orig, repl);
                        TextureManager::Get().SetOverride(orig, repl);
                        Utils::ShowNotification("SkyLab: preview override " + orig + " -> " + repl);
                    }
                }
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Apply to World Object")){
            std::string original = texReplaceOrig;
            if (original.empty() && currentNif && selectedMesh >= 0 && selectedMesh < static_cast<int>(currentNif->meshes.size()))
                original = currentNif->meshes[selectedMesh].diffuseTexture;
            const std::string replacementInput = texReplaceNew;
            auto ref = Utils::GetCrosshairRef();
            std::string sourceNif;
            sourceNif = texReplaceNifSource;
            if (sourceNif.empty()) {
                const auto refNif = Utils::GetCrosshairRefNifPath();
                if (!refNif.empty()) sourceNif = NormalizeAssetPath(refNif, "meshes");
                else if ((!ref || !ref->As<RE::Actor>()) && !targetNifPath.empty()) sourceNif = targetNifPath.string();
            }
            std::vector<uint8_t> textureBytes, nifBytes;
            const fs::path replacementPath = Utils::ResolveTexturePath(replacementInput);
            const fs::path originalTexture = NormalizeAssetPath(original, "textures");
            const fs::path originalNif = NormalizeAssetPath(sourceNif, "meshes");
            if (!HasResourceFolderPrefix(original, "textures") || !HasResourceFolderPrefix(replacementInput, "textures") ||
                !HasResourceFolderPrefix(sourceNif, "meshes")) {
                Utils::ShowNotification("SkyLab: texture paths must start with textures\\ and the NIF path must start with meshes\\.");
            } else if (!Utils::IsDDS(replacementPath) || !TextureManager::Get().Load(replacementInput) ||
                       !Utils::ReadResourceFile(replacementPath, textureBytes) || textureBytes.empty()) {
                Utils::ShowNotification("SkyLab: replacement DDS could not be read.");
            } else if (!Utils::ReadResourceFile(originalNif, nifBytes) || nifBytes.empty()) {
                Utils::ShowNotification("SkyLab: source NIF could not be read from Data/BSA.");
            } else {
                fs::path outputRoot = ExportRoot();
                fs::path writtenTexture, writtenNif;
                const bool savedTexture = Utils::WriteExportFile(outputRoot, originalTexture, textureBytes, &writtenTexture);
                const bool savedNif = savedTexture && Utils::WriteExportFile(outputRoot, originalNif, nifBytes, &writtenNif);
                if (!savedTexture || !savedNif) {
                    Utils::ShowNotification("SkyLab: export failed. Check Export Settings and file permissions.");
                } else {
                    if (renderer) renderer->SetTextureOverride(original, originalTexture.string());
                    TextureManager::Get().SetOverride(original, originalTexture.string());
                    bool liveApplied = ref && TextureManager::Get().ReplaceTextureOnRef(ref, original, originalTexture.string());
                    if (liveApplied) {
                        Utils::ShowNotification("SkyLab: exported NIF and DDS; texture applied to the live object.");
                    } else {
                        Utils::ShowNotification("SkyLab: exported NIF and DDS. The selected live mesh did not expose a matching texture slot.");
                    }
                    logger::info("World texture export complete: {} and {}", writtenNif.string(), writtenTexture.string());
                }
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Close")) showTexReplace=false;
        ImGui::EndPopup();
    }
}

void UIManager::DrawNifReplacePopup()
{
    if (!showNifReplace) return;
    if (nifReplaceSource[0] == '\0') {
        const auto source = !targetNifPath.empty() ? targetNifPath.string() :
            (!exportNifPaths.empty() ? exportNifPaths.front().string() : std::string{});
        strncpy_s(nifReplaceSource, source.c_str(), _TRUNCATE);
    }
    ImGui::OpenPopup("Replace NIF");
    if (ImGui::BeginPopupModal("Replace NIF", &showNifReplace, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("The source path is filled from the selected object. Paste the replacement NIF path below; paths must start with meshes\\. Loose files, MO2/Vortex files, and loaded BSAs are supported.");
        ImGui::InputText("Source NIF", nifReplaceSource, sizeof(nifReplaceSource), ImGuiInputTextFlags_ReadOnly);
        ImGui::InputTextWithHint("Replacement NIF", "meshes\\armor\\replacement.nif", nifReplaceDestination, sizeof(nifReplaceDestination));
        if (ImGui::Button("Replace and Preview")) {
            std::string source = nifReplaceSource;
            std::string replacementResource = nifReplaceDestination;
            if (!HasResourceFolderPrefix(source, "meshes") || !HasResourceFolderPrefix(replacementResource, "meshes")) {
                Utils::ShowNotification("SkyLab: source and replacement NIF paths must start with meshes\\.");
            } else {
                const fs::path replacementPath = Utils::ResolveNifPath(replacementResource);
                std::vector<uint8_t> bytes;
                NifFile replacement;
                if (!Utils::ReadResourceFile(replacementPath, bytes) || bytes.empty()) {
                    Utils::ShowNotification("SkyLab: replacement NIF could not be read.");
                } else {
                    replacement = NifParser::ParseFromMemory(bytes.data(), bytes.size(), replacementResource);
                    if (!replacement.isValid || replacement.totalVerts == 0) {
                        // Parsing serialized geometry is incomplete for some
                        // Skyrim BSTriShape variants. Ask Skyrim's resource
                        // loader to construct the replacement and capture its
                        // live geometry before deciding whether preview works.
                        std::string resourceName = replacementResource;
                        std::replace(resourceName.begin(), resourceName.end(), '/', '\\');
                        RE::NiPointer<RE::NiNode> replacementNode;
                        RE::BSModelDB::DBTraits::ArgsType args{};
                        const auto demandResult = RE::BSModelDB::Demand(resourceName.c_str(), replacementNode, args);
                        if (demandResult == RE::BSResource::ErrorCode::kNone && replacementNode) {
                            auto runtime = NifParser::CaptureScene(replacementNode.get(), resourceName);
                            if (runtime.isValid && runtime.totalVerts > 0) {
                                // Keep material paths extracted from the file
                                // parser; the live geometry snapshot provides
                                // Skyrim's decoded vertices for newer NIFs.
                                for (auto& runtimeMesh : runtime.meshes) {
                                    auto parsedMesh = std::find_if(replacement.meshes.begin(), replacement.meshes.end(), [&](const NifMesh& candidate) {
                                        return Utils::ToLower(candidate.name) == Utils::ToLower(runtimeMesh.name);
                                    });
                                    if (parsedMesh != replacement.meshes.end()) {
                                        runtimeMesh.diffuseTexture = parsedMesh->diffuseTexture;
                                        runtimeMesh.normalTexture = parsedMesh->normalTexture;
                                        runtimeMesh.glowTexture = parsedMesh->glowTexture;
                                        runtimeMesh.specularTexture = parsedMesh->specularTexture;
                                    }
                                }
                                runtime.textureFiles = replacement.textureFiles;
                                replacement = std::move(runtime);
                            }
                        }
                    }
                    if (!replacement.isValid || replacement.totalVerts == 0) {
                        Utils::ShowNotification("SkyLab: NIF export needs valid Skyrim geometry, but this replacement could not be previewed. The source was left unchanged.");
                    } else {
                        const fs::path relative = NormalizeAssetPath(source, "meshes");
                        fs::path written;
                        if (!Utils::WriteExportFile(ExportRoot(), relative, bytes, &written)) {
                            Utils::ShowNotification("SkyLab: NIF export failed. Check Export Settings.");
                        } else {
                            targetNifPath = relative;
                            exportNifPaths = { relative };
                            SetCurrentNif(std::move(replacement), written, false);
                            Utils::ShowNotification(currentNif && currentNif->totalVerts
                                ? "SkyLab: replaced and exported " + relative.string()
                                : "SkyLab: NIF exported, but its geometry is not supported by the preview.");
                            showNifReplace = false;
                        }
                    }
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to World Object")) {
            const std::string replacement = NormalizeAssetPath(nifReplaceDestination, "meshes");
            ApplyNifToWorldReference(replacement);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) showNifReplace = false;
        ImGui::EndPopup();
    }
}

void UIManager::DrawExportSettingsPopup()
{
    if (!showExportSettings) return;
    ImGui::OpenPopup("Export Settings");
    if (ImGui::BeginPopupModal("Export Settings", &showExportSettings, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Vortex: leave the folder blank to export to Skyrim Data. MO2: enter the active instance's Overwrite folder to keep exports in Overwrite.");
        ImGui::SeparatorText("Source assets to export");
        if (exportNifPaths.empty()) {
            ImGui::TextDisabled("No source NIF path is available for the current preview.");
        } else {
            for (const auto& source : exportNifPaths) ImGui::TextWrapped("NIF: %s", source.string().c_str());
        }
        if (currentNif) {
            for (const auto& texture : currentNif->textureFiles)
                ImGui::TextWrapped("Texture: %s", NormalizeAssetPath(texture, "textures").c_str());
        }
        ImGui::Separator();
        ImGui::InputText("Export folder", exportRootInput, sizeof(exportRootInput));
        ImGui::Text("Current target: %s", ExportRoot().string().c_str());
        if (ImGui::Button("Save")) {
            Config::Get().exportRoot = exportRootInput;
            Config::Save();
            Utils::ShowNotification("SkyLab: export folder saved.");
            showExportSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Skyrim Data")) {
            Config::Get().exportRoot.clear();
            exportRootInput[0] = '\0';
            Config::Save();
            Utils::ShowNotification("SkyLab: exports will go to Skyrim Data.");
            showExportSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) showExportSettings = false;
        ImGui::EndPopup();
    }
}

void UIManager::DrawObjImportPopup()
{
    if (showObjImport) {
        ImGui::OpenPopup("Import OBJ as Skyrim SE NIF");
        showObjImport = false;
    }
    if (!ImGui::BeginPopupModal("Import OBJ as Skyrim SE NIF", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("Create a static Skyrim SE/AE BSTriShape NIF from OBJ geometry using Nifly. The imported mesh is unskinned; character and armor meshes need bone weights before they can deform on actors.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(500.0f);
    ImGui::InputTextWithHint("OBJ file", "C:\\path\\to\\mesh.obj", objImportSource, sizeof(objImportSource));
    ImGui::SetNextItemWidth(500.0f);
    ImGui::InputTextWithHint("Output NIF", "C:\\path\\to\\Data\\meshes\\custom\\mesh.nif", objImportOutput, sizeof(objImportOutput));
    ImGui::SetNextItemWidth(500.0f);
    ImGui::InputTextWithHint("Diffuse texture (optional)", "textures\\custom\\mesh.dds", objImportDiffuse, sizeof(objImportDiffuse));
    ImGui::TextDisabled("Enter a Skyrim-relative texture path if you want the generated NIF to reference a DDS.");
    if (ImGui::Button("Import and Export NIF") && objImportSource[0] && objImportOutput[0]) {
        std::string error;
        if (ObjNifImporter::ImportAsSkyrimSENif(fs::path(objImportSource), fs::path(objImportOutput), objImportDiffuse, error)) {
            Utils::ShowNotification("SkyLab: Skyrim SE NIF exported from OBJ.");
            OpenNif(fs::path(objImportOutput));
            ImGui::CloseCurrentPopup();
        } else {
            errorMsg = "OBJ import failed: " + error;
            Utils::ShowNotification(errorMsg);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void UIManager::DrawOpenNifPopup(){
    if (showOpenNif) {
        ImGui::OpenPopup("Open NIF Resource");
        showOpenNif = false;
    }
    if(ImGui::BeginPopupModal("Open NIF Resource", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Copy and paste a Data-relative NIF path beginning with meshes\\ (for example, meshes\\armor\\example.nif). Loose files, MO2/Vortex files, and loaded BSAs are supported.");
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextWithHint("##nifResource", "meshes\\armor\\example.nif", nifResourcePath, sizeof(nifResourcePath));
        if(ImGui::Button("Open") && nifResourcePath[0]){
            if (!HasResourceFolderPrefix(nifResourcePath, "meshes")) {
                Utils::ShowNotification("SkyLab: NIF paths must start with meshes\\.");
            } else {
                OpenNif(Utils::ResolveNifPath(nifResourcePath));
                if(errorMsg.empty()) ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void UIManager::DrawLooseNifPopup()
{
    if (showLooseNifList) {
        ImGui::OpenPopup("Loose NIF files");
        showLooseNifList = false;
    }
    if (ImGui::BeginPopupModal("Loose NIF files", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Loose NIF files found in Skyrim Data\\meshes:");
        ImGui::BeginChild("##looseNifList", ImVec2(520.0f, 360.0f), true);
        if (recentFiles.empty()) {
            ImGui::TextDisabled("No loose NIF files found.");
        }
        for (const auto& file : recentFiles) {
            if (ImGui::Selectable(file.c_str())) {
                OpenNif(fs::path(file));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void UIManager::DrawAssetSearchPanel()
{
    if (!ImGui::Begin("Mesh / Texture Search")) { ImGui::End(); return; }
    ImGui::TextWrapped("Search by asset name or linked in-game object name. Loose files and BSA contents are included.");
    if (ImGui::RadioButton("Meshes", &assetSearchKind, 0)) { assetCatalogBuilt = false; assetCatalog.clear(); }
    ImGui::SameLine();
    if (ImGui::RadioButton("Textures", &assetSearchKind, 1)) { assetCatalogBuilt = false; assetCatalog.clear(); }
    ImGui::SetNextItemWidth(-1.0f);
    const bool queryChanged = ImGui::InputTextWithHint("##assetSearch", "Search NIF / DDS or in-game name", assetSearchBuf, sizeof(assetSearchBuf));
    const auto query = Utils::ToLower(assetSearchBuf);
    if (query.size() < 4) {
        ImGui::TextDisabled("Type at least 4 characters to search. Results are limited to 50.");
        ImGui::End();
        return;
    }
    if (!assetCatalogBuilt) {
        const auto dataRoot = fs::path(Utils::GetDataPath());
        const auto extension = assetSearchKind == 0 ? ".nif" : ".dds";
        const auto folder = assetSearchKind == 0 ? "meshes" : "textures";
        std::unordered_set<std::string> indexedPaths;
        std::unordered_set<std::string> indexedPathForms;
        std::unordered_map<std::string, std::size_t> firstEntryByPath;
        auto addAssetPath = [&](std::string path) {
            path = NormalizeAssetPath(path, folder);
            if (indexedPaths.insert(Utils::ToLower(path)).second) assetCatalog.push_back({std::move(path), {}, {}});
        };
        for (const auto& path : Utils::ListFilesRecursive(dataRoot / folder, extension)) addAssetPath(path);
        auto archived = Utils::ListBsaResources(dataRoot, extension);
        for (auto& path : archived) addAssetPath(std::move(path));
        for (std::size_t i = 0; i < assetCatalog.size(); ++i) firstEntryByPath.try_emplace(Utils::ToLower(assetCatalog[i].path), i);
        if (assetSearchKind == 0) {
            auto* handler = RE::TESDataHandler::GetSingleton();
            auto addRecord = [&](RE::TESForm* form, const char* model) {
                if (!form || !model || !*model) return;
                const auto path = NormalizeAssetPath(model, "meshes");
                char id[16]{}; std::snprintf(id, sizeof(id), "%08X", form->GetFormID());
                const auto gameName = form->GetName() ? form->GetName() : "";
                const auto pathKey = Utils::ToLower(path);
                if (!indexedPathForms.insert(pathKey + "|" + id).second) return;
                auto [found, inserted] = firstEntryByPath.try_emplace(pathKey, assetCatalog.size());
                if (inserted) assetCatalog.push_back({path, gameName, id});
                else {
                    auto& existing = assetCatalog[found->second];
                    if (existing.gameName.empty()) { existing.gameName = gameName; existing.formID = id; }
                    else assetCatalog.push_back({path, gameName, id});
                }
            };
            if (handler) {
                for (auto* form : handler->GetFormArray<RE::TESObjectWEAP>()) addRecord(form, form ? form->GetModel() : nullptr);
                for (auto* form : handler->GetFormArray<RE::TESObjectSTAT>()) addRecord(form, form ? form->GetModel() : nullptr);
                for (auto* form : handler->GetFormArray<RE::TESObjectCONT>()) addRecord(form, form ? form->GetModel() : nullptr);
                for (auto* form : handler->GetFormArray<RE::TESObjectDOOR>()) addRecord(form, form ? form->GetModel() : nullptr);
                for (auto* form : handler->GetFormArray<RE::TESFurniture>()) addRecord(form, form ? form->GetModel() : nullptr);
                for (auto* form : handler->GetFormArray<RE::TESObjectARMA>()) if (form) {
                    addRecord(form, form->bipedModels[RE::SEXES::kMale].GetModel());
                    addRecord(form, form->bipedModels[RE::SEXES::kFemale].GetModel());
                }
                for (auto* form : handler->GetFormArray<RE::BGSHeadPart>()) addRecord(form, form ? form->GetModel() : nullptr);
                for (auto* form : handler->GetFormArray<RE::TESObjectARMO>()) if (form) {
                    addRecord(form, form->worldModels[0].GetModel());
                    addRecord(form, form->worldModels[1].GetModel());
                }
            }
        }
        std::sort(assetCatalog.begin(), assetCatalog.end(), [](const AssetSearchEntry& a, const AssetSearchEntry& b) { return a.path < b.path; });
        assetCatalogBuilt = true;
    }
    if (assetSearchKind == 1 && (queryChanged || semanticTextureQuery != query)) {
        semanticTextureQuery = query;
        semanticTextureResults.clear();
        std::unordered_set<std::string> parsedModels;
        std::unordered_set<std::string> indexedTextures;
        auto* handler = RE::TESDataHandler::GetSingleton();
        auto addSemanticTextures = [&](RE::TESForm* form, const char* model) {
            if (!form || !model || !*model || parsedModels.size() >= 16) return;
            const auto name = form->GetName() ? form->GetName() : "";
            const auto editor = form->GetFormEditorID() ? form->GetFormEditorID() : "";
            char id[16]{}; std::snprintf(id, sizeof(id), "%08X", form->GetFormID());
            if (Utils::ToLower(std::string(name) + " " + editor + " " + id).find(query) == std::string::npos) return;
            const auto modelKey = Utils::ToLower(NormalizeAssetPath(model, "meshes"));
            if (!parsedModels.insert(modelKey).second) return;
            const auto parsed = NifParser::Parse(Utils::ResolveNifPath(model));
            for (const auto& mesh : parsed.meshes) for (const auto* texture : {&mesh.diffuseTexture, &mesh.normalTexture, &mesh.glowTexture, &mesh.specularTexture}) {
                if (texture->empty()) continue;
                const auto normalized = NormalizeAssetPath(*texture, "textures");
                if (indexedTextures.insert(Utils::ToLower(normalized)).second) semanticTextureResults.push_back({normalized, name, id});
            }
        };
        if (handler) {
            for (auto* form : handler->GetFormArray<RE::TESObjectWEAP>()) addSemanticTextures(form, form ? form->GetModel() : nullptr);
            for (auto* form : handler->GetFormArray<RE::TESObjectSTAT>()) addSemanticTextures(form, form ? form->GetModel() : nullptr);
            for (auto* form : handler->GetFormArray<RE::TESObjectARMO>()) if (form) {
                addSemanticTextures(form, form->worldModels[0].GetModel());
                addSemanticTextures(form, form->worldModels[1].GetModel());
            }
        }
    }
    const auto resultKey = std::to_string(assetSearchKind) + ":" + query;
    if (assetSearchResultKey != resultKey) {
        assetSearchResultKey = resultKey;
        assetSearchResults.clear();
        auto collectMatches = [&](const std::vector<AssetSearchEntry>& entries) {
            for (const auto& entry : entries) {
                if (Utils::ToLower(entry.path + " " + entry.gameName + " " + entry.formID).find(query) == std::string::npos) continue;
                assetSearchResults.push_back(entry);
                if (assetSearchResults.size() >= 50) return;
            }
        };
        collectMatches(assetCatalog);
        if (assetSearchResults.size() < 50) collectMatches(semanticTextureResults);
    }
    ImGui::BeginChild("##assetResults", ImVec2(0.0f, 0.0f), true);
    for (const auto& entry : assetSearchResults) {
        const auto label = entry.gameName.empty() ? entry.path : std::format("{} [{}] {}", entry.gameName, entry.formID, entry.path);
        if (ImGui::Selectable(label.c_str())) {
            if (assetSearchKind == 0) OpenNif(Utils::ResolveNifPath(entry.path));
            else if (currentNif && selectedMesh >= 0 && selectedMesh < static_cast<int>(currentNif->meshes.size())) {
                auto& mesh = currentNif->meshes[selectedMesh];
                if (TextureManager::Get().Load(entry.path)) {
                    if (mesh.diffuseTexture.empty()) mesh.diffuseTexture = entry.path;
                    if (renderer) renderer->SetTextureOverride(mesh.diffuseTexture, entry.path);
                    TextureManager::Get().SetOverride(mesh.diffuseTexture, entry.path);
                } else Utils::ShowNotification("SkyLab: DDS could not be loaded.");
            }
        }
    }
    if (assetSearchResults.empty()) ImGui::TextDisabled("No matching assets found.");
    ImGui::EndChild();
    ImGui::End();
}

void UIManager::DrawNpcSearchPanel()
{
    if (!ImGui::Begin("NPC Search")) { ImGui::End(); return; }
    ImGui::TextWrapped("Search NPC records from Skyrim and installed mods by name, editor ID, FormID, or FaceGen NIF ID.");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##npcSearch", "NPC name, editor ID, FormID, or FaceGen ID", npcSearchBuf, sizeof(npcSearchBuf));
    const auto query = Utils::ToLower(npcSearchBuf);
    if (query.size() < 4) {
        ImGui::TextDisabled("Type at least 4 characters to search. Results are limited to 50 NPCs.");
        ImGui::End();
        return;
    }
    if (npcSearchResultQuery != query) {
        npcSearchResultQuery = query;
        npcSearchResults.clear();
        auto idQuery = query;
        while (!idQuery.empty() && std::isspace(static_cast<unsigned char>(idQuery.front()))) idQuery.erase(idQuery.begin());
        while (!idQuery.empty() && std::isspace(static_cast<unsigned char>(idQuery.back()))) idQuery.pop_back();
        if (idQuery.ends_with(".nif")) idQuery.resize(idQuery.size() - 4);
        if (idQuery.starts_with("0x")) idQuery.erase(0, 2);
        if (auto* handler = RE::TESDataHandler::GetSingleton()) {
            for (auto* npc : handler->GetFormArray<RE::TESNPC>()) {
                if (!npc) continue;
                const auto name = npc->GetName() ? npc->GetName() : "";
                const auto editor = npc->GetFormEditorID() ? npc->GetFormEditorID() : "";
                char formId[16]{};
                char localId[16]{};
                char lowId[16]{};
                const auto fullFormId = npc->GetFormID();
                std::snprintf(formId, sizeof(formId), "%08X", fullFormId);
                std::snprintf(localId, sizeof(localId), "%08X", npc->GetLocalFormID());
                std::snprintf(lowId, sizeof(lowId), "%08X", fullFormId & 0x00FFFFFF);
                const auto searchableText = Utils::ToLower(std::string(name) + " " + editor + " " + formId + " 0x" + formId);
                const auto searchableIds = Utils::ToLower(std::string(formId) + " " + localId + " " + lowId);
                if (searchableText.find(query) == std::string::npos &&
                    (idQuery.size() < 4 || searchableIds.find(idQuery) == std::string::npos)) continue;
                npcSearchResults.push_back(npc);
                if (npcSearchResults.size() >= 50) break;
            }
        }
    }
    ImGui::BeginChild("##npcResults", ImVec2(0.0f, 0.0f), true);
    for (auto* npc : npcSearchResults) {
        const auto name = npc->GetName() ? npc->GetName() : "";
        const auto editor = npc->GetFormEditorID() ? npc->GetFormEditorID() : "";
        char id[16]{}; std::snprintf(id, sizeof(id), "%08X", npc->GetFormID());
        const auto label = std::format("{} [0x{}] {}", name[0] ? name : editor, id, editor);
        if (ImGui::Selectable(label.c_str())) OpenNpcPreview(npc);
    }
    if (npcSearchResults.empty()) ImGui::TextDisabled("No matching NPCs.");
    ImGui::EndChild();
    ImGui::End();
}

bool UIManager::OpenNpcPreview(RE::TESNPC* npc, RE::Actor* liveActor)
{
    if (!npc) return false;
    if (auto* actor = liveActor ? liveActor : npc->GetUniqueActor()) {
        std::vector<RE::NiAVObject*> roots;
        std::vector<std::pair<std::string, RE::BSTextureSet*>> materials;
        std::vector<std::string> bipedModels;
        std::unordered_set<std::string> bipedModelSet;
        auto addBipedModel = [&](const char* model) {
            if (!model || !*model) return;
            const auto key = Utils::ToLower(NormalizeAssetPath(model, "meshes"));
            if (bipedModelSet.insert(key).second) bipedModels.emplace_back(model);
        };
        const auto faceGeometryPaths = NpcFaceGeometryPaths(npc);
        auto* actorRoot = actor->Get3D();
        if (actorRoot) roots.push_back(actorRoot);
        auto* headAnchor = actorRoot
            ? actorRoot->GetObjectByName(RE::BSFixedString("NPC Head [Head]"))
            : nullptr;
        if (!headAnchor) {
            headAnchor = actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
        }
        // Head-part objects can be held outside the actor root on some actor
        // states. Capture each one, de-duplicating by CaptureScene's visited set.
        for (std::uint32_t type = 0; type < static_cast<std::uint32_t>(RE::BGSHeadPart::HeadPartType::kTotal); ++type) {
            if (auto* head = actor->GetHeadPartObject(static_cast<RE::BGSHeadPart::HeadPartType>(type))) {
                roots.push_back(head);
            }
        }
        if (const auto& biped = actor->GetBiped(false); biped) {
            if (!actorRoot && biped->root) roots.insert(roots.begin(), biped->root);
            for (auto& part : biped->objects) {
                // The actor root already contains these clones and their world
                // transforms. Use detached clones only when there is no root.
                if (!actorRoot && part.partClone) roots.push_back(part.partClone.get());
                const char* model = part.part ? part.part->GetModel() : nullptr;
                if (model && *model) {
                    addBipedModel(model);
                    materials.emplace_back(model, part.skinTexture);
                }
            }
        }
        auto live = NifParser::CaptureScene(roots, npc->GetName() ? npc->GetName() : "NPC");
        if (live.isValid) {
            for (const auto& model : bipedModels) {
                auto fallback = NifParser::Parse(Utils::ResolveNifPath(model));
                if (!fallback.isValid || fallback.totalVerts == 0)
                    fallback = NifParser::ParseGameResource(Utils::ResolveNifPath(model));
                if (!fallback.isValid) continue;
                const auto normalizedModelPath = NormalizeAssetPath(model, "meshes");
                for (auto& liveMesh : live.meshes) {
                    if (!liveMesh.sourcePath.empty()) continue;
                    const auto liveName = Utils::ToLower(liveMesh.name);
                    if (std::any_of(fallback.meshes.begin(), fallback.meshes.end(), [&](const NifMesh& candidate) {
                            return Utils::ToLower(candidate.name) == liveName;
                        })) liveMesh.sourcePath = normalizedModelPath;
                }
                for (auto& fallbackMesh : fallback.meshes) fallbackMesh.sourcePath = normalizedModelPath;
                std::unordered_set<std::string> captured;
                for (const auto& mesh : live.meshes) if (!mesh.vertices.empty() && !mesh.indices.empty()) captured.insert(Utils::ToLower(mesh.name));
                AppendFallbackMeshes(live, std::move(fallback), captured);
            }

            // FaceGen geometry is stored in head-local space. Attach it to the
            // live actor's head bone before adding it to the preview; appending
            // its untransformed file vertices puts a second face at world origin.
            RE::NiAVObject* captureRoot = actorRoot ? actorRoot : headAnchor;
            if (!captureRoot && !roots.empty()) captureRoot = roots.front();
            if (captureRoot && headAnchor) {
                const RE::NiTransform faceToPreview = captureRoot->world.Invert() * headAnchor->world;
                for (const auto& path : faceGeometryPaths) {
                    auto face = NifParser::Parse(Utils::ResolveNifPath(path));
                    if (!face.isValid || face.totalVerts == 0)
                        face = NifParser::ParseGameResource(Utils::ResolveNifPath(path));
                    if (!face.isValid || face.meshes.empty()) continue;

                    const auto normalizedPath = NormalizeAssetPath(path, "meshes");
                    SetFaceTint(face, NpcFaceTintPathFromGeometry(path));
                    for (auto& mesh : face.meshes) {
                        mesh.sourcePath = normalizedPath;
                        mesh.previewTransform = {};
                        for (auto& vertex : mesh.vertices) {
                            const auto position = faceToPreview * RE::NiPoint3(vertex.pos.x, vertex.pos.y, vertex.pos.z);
                            vertex.pos = {position.x, position.y, position.z};
                            auto normal = faceToPreview.rotate * RE::NiPoint3(vertex.normal.x, vertex.normal.y, vertex.normal.z);
                            const float length = normal.Length();
                            if (length > 0.0001f) normal = normal / length;
                            vertex.normal = {normal.x, normal.y, normal.z};
                        }
                    }

                    NifFile unmatchedFaceMeshes;
                    unmatchedFaceMeshes.isValid = true;
                    unmatchedFaceMeshes.nodes.push_back(NifNode{});
                    unmatchedFaceMeshes.nodes.front().name = "NPC FaceGen";
                    for (auto& faceMesh : face.meshes) {
                        auto liveMesh = std::find_if(live.meshes.begin(), live.meshes.end(), [&](const NifMesh& candidate) {
                            return Utils::ToLower(candidate.name) == Utils::ToLower(faceMesh.name);
                        });
                        if (liveMesh == live.meshes.end()) {
                            unmatchedFaceMeshes.meshes.push_back(std::move(faceMesh));
                            continue;
                        }
                        faceMesh.blockIndex = liveMesh->blockIndex;
                        faceMesh.name = liveMesh->name;
                        *liveMesh = std::move(faceMesh);
                    }
                    for (const auto& texture : face.textureFiles) {
                        if (std::find(live.textureFiles.begin(), live.textureFiles.end(), texture) == live.textureFiles.end())
                            live.textureFiles.push_back(texture);
                    }
                    if (!unmatchedFaceMeshes.meshes.empty())
                        AppendFallbackMeshes(live, std::move(unmatchedFaceMeshes));
                    SKSE::log::info("SkyLab attached NPC FaceGen '{}': {} mesh(es) at the actor head", path, face.meshes.size());
                    break;
                }
            } else if (!faceGeometryPaths.empty()) {
                SKSE::log::warn("SkyLab could not attach NPC FaceGen: actor head bone was not available");
            }

            EnrichSceneTextures(live, materials);
            RecalculateSceneBounds(live);
        }
        if (live.isValid && !live.meshes.empty()) {
            errorMsg.clear();
            exportNifPaths.clear();
            targetNifPath.clear();
            SetCurrentNif(std::move(live), {}, false);
            return true;
        }
    }

    NifFile assembled;
    assembled.isValid = true;
    assembled.nodes.push_back(NifNode{});
    assembled.nodes.front().name = npc->GetName() ? npc->GetName() : "NPC";
    std::vector<std::string> paths;
    auto addPath = [&](const char* path) {
        if (!path || !*path) return;
        if (std::none_of(paths.begin(), paths.end(), [&](const std::string& existing) { return Utils::ToLower(existing) == Utils::ToLower(path); })) paths.emplace_back(path);
    };
    const auto sex = npc->GetSex() == RE::SEXES::kFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;
    const auto* bodyPrefix = sex == RE::SEXES::kFemale ? "female" : "male";
    for (const auto* part : {"body_0", "body_1", "hands_0", "hands_1", "feet_0", "feet_1"}) {
        const auto modelPath = std::format("meshes\\actors\\character\\character assets\\{}{}.nif", bodyPrefix, part);
        addPath(modelPath.c_str());
    }
    std::function<void(RE::BGSHeadPart*)> addHead = [&](RE::BGSHeadPart* part) {
        if (!part) return;
        addPath(part->GetModel());
        for (auto* extra : part->extraParts) addHead(extra);
    };
    for (int i = 0; npc->headParts && i < npc->numHeadParts; ++i) addHead(npc->headParts[i]);
    if (auto* race = npc->GetRace()) addPath(race->bodyTextureModels[sex].GetModel());
    auto addArmor = [&](RE::TESObjectARMO* armor) {
        if (!armor) return;
        for (auto* addon : armor->armorAddons) {
            if (addon && (!npc->GetRace() || addon->IsValidRace(npc->GetRace()))) addPath(addon->bipedModels[sex].GetModel());
        }
    };
    addArmor(npc->farSkin);
    if (npc->defaultOutfit) npc->defaultOutfit->ForEachItem([&](RE::TESForm* form) {
        if (auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr) addArmor(armor);
        return RE::BSContainer::ForEachResult::kContinue;
    });
    for (const auto& path : paths) {
        auto part = NifParser::Parse(Utils::ResolveNifPath(path));
        if (!part.isValid || part.totalVerts == 0)
            part = NifParser::ParseGameResource(Utils::ResolveNifPath(path));
        if (part.isValid && part.totalVerts > 0) AppendFallbackMeshes(assembled, std::move(part));
    }
    if (assembled.meshes.empty()) {
        Utils::ShowNotification("SkyLab: NPC record has no previewable head, skin, or outfit NIFs. Load a live NPC with Open from Crosshair for its assembled appearance.");
        return false;
    }
    errorMsg.clear();
    exportNifPaths.clear();
    targetNifPath.clear();
    SetCurrentNif(std::move(assembled), {}, false);
    return true;
}

void UIManager::DrawAboutPopup(){
    if (showAbout) {
        ImGui::OpenPopup("About SkyLab");
        showAbout = false;
    }
    if (ImGui::BeginPopupModal("About SkyLab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("SkyLab is an in-game asset inspection and preview tool for Skyrim SE/AE modders. Inspect NIF scene meshes and materials, search loose files and loaded BSA archives for meshes and textures, preview NPC appearance parts, and try mesh or texture changes. Export NIFs and textures, or import an OBJ as a static Skyrim SE NIF. OBJ imports do not include Skyrim skinning or animation data.");
        ImGui::Spacing();
        ImGui::Text("Developed by Degdatar for SkyOS");
        ImGui::TextDisabled("Press F7 to open or close the editor.");
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void UIManager::DrawStatusBar(){
    ImGui::Separator();
    if(currentNif) ImGui::TextDisabled("%s | %zu meshes | %s", currentPath.filename().string().c_str(), currentNif->meshes.size(), NifParser::Summarize(*currentNif).c_str());
    else ImGui::TextDisabled("SkyLab — press F7 on an object, or File -> Open NIF. Right-click mesh -> Replace texture to live-preview.");
}

void UIManager::OnRender(ID3D11Device* device_, ID3D11DeviceContext* ctx_, ID3D11RenderTargetView* rtv){
    std::scoped_lock lock(stateMutex);
    if(!visible || !initialized) return;
    OnNewFrame();
    // Fullscreen dockspace host
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus|ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::Begin("SkyLab##host", nullptr, hostFlags);
    ImGui::PopStyleVar(3);
    ImGuiID dock = ImGui::GetID("SkyLabDock");

    // Build a real editor layout instead of docking all three windows into
    // the same tab stack. The ini file is disabled, so seed this each session.
    if(!ImGui::DockBuilderGetNode(dock)){
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, vp->Size);
        ImGuiID sceneNode=0, remainder=0, propertiesNode=0, viewportNode=0, searchStackNode=0, assetSearchNode=0, npcSearchNode=0;
        ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.22f, &sceneNode, &remainder);
        ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, 0.26f, &propertiesNode, &viewportNode);
        ImGui::DockBuilderSplitNode(propertiesNode, ImGuiDir_Down, 0.42f, &searchStackNode, &propertiesNode);
        ImGui::DockBuilderSplitNode(searchStackNode, ImGuiDir_Down, 0.50f, &npcSearchNode, &assetSearchNode);
        ImGui::DockBuilderDockWindow("Scene Tree", sceneNode);
        ImGui::DockBuilderDockWindow("Properties", propertiesNode);
        ImGui::DockBuilderDockWindow("Mesh / Texture Search", assetSearchNode);
        ImGui::DockBuilderDockWindow("NPC Search", npcSearchNode);
        ImGui::DockBuilderDockWindow("3D Mesh View", viewportNode);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpace(dock, ImVec2(0,0), ImGuiDockNodeFlags_None);
    DrawMenuBar();
    // File/Help menu popups must use the same host window ID stack where the
    // menu items called OpenPopup, otherwise ImGui silently drops them.
    DrawOpenNifPopup();
    DrawLooseNifPopup();
    DrawAboutPopup();

    // Top bar imitation of requested ASCII layout
    ImGui::TextColored(ImVec4(0.7f,0.85f,1.f,1.f), " SKYLAB — 3D Studio inside Skyrim  [F7 toggle | Right-click mesh -> Replace texture]");
    ImGui::Separator();

    ImGui::End(); // host

    // Scene Tree window
    ImGui::Begin("Scene Tree", nullptr, ImGuiWindowFlags_NoCollapse);
    DrawSceneTree();
    ImGui::End();

    // Properties window
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_NoCollapse);
    DrawProperties();
    ImGui::End();

    // These are independent dockable windows. They begin below Properties in
    // the right dock column and can be resized, collapsed, or dragged elsewhere.
    DrawAssetSearchPanel();
    DrawNpcSearchPanel();

    // Viewport window (central)
    ImGui::Begin("3D Mesh View", nullptr, ImGuiWindowFlags_NoCollapse);
    DrawViewport(device_, ctx_);
    DrawStatusBar();
    ImGui::End();

    // Popups
    DrawTextureReplacePopup();
    DrawNifReplacePopup();
    DrawExportSettingsPopup();
    DrawObjImportPopup();

    ImGui::Render();
    // Skyrim's swapchain RTV is passed in; we render ImGui on top of game's backbuffer
    // Bind game RTV then draw
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable){
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

}
