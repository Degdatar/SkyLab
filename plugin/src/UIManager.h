#pragma once
#include "NifParser.h"
#include "PreviewRenderer.h"
namespace SkyLab {

class UIManager {
public:
    static UIManager& Get();
    bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx, HWND hwnd);
    void Shutdown();
    void ToggleVisible(){ SetVisible(!visible.load()); }
    bool IsVisible() const { return visible.load(); }
    void SetVisible(bool v);
    void OnRender(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv);
    void OpenNif(const fs::path& path);
    void OpenNifFromCrosshair();
    // Call from WndProc hook to route ImGui input when visible
    bool WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnNewFrame();
private:
    void DrawMainWindow(float w, float h);
    void DrawMenuBar();
    void DrawSceneTree();
    void DrawProperties();
    void DrawViewport(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void DrawTextureReplacePopup();
    void DrawNifReplacePopup();
    void DrawExportSettingsPopup();
    void DrawObjImportPopup();
    void ExportCurrentPreview();
    void DrawOpenNifPopup();
    void DrawLooseNifPopup();
    void DrawAssetSearchPanel();
    void DrawNpcSearchPanel();
    bool OpenNpcPreview(RE::TESNPC* npc, RE::Actor* liveActor = nullptr);
    void DrawAboutPopup();
    void DrawStatusBar();
    void EnsureNifLoaded(const fs::path& p);
    void SetCurrentNif(NifFile&& nif, const fs::path& path, bool rememberPath);
    std::atomic_bool visible{false};
    bool initialized=false;
    mutable std::recursive_mutex stateMutex;
    HWND hwnd=nullptr;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* ctx=nullptr;
    std::unique_ptr<PreviewRenderer> renderer;
    std::unique_ptr<NifFile> currentNif;
    fs::path currentPath;
    std::string errorMsg;
    int selectedNode=-1, selectedMesh=-1;
    int hoveredMesh=-1;
    // camera
    float camYaw=0.6f, camPitch=0.35f, camDist=140.f;
    DirectX::XMFLOAT3 camTarget{0,0,0};
    bool isDragging=false, isPanning=false;
    ImVec2 lastMouse{0,0};
    bool gamePauseClaimed=false;
    ImVec2 virtualMouse{0,0};
    bool virtualMouseInitialized=false;
    bool rawMouseActive=false;
    ULONGLONG lastRawMouseTick=0;
    bool showAbout=false;
    bool showNifReplace=false;
    bool showExportSettings=false;
    bool showObjImport=false;
    bool showOpenNif=false;
    bool showLooseNifList=false;
    bool assetCatalogBuilt=false;
    std::array<bool, 5> polledMouseButtons{};
    // filters
    char searchBuf[128]="";
    char assetSearchBuf[256]="";
    char npcSearchBuf[256]="";
    std::string npcSearchResultQuery;
    std::vector<RE::TESNPC*> npcSearchResults;
    int assetSearchKind=0;
    struct AssetSearchEntry { std::string path, gameName, formID; };
    std::vector<AssetSearchEntry> assetCatalog;
    std::vector<AssetSearchEntry> assetSearchResults;
    std::string assetSearchResultKey;
    std::string semanticTextureQuery;
    std::vector<AssetSearchEntry> semanticTextureResults;
    bool optWireframe=false, optCollision=true, optNormals=false, optUV=false;
    // render target for viewport
    ComPtr<ID3D11Texture2D> viewportTex;
    ComPtr<ID3D11RenderTargetView> viewportRTV;
    ComPtr<ID3D11ShaderResourceView> viewportSRV;
    float viewportW=800, viewportH=500;
    // texture replace
    bool showTexReplace=false;
    char texReplaceOrig[260]="";
    char texReplaceNew[260]="";
    char texReplaceNifSource[512]="";
    char nifResourcePath[512]="";
    char nifReplaceSource[512]="";
    char nifReplaceDestination[512]="";
    char exportRootInput[512]="";
    char objImportSource[512]="";
    char objImportOutput[512]="";
    char objImportDiffuse[512]="";
    fs::path targetNifPath;
    std::vector<fs::path> exportNifPaths;
    std::string pendingOrig;
    // file browser
    std::vector<std::string> recentFiles;
    float gizmoSize=1.f;
};
}
