#pragma once

#include "PCH.h"
#include <cmath>

namespace SkyLab
{
    struct NifVec2
    {
        float u{ 0.0f };
        float v{ 0.0f };
    };

    struct NifVec3
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct NifMat33
    {
        float m[3][3]{
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        };
    };

    struct NifTransform
    {
        NifVec3 translation{};
        NifMat33 rotation{};
        float scale{ 1.0f };
    };

    struct NifPreviewTransform
    {
        NifVec3 translation{};
        NifVec3 rotationDegrees{};
        NifVec3 axisScale{ 1.0f, 1.0f, 1.0f };
        float scale{ 1.0f };
        bool mirrorX{ false };
        bool mirrorY{ false };
        bool mirrorZ{ false };
    };

    inline NifVec3 RotatePreviewVector(const NifVec3& value, const NifPreviewTransform& transform)
    {
        constexpr float degreesToRadians = 0.01745329251994329577f;
        const float cx = std::cos(transform.rotationDegrees.x * degreesToRadians);
        const float sx = std::sin(transform.rotationDegrees.x * degreesToRadians);
        const float cy = std::cos(transform.rotationDegrees.y * degreesToRadians);
        const float sy = std::sin(transform.rotationDegrees.y * degreesToRadians);
        const float cz = std::cos(transform.rotationDegrees.z * degreesToRadians);
        const float sz = std::sin(transform.rotationDegrees.z * degreesToRadians);

        const float x1 = value.x;
        const float y1 = value.y * cx - value.z * sx;
        const float z1 = value.y * sx + value.z * cx;
        const float x2 = x1 * cy + z1 * sy;
        const float y2 = y1;
        const float z2 = -x1 * sy + z1 * cy;
        return { x2 * cz - y2 * sz, x2 * sz + y2 * cz, z2 };
    }

    inline NifVec3 TransformPreviewPosition(const NifVec3& value, const NifPreviewTransform& transform)
    {
        const float mirrorX = transform.mirrorX ? -1.0f : 1.0f;
        const float mirrorY = transform.mirrorY ? -1.0f : 1.0f;
        const float mirrorZ = transform.mirrorZ ? -1.0f : 1.0f;
        const NifVec3 rotated = RotatePreviewVector(
            { value.x * transform.scale * transform.axisScale.x * mirrorX,
              value.y * transform.scale * transform.axisScale.y * mirrorY,
              value.z * transform.scale * transform.axisScale.z * mirrorZ }, transform);
        return { rotated.x + transform.translation.x, rotated.y + transform.translation.y, rotated.z + transform.translation.z };
    }

