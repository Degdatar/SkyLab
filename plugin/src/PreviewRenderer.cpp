#include "PCH.h"
#include "PreviewRenderer.h"

#include "TextureManager.h"

namespace SkyLab
{
    namespace
    {
        constexpr char VERTEX_SHADER[] = R"(
cbuffer Frame : register(b0)
{
    matrix mvp;
    matrix world;
    float4 lightDirection;
    float4 cameraPosition;
    float4 options;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal   : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    float4 color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPosition =
        mul(float4(input.position, 1.0), world);

    output.position =
        mul(worldPosition, mvp);

    output.normal =
        normalize(
            mul(
                float4(input.normal, 0.0),
                world).xyz);

    output.uv =
        input.uv;

    output.color =
        input.color;

    return output;
}
)";


        constexpr char PIXEL_SHADER[] = R"(
cbuffer Frame : register(b0)
{
    matrix mvp;
    matrix world;
    float4 lightDirection;
    float4 cameraPosition;
    float4 options;
};

Texture2D diffuseTexture : register(t0);
SamplerState textureSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : TEXCOORD0;
    float2 uv       : TEXCOORD1;
    float4 color    : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    float3 normal =
        normalize(input.normal);

    float3 light =
        normalize(lightDirection.xyz);

    float lighting =
        saturate(
            abs(dot(normal, light)));

    float4 textureColor =
        diffuseTexture.Sample(
            textureSampler,
            input.uv);

    float3 base =
        textureColor.rgb *
        input.color.rgb;

    float3 result =
        base * (0.38 + lighting * 0.62);

    if (options.x > 0.5)
    {
        result += float3(
            0.025,
            0.045,
            0.065);
    }

