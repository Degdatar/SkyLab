#pragma once

#include "PCH.h"
#include "NifParser.h"

namespace SkyLab
{
    class PreviewRenderer
    {
    public:

        struct Stats
        {
            uint32_t verts{ 0 };
            uint32_t tris{ 0 };
            uint32_t meshes{ 0 };
        };


        PreviewRenderer();
        ~PreviewRenderer();


        bool Init(
            ID3D11Device* device);


        void Shutdown();


        void SetNif(
            const NifFile* nif);


        void SetSelectedMesh(
            int index);


        void SetOptions(
            bool wireframe,
            bool showCollision,
            bool showNormals,
            bool showUV);


        void UpdateCamera(
            float yaw,
            float pitch,
            float distance,
            DirectX::XMFLOAT3 target);


        void Render(
            ID3D11DeviceContext* context,
            ID3D11RenderTargetView* target,
            float width,
            float height);


        void ReloadShaders(
            ID3D11Device* device);


        void SetTextureOverride(
            const std::string& original,
            const std::string& replacement);


        void ClearTextureOverrides();


        Stats GetStats() const
        {
            return stats;
        }


    private:

        struct Vertex
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMFLOAT3 normal;
            DirectX::XMFLOAT2 uv;
            DirectX::XMFLOAT4 color;
        };


        struct MeshBuffer
        {
            ComPtr<ID3D11Buffer> vertexBuffer;
            ComPtr<ID3D11Buffer> indexBuffer;

            ComPtr<ID3D11ShaderResourceView>
                diffuse;

            uint32_t vertexCount{ 0 };
            uint32_t indexCount{ 0 };

            std::string texture;
        };


        struct FrameConstants
        {
            DirectX::XMFLOAT4X4 mvp;
            DirectX::XMFLOAT4X4 world;

            DirectX::XMFLOAT4 lightDirection;
            DirectX::XMFLOAT4 cameraPosition;

            DirectX::XMFLOAT4 options;
        };


        bool CreateShaders(
            ID3D11Device* device);


        bool CreateStates(
            ID3D11Device* device);


        bool CreateConstantBuffer(
            ID3D11Device* device);


        bool BuildMeshBuffers(
            ID3D11Device* device);


        bool CreateDepthBuffer(
            ID3D11Device* device,
            uint32_t width,
            uint32_t height);


        const NifFile* currentNif{ nullptr };

        std::vector<MeshBuffer>
            meshes;


        ComPtr<ID3D11VertexShader>
            vertexShader;

        ComPtr<ID3D11PixelShader>
            pixelShader;

        ComPtr<ID3D11InputLayout>
            inputLayout;


        ComPtr<ID3D11Buffer>
            constantBuffer;


        ComPtr<ID3D11SamplerState>
            sampler;

        ComPtr<ID3D11ShaderResourceView>
            fallbackDiffuse;


        ComPtr<ID3D11RasterizerState>
            solidRasterizer;

        ComPtr<ID3D11RasterizerState>
            wireRasterizer;


        ComPtr<ID3D11DepthStencilState>
            depthState;


        ComPtr<ID3D11Texture2D>
            depthTexture;

        ComPtr<ID3D11DepthStencilView>
            depthView;


        DirectX::XMFLOAT3
            cameraTarget{ 0.0f, 0.0f, 0.0f };


        float cameraYaw{ 0.6f };
        float cameraPitch{ 0.35f };
        float cameraDistance{ 100.0f };

        float fov{ 60.0f };


        bool wireframe{ false };
        bool showCollision{ true };
        bool showNormals{ false };
        bool showUV{ false };


        int selectedMesh{ -1 };


        uint32_t depthWidth{ 0 };
        uint32_t depthHeight{ 0 };


        Stats stats{};


        std::unordered_map<
            std::string,
            std::string>
            textureOverrides;


        bool initialized{ false };
        bool dirty{ false };
    };
}