    inline NifVec3 TransformPreviewNormal(const NifVec3& value, const NifPreviewTransform& transform)
    {
        const float sx = std::max(std::abs(transform.axisScale.x), 0.0001f);
        const float sy = std::max(std::abs(transform.axisScale.y), 0.0001f);
        const float sz = std::max(std::abs(transform.axisScale.z), 0.0001f);
        NifVec3 normal = RotatePreviewVector({
            value.x * (transform.mirrorX ? -1.0f : 1.0f) / sx,
            value.y * (transform.mirrorY ? -1.0f : 1.0f) / sy,
            value.z * (transform.mirrorZ ? -1.0f : 1.0f) / sz
        }, transform);
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0.0001f) {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }
        return normal;
    }

    struct NifVertex
    {
        NifVec3 pos{};
        NifVec3 normal{};
        NifVec2 uv{};
        NifVec2 uv2{};

        DirectX::XMFLOAT4 color{
            1.0f,
            1.0f,
            1.0f,
            1.0f
        };

        DirectX::XMFLOAT4 tangent{
            0.0f,
            0.0f,
            0.0f,
            1.0f
        };
    };

    struct NifMesh
    {
        std::string name;
        std::string blockType;
        // Data-relative or filesystem path for the NIF this mesh came from.
        // Runtime actor snapshots can contain geometry from several assets.
        std::string sourcePath;
        uint32_t blockIndex{ 0 };

        NifTransform transform{};

        std::vector<NifVertex> vertices;
        std::vector<uint32_t> indices;

        // Shader / material information
        std::string shaderType;

        std::string diffuseTexture;
        std::string normalTexture;
        std::string glowTexture;
        std::string specularTexture;

        int32_t shaderPropertyBlock{ -1 };
        int32_t textureSetBlock{ -1 };

        // Geometry flags
        bool skinned{ false };
        bool hasVertexColors{ false };
        bool hasUV{ false };
        bool hasNormals{ false };
        bool hasTangents{ false };
        bool hasAlpha{ false };
        bool doubleSided{ false };
        NifPreviewTransform previewTransform{};
    };

    struct NifNode
    {
        std::string name;
        std::string blockType;
        uint32_t blockIndex{ 0 };

        NifTransform transform{};

        int32_t parent{ -1 };

        std::vector<int32_t> children;
        std::vector<int32_t> meshIndices;

        // Collision information
        bool isCollision{ false };
        std::string collisionType;
    };

    struct NifCollision
    {
        std::string shapeType;

        std::vector<NifVec3> vertices;
        std::vector<uint32_t> indices;

        NifVec3 center{};
        NifVec3 halfExtents{};

        float radius{ 0.0f };
    };

    struct NifBlockInfo
    {
        uint32_t index{ 0 };
        uint16_t typeIndex{ 0 };
        uint32_t size{ 0 };

        std::string type;

        // Offset of the block payload inside the NIF file.
        size_t offset{ 0 };
    };

    struct NifFile
    {
        // ------------------------------------------------------------
        // NIF header
        // ------------------------------------------------------------

        std::string headerString;
        std::string versionString;

        uint32_t version{ 0 };
        uint32_t userVersion{ 0 };

        // Skyrim / Bethesda stream version.
        uint32_t bsVersion{ 0 };

        // NIF endian flag.
        // Skyrim SE NIFs are little-endian.
        uint8_t endian{ 1 };

        // BSStreamHeader fields.
        std::string creator;
        std::string exportScript;
        std::string processScript;

        // ------------------------------------------------------------
        // Block table
        // ------------------------------------------------------------

        uint32_t numBlocks{ 0 };

        std::vector<std::string> blockTypes;
        std::vector<NifBlockInfo> blocks;

        // ------------------------------------------------------------
        // Global string table
        // ------------------------------------------------------------

        std::vector<std::string> strings;

        // ------------------------------------------------------------
        // Parsed scene data
        // ------------------------------------------------------------

        std::vector<NifNode> nodes;
        std::vector<NifMesh> meshes;
        std::vector<NifCollision> collisions;

        int32_t rootIndex{ -1 };

        // ------------------------------------------------------------
        // Discovered resources
        // ------------------------------------------------------------

        std::vector<std::string> textureFiles;
        std::vector<std::string> bones;

        uint32_t numBones{ 0 };

        // ------------------------------------------------------------
        // Geometry statistics
        // ------------------------------------------------------------

        uint32_t totalVerts{ 0 };
        uint32_t totalTris{ 0 };

        NifVec3 boundsMin{
            FLT_MAX,
            FLT_MAX,
            FLT_MAX
        };

        NifVec3 boundsMax{
            -FLT_MAX,
            -FLT_MAX,
            -FLT_MAX
        };

        // ------------------------------------------------------------
        // File identification
        // ------------------------------------------------------------

        bool isSkyrimSE{ false };
        bool isValid{ false };

        std::string error;
    };

    class NifParser
    {
    public:
        NifParser() = delete;

        /*
         * Parse a NIF from a normal filesystem path.
         *
         * This is the low-level parser entry point. Asset resolution
         * (MO2/Vortex/BSA/virtual resources) can feed bytes into
         * ParseFromMemory when required.
         */
        static NifFile Parse(const fs::path& path);

        /*
         * Parse an already-loaded NIF buffer.
         *
         * This is important for Skyrim's virtual resource system and
         * archive-backed files because the NIF does not have to exist
         * as a loose filesystem file.
         */
        static NifFile ParseFromMemory(
            const uint8_t* data,
            size_t size,
            const std::string& label = "memory");

        // Full Nifly decoding fallback for NIF variants the lightweight
        // inspector cannot turn into preview geometry.
        static NifFile ParseWithNifly(const uint8_t* data, size_t size,
                                      const std::string& label = "memory");

        // Capture already-loaded Skyrim geometry. This handles live references
        // (including assembled NPCs) and backs up NIF variants the lightweight
        // block decoder cannot yet turn into preview geometry.
        static NifFile CaptureScene(RE::NiAVObject* root, const std::string& label);
        static NifFile CaptureScene(const std::vector<RE::NiAVObject*>& roots, const std::string& label);
        static NifFile ParseGameResource(const fs::path& path);

        /*
         * Produce a human-readable summary for the editor/log.
         */
        static std::string Summarize(const NifFile& file);

    };
}