    return float4(
        saturate(result),
        textureColor.a);
}
)";
    }


    PreviewRenderer::PreviewRenderer() = default;


    PreviewRenderer::~PreviewRenderer()
    {
        Shutdown();
    }


    bool PreviewRenderer::Init(
        ID3D11Device* device)
    {
        if (!device) {
            return false;
        }

        if (initialized) {
            return true;
        }


        if (!CreateShaders(device)) {
            logger::error("SkyLab preview: shader or input-layout initialization failed");
            return false;
        }


        if (!CreateStates(device)) {
            logger::error("SkyLab preview: sampler/rasterizer/depth-state initialization failed");
            return false;
        }


        if (!CreateConstantBuffer(device)) {
            logger::error("SkyLab preview: constant-buffer initialization failed");
            return false;
        }

        // Render undecoded/missing textures as a neutral white material so
        // mesh geometry remains visible while inspecting the model.
        const uint32_t whitePixel = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = 1;
        textureDesc.Height = 1;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA textureData{};
        textureData.pSysMem = &whitePixel;
        textureData.SysMemPitch = sizeof(whitePixel);
        ComPtr<ID3D11Texture2D> whiteTexture;
        const HRESULT whiteTextureResult = device->CreateTexture2D(&textureDesc, &textureData, &whiteTexture);
        if (FAILED(whiteTextureResult)) {
            logger::error("SkyLab preview: fallback texture creation failed: 0x{:08X}", static_cast<uint32_t>(whiteTextureResult));
            return false;
        }
        const HRESULT whiteSrvResult = device->CreateShaderResourceView(whiteTexture.Get(), nullptr, &fallbackDiffuse);
        if (FAILED(whiteSrvResult)) {
            logger::error("SkyLab preview: fallback texture view creation failed: 0x{:08X}", static_cast<uint32_t>(whiteSrvResult));
            return false;
        }


        initialized = true;

        return true;
    }


    void PreviewRenderer::Shutdown()
    {
        meshes.clear();

        depthView.Reset();
        depthTexture.Reset();

        constantBuffer.Reset();

        sampler.Reset();
        fallbackDiffuse.Reset();

        solidRasterizer.Reset();
        wireRasterizer.Reset();

        depthState.Reset();

        inputLayout.Reset();

        vertexShader.Reset();
        pixelShader.Reset();

        currentNif = nullptr;

        initialized = false;
    }


    bool PreviewRenderer::CreateShaders(
        ID3D11Device* device)
    {
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        ComPtr<ID3DBlob> errors;


        HRESULT hr =
            D3DCompile(
                VERTEX_SHADER,
                sizeof(VERTEX_SHADER),
                "SkyLabVS",
                nullptr,
                nullptr,
                "main",
                "vs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS,
                0,
                &vsBlob,
                &errors);


        if (FAILED(hr))
        {
            if (errors)
            {
                logger::error(
                    "SkyLab vertex shader: {}",
                    static_cast<const char*>(
                        errors->GetBufferPointer()));
            }

            return false;
        }


        hr =
            device->CreateVertexShader(
                vsBlob->GetBufferPointer(),
                vsBlob->GetBufferSize(),
                nullptr,
                &vertexShader);


        if (FAILED(hr)) {
            return false;
        }


        errors.Reset();


        hr =
            D3DCompile(
                PIXEL_SHADER,
                sizeof(PIXEL_SHADER),
                "SkyLabPS",
                nullptr,
                nullptr,
                "main",
                "ps_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS,
                0,
                &psBlob,
                &errors);


        if (FAILED(hr))
        {
            if (errors)
            {
                logger::error(
                    "SkyLab pixel shader: {}",
                    static_cast<const char*>(
                        errors->GetBufferPointer()));
            }

            return false;
        }


        hr =
            device->CreatePixelShader(
                psBlob->GetBufferPointer(),
                psBlob->GetBufferSize(),
                nullptr,
                &pixelShader);


        if (FAILED(hr)) {
            return false;
        }


        const D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            {
                "POSITION",
                0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                0,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            },

            {
                "NORMAL",
                0,
                DXGI_FORMAT_R32G32B32_FLOAT,
                0,
                12,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            },

            {
                "TEXCOORD",
                0,
                DXGI_FORMAT_R32G32_FLOAT,
                0,
                24,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            },

            {
                "COLOR",
                0,
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                0,
                32,
                D3D11_INPUT_PER_VERTEX_DATA,
                0
            }
        };


        hr =
            device->CreateInputLayout(
                layout,
                ARRAYSIZE(layout),
                vsBlob->GetBufferPointer(),
                vsBlob->GetBufferSize(),
                &inputLayout);


        return SUCCEEDED(hr);
    }


    bool PreviewRenderer::CreateStates(
        ID3D11Device* device)
    {
        D3D11_SAMPLER_DESC samplerDesc{};

        samplerDesc.Filter =
            D3D11_FILTER_MIN_MAG_MIP_LINEAR;

        samplerDesc.AddressU =
            D3D11_TEXTURE_ADDRESS_WRAP;

        samplerDesc.AddressV =
            D3D11_TEXTURE_ADDRESS_WRAP;

        samplerDesc.AddressW =
            D3D11_TEXTURE_ADDRESS_WRAP;

        samplerDesc.MaxLOD =
            D3D11_FLOAT32_MAX;


        if (FAILED(
            device->CreateSamplerState(
                &samplerDesc,
                &sampler)))
        {
            return false;
        }


        D3D11_RASTERIZER_DESC raster{};

        raster.FillMode =
            D3D11_FILL_SOLID;

        // NIF winding and Skyrim's left-handed preview camera can differ;
        // keep both sides visible so imported shapes never vanish.
        raster.CullMode =
            D3D11_CULL_NONE;

        raster.DepthClipEnable =
            TRUE;


        if (FAILED(
            device->CreateRasterizerState(
                &raster,
                &solidRasterizer)))
        {
            return false;
        }


        raster.FillMode =
            D3D11_FILL_WIREFRAME;

        raster.CullMode =
            D3D11_CULL_NONE;


        if (FAILED(
            device->CreateRasterizerState(
                &raster,
                &wireRasterizer)))
        {
            return false;
        }


        D3D11_DEPTH_STENCIL_DESC depth{};

        depth.DepthEnable =
            TRUE;

        depth.DepthWriteMask =
            D3D11_DEPTH_WRITE_MASK_ALL;

        depth.DepthFunc =
            D3D11_COMPARISON_LESS;


        return SUCCEEDED(
            device->CreateDepthStencilState(
                &depth,
                &depthState));
    }


    bool PreviewRenderer::CreateConstantBuffer(
        ID3D11Device* device)
    {
        D3D11_BUFFER_DESC desc{};

        desc.ByteWidth =
            sizeof(FrameConstants);

        desc.Usage =
            D3D11_USAGE_DYNAMIC;

        desc.BindFlags =
            D3D11_BIND_CONSTANT_BUFFER;

        desc.CPUAccessFlags =
            D3D11_CPU_ACCESS_WRITE;


        return SUCCEEDED(
            device->CreateBuffer(
                &desc,
                nullptr,
                &constantBuffer));
    }


    void PreviewRenderer::SetNif(
        const NifFile* nif)
    {
        currentNif = nif;

        meshes.clear();

        stats = {};

        dirty = true;
    }


    void PreviewRenderer::SetSelectedMesh(
        int index)
    {
        selectedMesh = index;
    }


    void PreviewRenderer::SetOptions(
        bool a_wireframe,
        bool a_showCollision,
        bool a_showNormals,
        bool a_showUV)
    {
        wireframe = a_wireframe;
        showCollision = a_showCollision;
        showNormals = a_showNormals;
        showUV = a_showUV;
    }


    void PreviewRenderer::UpdateCamera(
        float yaw,
        float pitch,
        float distance,
        DirectX::XMFLOAT3 target)
    {
        cameraYaw = yaw;
        cameraPitch = pitch;
        cameraDistance =
            std::max(
                0.01f,
                distance);

        cameraTarget = target;
    }


    void PreviewRenderer::SetTextureOverride(
        const std::string& original,
        const std::string& replacement)
    {
        textureOverrides[
            original] =
            replacement;

        dirty = true;
    }


    void PreviewRenderer::ClearTextureOverrides()
    {
        textureOverrides.clear();

        dirty = true;
    }


    bool PreviewRenderer::BuildMeshBuffers(
        ID3D11Device* device)
    {
        meshes.clear();

        stats = {};

        if (!currentNif) {
            return true;
        }


        for (const auto& nifMesh : currentNif->meshes)
        {
            if (
                nifMesh.vertices.empty() ||
                nifMesh.indices.empty())
            {
                continue;
            }


            MeshBuffer buffer;


            std::vector<Vertex>
                vertices;

            vertices.reserve(
                nifMesh.vertices.size());


            for (const auto& vertex :
                 nifMesh.vertices)
            {
                Vertex v{};

                const NifVec3 previewPosition = TransformPreviewPosition(vertex.pos, nifMesh.previewTransform);
                v.position = { previewPosition.x, previewPosition.y, previewPosition.z };

                const NifVec3 previewNormal = TransformPreviewNormal(
                    { vertex.normal.x, vertex.normal.y, vertex.normal.z }, nifMesh.previewTransform);
                v.normal = { previewNormal.x, previewNormal.y, previewNormal.z };


                if (
                    DirectX::XMVectorGetX(
                        DirectX::XMVector3Length(
                            DirectX::XMLoadFloat3(
                                reinterpret_cast<
                                    const DirectX::XMFLOAT3*>(
                                        &v.normal)))) < 0.0001f)
                {
                    v.normal =
                    {
                        0.0f,
                        0.0f,
                        1.0f
                    };
                }


                v.uv =
                {
                    vertex.uv.u,
                    vertex.uv.v
                };

                v.color =
                    vertex.color;


                vertices.push_back(v);
            }


            if (vertices.size() > (std::numeric_limits<UINT>::max)() / sizeof(Vertex)) {
                logger::error("SkyLab preview: '{}' vertex buffer exceeds D3D11 size limits", nifMesh.name);
                continue;
            }
            D3D11_BUFFER_DESC vb{};
            vb.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
            vb.Usage = D3D11_USAGE_IMMUTABLE;
            vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vbData{};
            vbData.pSysMem = vertices.data();
            HRESULT vertexResult = device->CreateBuffer(&vb, &vbData, &buffer.vertexBuffer);
            if (FAILED(vertexResult)) {
                // Some injected/game D3D configurations reject immutable
                // resources. Retry with the universally supported default
                // usage before giving up on GPU preview for this mesh.
                vb.Usage = D3D11_USAGE_DEFAULT;
                vertexResult = device->CreateBuffer(&vb, &vbData, &buffer.vertexBuffer);
            }
            if (FAILED(vertexResult)) {
                logger::error("SkyLab preview: vertex buffer failed for '{}' ({} vertices, {} bytes): 0x{:08X}", nifMesh.name, vertices.size(), vb.ByteWidth, static_cast<uint32_t>(vertexResult));
                continue;
            }


            buffer.vertexCount =
                static_cast<uint32_t>(
                    vertices.size());


            if (nifMesh.indices.size() > (std::numeric_limits<UINT>::max)() / sizeof(uint32_t)) {
                logger::error("SkyLab preview: '{}' index buffer exceeds D3D11 size limits", nifMesh.name);
                continue;
            }
            D3D11_BUFFER_DESC ib{};
            ib.ByteWidth = static_cast<UINT>(nifMesh.indices.size() * sizeof(uint32_t));
            ib.Usage = D3D11_USAGE_IMMUTABLE;
            ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ibData{};
            ibData.pSysMem = nifMesh.indices.data();
            HRESULT indexResult = device->CreateBuffer(&ib, &ibData, &buffer.indexBuffer);
            if (FAILED(indexResult)) {
                ib.Usage = D3D11_USAGE_DEFAULT;
                indexResult = device->CreateBuffer(&ib, &ibData, &buffer.indexBuffer);
            }
            if (FAILED(indexResult)) {
                logger::error("SkyLab preview: index buffer failed for '{}' ({} indices, {} bytes): 0x{:08X}", nifMesh.name, nifMesh.indices.size(), ib.ByteWidth, static_cast<uint32_t>(indexResult));
                continue;
            }


            buffer.indexCount =
                static_cast<uint32_t>(
                    nifMesh.indices.size());

            buffer.texture =
                nifMesh.diffuseTexture;


            auto overrideIt =
                textureOverrides.find(
                    buffer.texture);


            if (
                overrideIt !=
                textureOverrides.end())
            {
                buffer.texture =
                    overrideIt->second;
            }


            buffer.diffuse =
                TextureManager::Get()
                    .GetSRV(buffer.texture);

            if (!buffer.diffuse) {
                buffer.diffuse = fallbackDiffuse;
            }


            stats.verts +=
                buffer.vertexCount;

            stats.tris +=
                buffer.indexCount / 3;

            ++stats.meshes;


            meshes.push_back(
                std::move(buffer));
        }


        dirty = false;

        return true;
    }


    bool PreviewRenderer::CreateDepthBuffer(
        ID3D11Device* device,
        uint32_t width,
        uint32_t height)
    {
        depthView.Reset();
        depthTexture.Reset();


        D3D11_TEXTURE2D_DESC desc{};

        desc.Width = width;
        desc.Height = height;

        desc.MipLevels = 1;
        desc.ArraySize = 1;

        desc.Format =
            DXGI_FORMAT_D24_UNORM_S8_UINT;

        desc.SampleDesc.Count = 1;

        desc.Usage =
            D3D11_USAGE_DEFAULT;

        desc.BindFlags =
            D3D11_BIND_DEPTH_STENCIL;


        if (FAILED(
            device->CreateTexture2D(
                &desc,
                nullptr,
                &depthTexture)))
        {
            return false;
        }


        if (FAILED(
            device->CreateDepthStencilView(
                depthTexture.Get(),
                nullptr,
                &depthView)))
        {
            return false;
        }


        depthWidth = width;
        depthHeight = height;

        return true;
    }


    void PreviewRenderer::ReloadShaders(
        ID3D11Device* device)
    {
        vertexShader.Reset();
        pixelShader.Reset();
        inputLayout.Reset();

        CreateShaders(device);

        dirty = true;
    }


    void PreviewRenderer::Render(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* target,
        float width,
        float height)
    {
        if (
            !initialized ||
            !context ||
            !target ||
            width <= 1.0f ||
            height <= 1.0f)
        {
            return;
        }


        ComPtr<ID3D11Device> device;

        context->GetDevice(
            &device);


        if (!device) {
            return;
        }


        if (dirty) {
            BuildMeshBuffers(
                device.Get());
        }


        const uint32_t viewportWidth =
            std::max(
                1u,
                static_cast<uint32_t>(
                    width));

        const uint32_t viewportHeight =
            std::max(
                1u,
                static_cast<uint32_t>(
                    height));


        if (
            !depthView ||
            depthWidth != viewportWidth ||
            depthHeight != viewportHeight)
        {
            if (!CreateDepthBuffer(
                device.Get(),
                viewportWidth,
                viewportHeight)) {
                logger::error("SkyLab preview: failed to create viewport depth buffer");
                return;
            }
        }


        D3D11_VIEWPORT viewport{};

        viewport.Width = width;
        viewport.Height = height;

        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;


        context->RSSetViewports(
            1,
            &viewport);


        context->OMSetRenderTargets(
            1,
            &target,
            depthView.Get());

        const float clearColor[4] = { 0.075f, 0.080f, 0.090f, 1.0f };
        context->ClearRenderTargetView(target, clearColor);
        context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);


        context->OMSetDepthStencilState(
            depthState.Get(),
            0);


        context->RSSetState(
            wireframe
                ? wireRasterizer.Get()
                : solidRasterizer.Get());


        context->IASetInputLayout(
            inputLayout.Get());


        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


        context->VSSetShader(
            vertexShader.Get(),
            nullptr,
            0);


        context->PSSetShader(
            pixelShader.Get(),
            nullptr,
            0);


        context->PSSetSamplers(
            0,
            1,
            sampler.GetAddressOf());


        float targetX =
            cameraTarget.x;

        float targetY =
            cameraTarget.y;

        float targetZ =
            cameraTarget.z;


        if (
            currentNif &&
            cameraTarget.x == 0.0f &&
            cameraTarget.y == 0.0f &&
            cameraTarget.z == 0.0f)
        {
            targetX =
                (currentNif->boundsMin.x +
                 currentNif->boundsMax.x) *
                0.5f;

            targetY =
                (currentNif->boundsMin.y +
                 currentNif->boundsMax.y) *
                0.5f;

            targetZ =
                (currentNif->boundsMin.z +
                 currentNif->boundsMax.z) *
                0.5f;
        }


        using namespace DirectX;


        XMVECTOR targetPosition =
            XMVectorSet(
                targetX,
                targetY,
                targetZ,
                1.0f);


        const float cp =
            cameraPitch;

        const float cy =
            cameraYaw;


        XMVECTOR eye =
            XMVectorSet(
                targetX +
                    cosf(cp) *
                    sinf(cy) *
                    cameraDistance,

                targetY +
                    cosf(cp) *
                    cosf(cy) *
                    cameraDistance,

                targetZ +
                    sinf(cp) *
                    cameraDistance,

                1.0f);


        XMMATRIX view =
            XMMatrixLookAtLH(
                eye,
                targetPosition,
                XMVectorSet(
                    0.0f,
                    0.0f,
                    1.0f,
                    0.0f));


        XMMATRIX projection =
            XMMatrixPerspectiveFovLH(
                XMConvertToRadians(fov),
                width / height,
                0.01f,
                100000.0f);


        XMMATRIX vp =
            view * projection;


        XMFLOAT4 cameraPosition;

        XMStoreFloat4(
            &cameraPosition,
            eye);


        for (size_t i = 0;
             i < meshes.size();
             ++i)
        {
            auto& mesh =
                meshes[i];


            if (
                !mesh.vertexBuffer ||
                !mesh.indexBuffer)
            {
                continue;
            }


            XMMATRIX world =
                XMMatrixIdentity();


            FrameConstants constants{};

            XMStoreFloat4x4(
                &constants.mvp,
                XMMatrixTranspose(
                    world * vp));


            XMStoreFloat4x4(
                &constants.world,
                XMMatrixTranspose(
                    world));


            constants.lightDirection =
            {
                0.45f,
                0.80f,
                0.30f,
                0.0f
            };


            constants.cameraPosition =
                cameraPosition;


            constants.options =
            {
                selectedMesh ==
                    static_cast<int>(i)
                    ? 1.0f
                    : 0.0f,

                showNormals
                    ? 1.0f
                    : 0.0f,

                showUV
                    ? 1.0f
                    : 0.0f,

                0.0f
            };


            D3D11_MAPPED_SUBRESOURCE mapped{};


            if (SUCCEEDED(
                context->Map(
                    constantBuffer.Get(),
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mapped)))
            {
                std::memcpy(
                    mapped.pData,
                    &constants,
                    sizeof(constants));

                context->Unmap(
                    constantBuffer.Get(),
                    0);
            }


            context->VSSetConstantBuffers(
                0,
                1,
                constantBuffer.GetAddressOf());


            context->PSSetConstantBuffers(
                0,
                1,
                constantBuffer.GetAddressOf());


            const UINT stride =
                sizeof(Vertex);

            const UINT offset = 0;


            context->IASetVertexBuffers(
                0,
                1,
                mesh.vertexBuffer.GetAddressOf(),
                &stride,
                &offset);


            context->IASetIndexBuffer(
                mesh.indexBuffer.Get(),
                DXGI_FORMAT_R32_UINT,
                0);


            ID3D11ShaderResourceView*
                srv =
                    mesh.diffuse.Get();


            context->PSSetShaderResources(
                0,
                1,
                &srv);


            context->DrawIndexed(
                mesh.indexCount,
                0,
                0);
        }


        ID3D11ShaderResourceView*
            nullSRV = nullptr;

        context->PSSetShaderResources(
            0,
            1,
            &nullSRV);
    }
}
