#include "PCH.h"
#include "NifParser.h"
#include "Utils.h"
#include <sstream>
#include <NifFile.hpp>
#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/S/Stream.h"

#include <limits>
#include <bit>
#include <cmath>
#include <cstring>

namespace SkyLab
{
    namespace
    {
        constexpr uint32_t kSkyrimSEVersion = 0x14020007;
        constexpr uint32_t kBethesdaMinVersion = 0x14000000;
        constexpr uint32_t kBlockSizeVersion = 0x14020005;
        constexpr uint32_t kStringTableVersion = 0x14010001;

        class Reader
        {
        public:
            Reader(const uint8_t* data, size_t size) :
                data_(data),
                size_(size)
            {}

            size_t Position() const
            {
                return position_;
            }

            size_t Remaining() const
            {
                return position_ <= size_ ? size_ - position_ : 0;
            }

            bool Seek(size_t position)
            {
                if (position > size_) {
                    return false;
                }

                position_ = position;
                return true;
            }

            bool Skip(size_t count)
            {
                if (count > Remaining()) {
                    return false;
                }

                position_ += count;
                return true;
            }

            bool ReadU8(uint8_t& value)
            {
                if (Remaining() < 1) {
                    return false;
                }

                value = data_[position_++];
                return true;
            }

            bool ReadU16(uint16_t& value)
            {
                if (Remaining() < 2) {
                    return false;
                }

                value =
                    static_cast<uint16_t>(data_[position_]) |
                    (static_cast<uint16_t>(data_[position_ + 1]) << 8);

                position_ += 2;
                return true;
            }

            bool ReadU32(uint32_t& value)
            {
                if (Remaining() < 4) {
                    return false;
                }

                value =
                    static_cast<uint32_t>(data_[position_]) |
                    (static_cast<uint32_t>(data_[position_ + 1]) << 8) |
                    (static_cast<uint32_t>(data_[position_ + 2]) << 16) |
                    (static_cast<uint32_t>(data_[position_ + 3]) << 24);

                position_ += 4;
                return true;
            }

            bool ReadU64(uint64_t& value)
            {
                uint32_t low = 0;
                uint32_t high = 0;
                if (!ReadU32(low) || !ReadU32(high)) return false;
                value = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
                return true;
            }

            bool ReadF32(float& value)
            {
                uint32_t bits = 0;
                if (!ReadU32(bits)) return false;
                value = std::bit_cast<float>(bits);
                return std::isfinite(value);
            }

            bool ReadString32(std::string& value)
            {
                uint32_t length = 0;

                if (!ReadU32(length)) {
                    return false;
                }

                if (length > Remaining()) {
                    return false;
                }

                value.assign(
                    reinterpret_cast<const char*>(data_ + position_),
                    length);

                position_ += length;

                while (!value.empty() && value.back() == '\0') {
                    value.pop_back();
                }

                return true;
            }

            /*
             * Bethesda NIF strings in the serialized header are NOT
             * fixed-size 64-byte fields.
             *
             * They are encoded as:
             *
             *   uint8 length
             *   char[length] bytes
             *
             * for the Bethesda header's Creator/ExportInfo fields.
             */
            bool ReadBethesdaString(std::string& value)
            {
                uint8_t length = 0;

                if (!ReadU8(length)) {
                    return false;
                }

                if (length > Remaining()) {
                    return false;
                }

                value.assign(
                    reinterpret_cast<const char*>(data_ + position_),
                    length);

                position_ += length;

                while (!value.empty() && value.back() == '\0') {
                    value.pop_back();
                }

                return true;
            }

            bool ReadBytes(std::vector<uint8_t>& output, size_t count)
            {
                if (count > Remaining()) {
                    return false;
                }

                output.assign(
                    data_ + position_,
                    data_ + position_ + count);

                position_ += count;
                return true;
            }

            const uint8_t* Data() const
            {
                return data_;
            }

        private:
            const uint8_t* data_{ nullptr };
            size_t size_{ 0 };
            size_t position_{ 0 };
        };

        bool IsBethesdaVersion(uint32_t version)
        {
            /*
             * Bethesda Skyrim/Fallout NIFs use versions in the
             * 0x14xxxxxx range.
             */
            return (version & 0xFF000000u) == 0x14000000u;
        }

        std::string VersionToString(uint32_t version)
        {
            const uint32_t major = (version >> 24) & 0xFF;
            const uint32_t minor = (version >> 16) & 0xFF;
            const uint32_t patch = (version >> 8) & 0xFF;
            const uint32_t build = version & 0xFF;

            return std::format(
                "{}.{}.{}.{}",
                major,
                minor,
                patch,
                build);
        }

        bool IsReasonableBlockTypeIndex(
            uint16_t index,
            size_t blockTypeCount)
        {
            return index < blockTypeCount;
        }

        float HalfToFloat(uint16_t half)
        {
            const uint32_t sign = static_cast<uint32_t>(half & 0x8000u) << 16;
            int exponent = static_cast<int>((half >> 10) & 0x1Fu);
            uint32_t mantissa = half & 0x03FFu;
            uint32_t bits = 0;
            if (exponent == 0) {
                if (mantissa == 0) bits = sign;
                else {
                    exponent = 1;
                    while ((mantissa & 0x0400u) == 0) { mantissa <<= 1; --exponent; }
                    mantissa &= 0x03FFu;
                    bits = sign | (static_cast<uint32_t>(exponent + (127 - 15)) << 23) | (mantissa << 13);
                }
            } else if (exponent == 31) {
                bits = sign | 0x7F800000u | (mantissa << 13);
            } else {
                bits = sign | (static_cast<uint32_t>(exponent + (127 - 15)) << 23) | (mantissa << 13);
            }
            return std::bit_cast<float>(bits);
        }

        bool ReadHalf(Reader& reader, float& value)
        {
            uint16_t bits = 0;
            if (!reader.ReadU16(bits)) return false;
            value = HalfToFloat(bits);
            return std::isfinite(value);
        }

        bool ReadVec3(Reader& reader, NifVec3& value)
        {
            return reader.ReadF32(value.x) && reader.ReadF32(value.y) && reader.ReadF32(value.z);
        }

        bool ParseBSTriShape(
            const uint8_t* data,
            size_t dataSize,
            const NifBlockInfo& block,
            NifFile& file,
            NifMesh& mesh)
        {
            if (!data || block.offset > dataSize || block.size > dataSize - block.offset) return false;
            Reader r(data, dataSize);
            if (!r.Seek(block.offset)) return false;
            const size_t end = block.offset + block.size;

            auto readU32 = [&](uint32_t& out) { return r.ReadU32(out) && r.Position() <= end; };
            auto readI32 = [&](int32_t& out) { uint32_t raw = 0; if (!readU32(raw)) return false; out = static_cast<int32_t>(raw); return true; };
            auto readFloat = [&](float& out) { return r.ReadF32(out) && r.Position() <= end; };

            // NiObjectNET fields: string-table name, extra-data refs, controller.
            uint32_t nameIndex = 0;
            uint32_t extraCount = 0;
            if (!readU32(nameIndex) || !readU32(extraCount) || r.Position() > end || extraCount > 65536 ||
                static_cast<uint64_t>(extraCount) * 4 > end - r.Position()) return false;
            if (nameIndex < file.strings.size()) mesh.name = file.strings[nameIndex];
            if (!r.Skip(static_cast<size_t>(extraCount) * 4) || !r.Skip(4)) return false;

            // Skyrim BSTriShape duplicates the NiAVObject transform and collision fields.
            NifVec3 translation{};
            NifMat33 rotation{};
            float scale = 1.0f;
            int32_t unusedRef = -1;
            NifVec3 boundCenter{};
            float boundRadius = 0.0f;
            uint32_t avFlags = 0;
            if (!readU32(avFlags) || !ReadVec3(r, translation)) return false;
            for (size_t column = 0; column < 3; ++column)
                for (size_t row = 0; row < 3; ++row)
                    if (!readFloat(rotation.m[row][column])) return false;
            if (!readFloat(scale) || !readI32(unusedRef) || !ReadVec3(r, boundCenter) || !readFloat(boundRadius)) return false;

            int32_t shaderRef = -1;
            if (!readI32(unusedRef) || !readI32(shaderRef) || !readI32(unusedRef)) return false;
            uint64_t vertexDesc = 0;
            uint32_t triangleCount = 0;
            uint16_t vertexCount = 0;
            uint32_t vertexDataSize = 0;
            if (!r.ReadU64(vertexDesc)) return false;
            if (file.userVersion >= 12 && file.bsVersion < 130) {
                uint16_t smallTriangleCount = 0;
                if (!r.ReadU16(smallTriangleCount)) return false;
                triangleCount = smallTriangleCount;
            } else if (!readU32(triangleCount)) return false;
            if (!r.ReadU16(vertexCount) || !readU32(vertexDataSize)) return false;
            if (r.Position() > end || vertexCount > 1000000 || triangleCount > 2000000) return false;

            // BSVertexDesc bits 0..3 store the vertex record size in DWORDs.
            // The UV/normal/color offsets are separate nibbles beginning at
            // bit 8; treating those offsets as the size rejects many BSA
            // meshes (including skinned weapons) before their vertices load.
            const uint32_t mainSize = static_cast<uint32_t>(vertexDesc & 0xFu) * 4u;
            const uint32_t flags = static_cast<uint32_t>(vertexDesc >> 44);
            const uint64_t triangleBytes = static_cast<uint64_t>(triangleCount) * 6;
            if (vertexCount == 0 || triangleCount == 0 || mainSize < 8 || vertexDataSize < triangleBytes) return false;
            const uint64_t vertexBytes = vertexDataSize - triangleBytes;
            if (vertexBytes % vertexCount != 0) return false;
            const uint32_t stride = static_cast<uint32_t>(vertexBytes / vertexCount);
            if (stride < mainSize || stride > 256 || vertexDataSize > end - r.Position()) return false;

            mesh.transform.translation = translation;
            mesh.transform.rotation = rotation;
            mesh.transform.scale = scale;
            mesh.skinned = (flags & (1u << 6)) != 0;
            mesh.hasUV = (flags & (1u << 1)) != 0;
            mesh.hasNormals = (flags & (1u << 3)) != 0;
            mesh.hasTangents = (flags & (1u << 4)) != 0;
            mesh.hasVertexColors = (flags & (1u << 5)) != 0;
            mesh.shaderPropertyBlock = shaderRef;

            mesh.vertices.clear();
            mesh.indices.clear();
            mesh.vertices.reserve(vertexCount);
            const bool fullPrecision = (flags & (1u << 10)) != 0;
            auto attributeOffset = [&](uint32_t attribute) {
                return static_cast<uint32_t>((vertexDesc >> (4u * attribute + 2u)) & 0x3Cu);
            };
            std::vector<uint8_t> record;
            for (uint32_t i = 0; i < vertexCount; ++i) {
                if (!r.ReadBytes(record, stride)) return false;
                Reader vertexReader(record.data(), record.size());
                NifVertex vertex{};
                if (fullPrecision) {
                    if (mainSize < 16 || !vertexReader.ReadF32(vertex.pos.x) || !vertexReader.ReadF32(vertex.pos.y) || !vertexReader.ReadF32(vertex.pos.z)) return false;
                } else {
                    if (!ReadHalf(vertexReader, vertex.pos.x) || !ReadHalf(vertexReader, vertex.pos.y) || !ReadHalf(vertexReader, vertex.pos.z)) return false;
                }
                if (mesh.hasUV) {
                    const uint32_t offset = attributeOffset(1);
                    if (offset + 4 > stride || !vertexReader.Seek(offset) || !ReadHalf(vertexReader, vertex.uv.u) || !ReadHalf(vertexReader, vertex.uv.v)) return false;
                }
                if (mesh.hasNormals) {
                    const uint32_t offset = attributeOffset(3);
                    if (offset + 4 > stride || !vertexReader.Seek(offset)) return false;
                    uint8_t n[4]{};
                    for (auto& b : n) if (!vertexReader.ReadU8(b)) return false;
                    vertex.normal = { (n[0] / 255.0f) * 2.0f - 1.0f, (n[1] / 255.0f) * 2.0f - 1.0f, (n[2] / 255.0f) * 2.0f - 1.0f };
                }
                if (mesh.hasTangents) {
                    const uint32_t offset = attributeOffset(4);
                    if (offset + 4 > stride || !vertexReader.Seek(offset) || !vertexReader.Skip(4)) return false;
                }
                if (mesh.hasVertexColors) {
                    const uint32_t offset = attributeOffset(5);
                    if (offset + 4 > stride || !vertexReader.Seek(offset)) return false;
                    uint8_t c[4]{};
                    for (auto& b : c) if (!vertexReader.ReadU8(b)) return false;
                    vertex.color = DirectX::XMFLOAT4(c[0]/255.0f, c[1]/255.0f, c[2]/255.0f, c[3]/255.0f);
                }

                // Apply the shape-local transform so preview geometry is in NIF model space.
                const float x = vertex.pos.x, y = vertex.pos.y, z = vertex.pos.z;
                vertex.pos = {
                    (x*rotation.m[0][0] + y*rotation.m[1][0] + z*rotation.m[2][0]) * scale + translation.x,
                    (x*rotation.m[0][1] + y*rotation.m[1][1] + z*rotation.m[2][1]) * scale + translation.y,
                    (x*rotation.m[0][2] + y*rotation.m[1][2] + z*rotation.m[2][2]) * scale + translation.z
                };
                const float nx = vertex.normal.x, ny = vertex.normal.y, nz = vertex.normal.z;
                vertex.normal = {
                    nx*rotation.m[0][0] + ny*rotation.m[1][0] + nz*rotation.m[2][0],
                    nx*rotation.m[0][1] + ny*rotation.m[1][1] + nz*rotation.m[2][1],
                    nx*rotation.m[0][2] + ny*rotation.m[1][2] + nz*rotation.m[2][2]
                };
                mesh.vertices.push_back(vertex);
            }

            mesh.indices.reserve(static_cast<size_t>(triangleCount) * 3);
            for (uint32_t i = 0; i < triangleCount; ++i) {
                uint16_t a = 0, b = 0, c = 0;
                if (!r.ReadU16(a) || !r.ReadU16(b) || !r.ReadU16(c) || a >= vertexCount || b >= vertexCount || c >= vertexCount) return false;
                mesh.indices.insert(mesh.indices.end(), { a, b, c });
            }
            mesh.name = mesh.name.empty() ? block.type : mesh.name;
            return true;
        }

        bool ParseTriShapeData(
            const uint8_t* data,
            size_t dataSize,
            const NifBlockInfo& block,
            const NifFile& file,
            NifMesh& mesh)
        {
            if (!data || block.offset > dataSize || block.size > dataSize - block.offset ||
                file.version != kSkyrimSEVersion || file.bsVersion == 0) {
                return false;
            }

            Reader r(data, dataSize);
            if (!r.Seek(block.offset)) return false;
            const size_t end = block.offset + block.size;
            auto readU8 = [&](uint8_t& out) { return r.ReadU8(out) && r.Position() <= end; };
            auto readU16 = [&](uint16_t& out) { return r.ReadU16(out) && r.Position() <= end; };
            auto readU32 = [&](uint32_t& out) { return r.ReadU32(out) && r.Position() <= end; };
            auto readFloat = [&](float& out) { return r.ReadF32(out) && r.Position() <= end; };
            auto skipChecked = [&](size_t count) { return r.Skip(count) && r.Position() <= end; };

            // NiGeometryData, in Skyrim's 20.2.0.7 layout.
            if (file.version >= 0x0A010072u && !skipChecked(4)) return false; // Group ID
            uint16_t vertexCount = 0;
            if (!readU16(vertexCount) || vertexCount == 0 || vertexCount > 1000000) return false;
            if (file.version >= 0x0A010000u && !skipChecked(2)) return false; // Keep/Compress flags
            uint8_t hasVertices = 0;
            if (!readU8(hasVertices) || !hasVertices) return false;

            mesh.vertices.assign(vertexCount, NifVertex{});
            for (auto& vertex : mesh.vertices) {
                if (!ReadVec3(r, vertex.pos) || r.Position() > end) return false;
            }

            uint16_t dataFlags = 0;
            if (!readU16(dataFlags)) return false;
            if (file.bsVersion > 34 && !skipChecked(4)) return false; // Material CRC

            uint8_t hasNormals = 0;
            if (!readU8(hasNormals)) return false;
            mesh.hasNormals = hasNormals != 0;
            if (mesh.hasNormals) {
                for (auto& vertex : mesh.vertices) {
                    if (!ReadVec3(r, vertex.normal) || r.Position() > end) return false;
                }
            }

            const bool hasTangents = mesh.hasNormals && (dataFlags & 0x1000u) != 0;
            mesh.hasTangents = hasTangents;
            if (hasTangents) {
                std::vector<NifVec3> tangents(vertexCount);
                for (uint32_t i = 0; i < vertexCount; ++i) {
                    if (!ReadVec3(r, tangents[i]) || r.Position() > end) return false;
                }
                for (uint32_t i = 0; i < vertexCount; ++i) {
                    NifVec3 bitangent{};
                    if (!ReadVec3(r, bitangent) || r.Position() > end) return false;
                    mesh.vertices[i].tangent = DirectX::XMFLOAT4(tangents[i].x, tangents[i].y, tangents[i].z, 1.0f);
                }
            }

            NifVec3 boundCenter{};
            float boundRadius = 0.0f;
            if (!ReadVec3(r, boundCenter) || !readFloat(boundRadius)) return false;

            uint8_t hasColors = 0;
            if (!readU8(hasColors)) return false;
            mesh.hasVertexColors = hasColors != 0;
            if (mesh.hasVertexColors) {
                for (auto& vertex : mesh.vertices) {
                    if (!readFloat(vertex.color.x) || !readFloat(vertex.color.y) ||
                        !readFloat(vertex.color.z) || !readFloat(vertex.color.w)) return false;
                }
            }

            const uint16_t uvSetCount = (dataFlags & 0x0001u) != 0 ? 1u : 0u;
            if (uvSetCount > 0) {
                mesh.hasUV = true;
                for (auto& vertex : mesh.vertices) {
                    if (!readFloat(vertex.uv.u) || !readFloat(vertex.uv.v)) return false;
                }
                // Skyrim meshes commonly contain only one UV set. Safely
                // consume any additional sets while retaining the first.
                const uint64_t extraUvBytes = static_cast<uint64_t>(uvSetCount - 1) * vertexCount * 8;
                if (extraUvBytes > end - r.Position() || !skipChecked(static_cast<size_t>(extraUvBytes))) return false;
            }

            if (!skipChecked(2)) return false; // Consistency flags
            if (file.version >= 0x14000004u && !skipChecked(4)) return false; // Additional data reference

            uint16_t triangleCount = 0;
            if (!readU16(triangleCount)) return false;
            mesh.indices.clear();
            if (block.type == "NiTriShapeData") {
                uint32_t trianglePointCount = 0;
                uint8_t hasTriangles = 0;
                if (!readU32(trianglePointCount) || trianglePointCount > 6000000 || !readU8(hasTriangles)) return false;
                if (hasTriangles) {
                    if (trianglePointCount != static_cast<uint32_t>(triangleCount) * 3u ||
                        static_cast<uint64_t>(triangleCount) * 6u > end - r.Position()) return false;
                    mesh.indices.reserve(trianglePointCount);
                    for (uint32_t i = 0; i < triangleCount; ++i) {
                        uint16_t a = 0, b = 0, c = 0;
                        if (!readU16(a) || !readU16(b) || !readU16(c) ||
                            a >= vertexCount || b >= vertexCount || c >= vertexCount) return false;
                        mesh.indices.insert(mesh.indices.end(), { a, b, c });
                    }
                }

                uint16_t matchGroupCount = 0;
                if (!readU16(matchGroupCount)) return false;
                for (uint16_t group = 0; group < matchGroupCount; ++group) {
                    uint16_t groupVertexCount = 0;
                    if (!readU16(groupVertexCount) || !skipChecked(static_cast<size_t>(groupVertexCount) * 2)) return false;
                }
            } else if (block.type == "NiTriStripsData") {
                uint16_t stripCount = 0;
                if (!readU16(stripCount)) return false;
                std::vector<uint16_t> stripLengths(stripCount);
                uint64_t pointCount = 0;
                for (auto& length : stripLengths) {
                    if (!readU16(length)) return false;
                    pointCount += length;
                }
                uint8_t hasPoints = 0;
                if (!readU8(hasPoints) || !hasPoints || pointCount > 6000000 || pointCount * 2 > end - r.Position()) return false;
                for (const auto length : stripLengths) {
                    std::vector<uint16_t> points(length);
                    for (auto& point : points) {
                        if (!readU16(point) || point >= vertexCount) return false;
                    }
                    for (size_t i = 2; i < points.size(); ++i) {
                        if ((i & 1u) == 0) mesh.indices.insert(mesh.indices.end(), { points[i - 2], points[i - 1], points[i] });
                        else mesh.indices.insert(mesh.indices.end(), { points[i - 1], points[i - 2], points[i] });
                    }
                }
            } else {
                return false;
            }

            mesh.name = mesh.name.empty() ? block.type : mesh.name;
            mesh.skinned = false;
            return !mesh.vertices.empty() && !mesh.indices.empty();
        }

        int32_t FindReferencedBlock(
            const uint8_t* data,
            size_t dataSize,
            const NifBlockInfo& owner,
            const NifFile& file,
            std::string_view targetType,
            size_t* matchedOffset = nullptr)
        {
            if (!data || owner.offset > dataSize || owner.size > dataSize - owner.offset) return -1;
            const size_t end = owner.offset + owner.size;
            for (size_t position = owner.offset; position + 4 <= end; ++position) {
                Reader r(data, dataSize);
                uint32_t raw = 0;
                if (!r.Seek(position) || !r.ReadU32(raw)) continue;
                const int32_t index = static_cast<int32_t>(raw);
                if (index >= 0 && static_cast<size_t>(index) < file.blocks.size() &&
                    file.blocks[static_cast<size_t>(index)].type == targetType) {
                    if (matchedOffset) *matchedOffset = position;
                    return index;
                }
            }
            return -1;
        }

        bool ParseReferencedTriShapeData(
            const uint8_t* data,
            size_t dataSize,
            const NifBlockInfo& shape,
            NifFile& file,
            NifMesh& mesh)
        {
            if (!data || shape.offset > dataSize || shape.size > dataSize - shape.offset) return false;

            // NiTriShape/NiTriStrips store geometry in a separate referenced
            // data block. Find only typed block references, then validate the
            // candidate by fully decoding its bounded payload.
            for (const auto type : { std::string_view("NiTriShapeData"), std::string_view("NiTriStripsData") }) {
                size_t searchFrom = shape.offset;
                const size_t shapeEnd = shape.offset + shape.size;
                while (searchFrom < shapeEnd) {
                    NifBlockInfo remaining = shape;
                    remaining.offset = searchFrom;
                    // The validated remainder cannot exceed shape.size, which
                    // is already represented as uint32_t in NifBlockInfo.
                    remaining.size = static_cast<uint32_t>(shapeEnd - searchFrom);
                    size_t referenceOffset = searchFrom;
                    const int32_t dataIndex = FindReferencedBlock(data, dataSize, remaining, file, type, &referenceOffset);
                    if (dataIndex < 0) break;
                    const auto& dataBlock = file.blocks[static_cast<size_t>(dataIndex)];
                    if (ParseTriShapeData(data, dataSize, dataBlock, file, mesh)) {
                        return true;
                    }
                    searchFrom = referenceOffset + 1;
                }
            }
            return false;
        }

        bool DecodeSkyrimDiffuseTexture(
            const uint8_t* data,
            size_t dataSize,
            NifFile& file,
            NifMesh& mesh)
        {
            const int32_t shaderIndex = mesh.shaderPropertyBlock;
            if (!data || shaderIndex < 0 || static_cast<size_t>(shaderIndex) >= file.blocks.size()) return false;
            const auto& shader = file.blocks[static_cast<size_t>(shaderIndex)];
            if (shader.type != "BSLightingShaderProperty" || shader.offset > dataSize || shader.size > dataSize - shader.offset) return false;
            mesh.shaderType = shader.type;

            Reader shaderReader(data, dataSize);
            if (!shaderReader.Seek(shader.offset)) return false;
            // SSE BSLightingShaderProperty stores its shader-type enum before
            // the inherited NiObjectNET name field (NIF 20.2.0.7).
            if (file.version == 0x14020007 && !shaderReader.Skip(4)) return false;
            uint32_t nameIndex = 0, extraCount = 0;
            if (!shaderReader.ReadU32(nameIndex) || !shaderReader.ReadU32(extraCount) || extraCount > 65536) return false;
            const uint64_t inheritedBytes = static_cast<uint64_t>(extraCount) * 4 + 4; // extras plus controller ref
            if (inheritedBytes > shader.size || !shaderReader.Skip(static_cast<size_t>(inheritedBytes))) return false;
            // Skyrim's two shader flag words, UV offset/scale, then texture ref.
            if (!shaderReader.Skip(24) || shaderReader.Position() > shader.offset + shader.size || 4 > shader.offset + shader.size - shaderReader.Position()) return false;
            uint32_t rawTextureSet = 0;
            if (!shaderReader.ReadU32(rawTextureSet) || shaderReader.Position() > shader.offset + shader.size) return false;
            const int32_t textureSetIndex = static_cast<int32_t>(rawTextureSet);
            if (textureSetIndex < 0 || static_cast<size_t>(textureSetIndex) >= file.blocks.size()) return false;

            const auto& textureSet = file.blocks[static_cast<size_t>(textureSetIndex)];
            if (textureSet.type != "BSShaderTextureSet" || textureSet.offset > dataSize || textureSet.size > dataSize - textureSet.offset) return false;
            Reader textureReader(data, dataSize);
            if (!textureReader.Seek(textureSet.offset)) return false;
            uint32_t textureCount = 0;
            if (!textureReader.ReadU32(textureCount) || textureCount > 64) return false;
            std::vector<std::string> paths(textureCount);
            for (auto& path : paths) {
                if (!textureReader.ReadString32(path) || textureReader.Position() > textureSet.offset + textureSet.size) return false;
            }

            mesh.textureSetBlock = textureSetIndex;
            if (!paths.empty()) mesh.diffuseTexture = paths[0];
            if (paths.size() > 1) mesh.normalTexture = paths[1];
            if (paths.size() > 2) mesh.glowTexture = paths[2];
            if (!mesh.diffuseTexture.empty() && std::find(file.textureFiles.begin(), file.textureFiles.end(), mesh.diffuseTexture) == file.textureFiles.end()) {
                file.textureFiles.push_back(mesh.diffuseTexture);
            }
            SKSE::log::info("SkyLab material '{}' diffuse texture: '{}'", mesh.name, mesh.diffuseTexture);
            return !mesh.diffuseTexture.empty();
        }

        bool ReadHeader(
            Reader& r,
            NifFile& file)
        {
            uint32_t version = 0;

            if (!r.ReadU32(version)) {
                file.error = "File too small to contain NIF version";
                return false;
            }

            file.version = version;
            file.versionString = VersionToString(version);

            /*
             * NIF 20.0.0.3 and newer contain an endian byte immediately
             * after the version.
             */
            if (version >= 0x14000003u) {
                if (!r.ReadU8(file.endian)) {
                    file.error = "Missing NIF endian byte";
                    return false;
                }

                /*
                 * Skyrim NIFs are little endian.
                 */
                if (file.endian != 1) {
                    SKSE::log::warn(
                        "SkyLab: NIF endian byte is {}, expected 1",
                        file.endian);
                }
            }

            /*
             * UserVersion appears for NIF 10.0.1.8 and newer.
             */
            if (version >= 0x0A000108u) {
                if (!r.ReadU32(file.userVersion)) {
                    file.error = "Missing NIF UserVersion";
                    return false;
                }
            }

            if (!r.ReadU32(file.numBlocks)) {
                file.error = "Missing NIF block count";
                return false;
            }

            /*
             * Bethesda's serialized header differs from the generic NIF
             * layout here.
             *
             * Skyrim SE 20.2.0.7 is Bethesda format and contains:
             *
             *   StreamVersion
             *   Creator
             *   ExportInfo1
             *   ExportInfo2
             *
             * The strings use an 8-bit length prefix.
             */
            const bool bethesda = IsBethesdaVersion(version);

            if (bethesda) {
                file.isSkyrimSE = true;

                if (!r.ReadU32(file.bsVersion)) {
                    file.error = "Missing Bethesda stream version";
                    return false;
                }

                if (!r.ReadBethesdaString(file.creator)) {
                    file.error = "Missing Bethesda creator string";
                    return false;
                }

                if (!r.ReadBethesdaString(file.exportScript)) {
                    file.error = "Missing Bethesda export information";
                    return false;
                }

                if (!r.ReadBethesdaString(file.processScript)) {
                    file.error = "Missing Bethesda process information";
                    return false;
                }

                /*
                 * Some Bethesda stream versions contain an additional
                 * value after the creator/export fields.
                 *
                 * Skyrim SE uses stream version 100, so this is NOT read
                 * for normal Skyrim SE files.
                 */
                if (file.bsVersion > 130) {
                    uint32_t unused = 0;

                    if (!r.ReadU32(unused)) {
                        file.error =
                            "Missing Bethesda extended stream field";
                        return false;
                    }
                }

                /*
                 * Stream version 130 has one additional export field.
                 */
                if (file.bsVersion == 130) {
                    std::string unused;

                    if (!r.ReadBethesdaString(unused)) {
                        file.error =
                            "Missing Bethesda stream-130 export field";
                        return false;
                    }
                }
            }

            uint16_t blockTypeCount = 0;

            if (!r.ReadU16(blockTypeCount)) {
                file.error = "Missing NIF block type count";
                return false;
            }

            if (blockTypeCount == 0) {
                file.error = "NIF contains zero block types";
                return false;
            }

            /*
             * Prevent corrupted files from attempting ridiculous
             * allocations.
             */
            if (blockTypeCount > 65535) {
                file.error = "Invalid NIF block type count";
                return false;
            }

            file.blockTypes.clear();
            file.blockTypes.reserve(blockTypeCount);

            for (uint16_t i = 0; i < blockTypeCount; ++i) {
                std::string type;

                if (!r.ReadString32(type)) {
                    file.error =
                        std::format(
                            "Failed reading NIF block type {}",
                            i);
                    return false;
                }

                if (type.empty()) {
                    file.error =
                        std::format(
                            "NIF block type {} is empty",
                            i);
                    return false;
                }

                file.blockTypes.push_back(std::move(type));
            }

            /*
             * Block type indices.
             */
            file.blocks.clear();
            file.blocks.resize(file.numBlocks);

            for (uint32_t i = 0; i < file.numBlocks; ++i) {
                uint16_t typeIndex = 0;

                if (!r.ReadU16(typeIndex)) {
                    file.error =
                        std::format(
                            "Missing block type index at block {}",
                            i);
                    return false;
                }

                if (!IsReasonableBlockTypeIndex(
                        typeIndex,
                        file.blockTypes.size())) {

                    file.error =
                        std::format(
                            "Invalid block type index {} at block {} "
                            "(block type count {})",
                            typeIndex,
                            i,
                            file.blockTypes.size());

                    return false;
                }

                file.blocks[i].index = i;
                file.blocks[i].typeIndex = typeIndex;
                file.blocks[i].type = file.blockTypes[typeIndex];
            }

            /*
             * NIF >= 20.2.0.5 contains a block-size table.
             */
            if (version >= kBlockSizeVersion) {
                for (uint32_t i = 0; i < file.numBlocks; ++i) {
                    uint32_t size = 0;

                    if (!r.ReadU32(size)) {
                        file.error =
                            std::format(
                                "Missing block size at block {}",
                                i);
                        return false;
                    }

                    file.blocks[i].size = size;
                }
            }

            /*
             * Skyrim SE 20.2.0.7 has a global string table.
             */
            if (version >= kStringTableVersion) {
                uint32_t stringCount = 0;
                uint32_t maxStringLength = 0;

                if (!r.ReadU32(stringCount)) {
                    file.error = "Missing NIF string count";
                    return false;
                }

                if (!r.ReadU32(maxStringLength)) {
                    file.error = "Missing NIF maximum string length";
                    return false;
                }

                /*
                 * Sanity protection against malformed files.
                 */
                if (stringCount > 10'000'000) {
                    file.error =
                        std::format(
                            "Unreasonable NIF string count {}",
                            stringCount);
                    return false;
                }

                if (maxStringLength > 100'000'000) {
                    file.error =
                        std::format(
                            "Unreasonable NIF maximum string length {}",
                            maxStringLength);
                    return false;
                }

                file.strings.clear();
                file.strings.reserve(stringCount);

                for (uint32_t i = 0; i < stringCount; ++i) {
                    std::string value;

                    if (!r.ReadString32(value)) {
                        file.error =
                            std::format(
                                "Failed reading NIF string {}",
                                i);
                        return false;
                    }

                    file.strings.push_back(std::move(value));
                }
            }

            /*
             * NIF group information.
             *
             * The serialized form stores:
             *
             *   uint32 groupCount
             *   uint32 groupSize[groupCount]
             */
            uint32_t groupCount = 0;

            if (!r.ReadU32(groupCount)) {
                file.error = "Missing NIF group count";
                return false;
            }

            if (groupCount > 4096) {
                file.error =
                    std::format(
                        "Unreasonable NIF group count {}",
                        groupCount);
                return false;
            }

            for (uint32_t i = 0; i < groupCount; ++i) {
                uint32_t groupSize = 0;

                if (!r.ReadU32(groupSize)) {
                    file.error =
                        std::format(
                            "Missing NIF group size {}",
                            i);
                    return false;
                }
            }

            /*
             * At this point we are exactly at the beginning of block
             * payload data.
             */
            const size_t payloadStart = r.Position();

            size_t payloadOffset = payloadStart;

            for (uint32_t i = 0; i < file.numBlocks; ++i) {
                file.blocks[i].offset = payloadOffset;

                const uint32_t size = file.blocks[i].size;

                if (size > r.Remaining()) {
                    if (version >= kBlockSizeVersion) {
                        file.error =
                            std::format(
                                "Block {} extends beyond end of NIF "
                                "(offset {}, size {}, remaining {})",
                                i,
                                payloadOffset,
                                size,
                                r.Remaining());
                        return false;
                    }

                    break;
                }

                payloadOffset += size;

                if (!r.Skip(size)) {
                    file.error =
                        std::format(
                            "Failed skipping block {}",
                            i);
                    return false;
                }
            }

            return true;
        }

        void BuildInventory(
            const uint8_t* data,
            size_t dataSize,
            NifFile& file)
        {
            /*
             * The parser intentionally keeps block decoding separate from
             * header/inventory parsing.
             *
             * This makes it possible to safely validate the Skyrim NIF
             * header before adding individual Bethesda block decoders.
             */
            file.nodes.clear();
            file.meshes.clear();
            file.collisions.clear();

            for (const auto& block : file.blocks) {
                if (block.type == "NiNode" ||
                    block.type == "BSFadeNode" ||
                    block.type == "BSMultiBoundNode" ||
                    block.type == "BSGeometry") {

                    NifNode node;
                    node.name = block.type;
                    node.blockType = block.type;
                    node.blockIndex = block.index;

                    if (block.type == "NiNode" ||
                        block.type == "BSFadeNode") {
                        node.isCollision = false;
                    }

                    file.nodes.push_back(std::move(node));
                }

                if (block.type == "BSTriShape" ||
                    block.type == "BSDynamicTriShape" ||
                    block.type == "BSSubIndexTriShape" ||
                    block.type == "BSLODTriShape" ||
                    block.type == "BSSegmentedTriShape" ||
                    block.type == "NiTriShape" ||
                    block.type == "NiTriStrips") {

                    NifMesh mesh;
                    mesh.name = block.type;
                    mesh.blockType = block.type;
                    mesh.blockIndex = block.index;
                    if (block.type == "BSTriShape" || block.type == "BSDynamicTriShape" || block.type == "BSSubIndexTriShape") {
                        if (!ParseBSTriShape(data, dataSize, block, file, mesh)) {
                            SKSE::log::warn("SkyLab: could not decode mesh geometry in block {} ({})", block.index, block.type);
                            mesh.vertices.clear();
                            mesh.indices.clear();
                        }
                    } else {
                        Reader shapeReader(data, dataSize);
                        uint32_t nameIndex = 0;
                        if (shapeReader.Seek(block.offset) && shapeReader.ReadU32(nameIndex) && nameIndex < file.strings.size()) {
                            mesh.name = file.strings[nameIndex];
                        }
                        mesh.shaderPropertyBlock = FindReferencedBlock(data, dataSize, block, file, "BSLightingShaderProperty");
                        if (!ParseReferencedTriShapeData(data, dataSize, block, file, mesh)) {
                            SKSE::log::warn("SkyLab: could not decode referenced triangle data for block {} ({})", block.index, block.type);
                            mesh.vertices.clear();
                            mesh.indices.clear();
                        }
                    }

                    if (!mesh.vertices.empty()) {
                        file.totalVerts += static_cast<uint32_t>(mesh.vertices.size());
                        file.totalTris += static_cast<uint32_t>(mesh.indices.size() / 3);
                        for (const auto& vertex : mesh.vertices) {
                            file.boundsMin.x = std::min(file.boundsMin.x, vertex.pos.x);
                            file.boundsMin.y = std::min(file.boundsMin.y, vertex.pos.y);
                            file.boundsMin.z = std::min(file.boundsMin.z, vertex.pos.z);
                            file.boundsMax.x = std::max(file.boundsMax.x, vertex.pos.x);
                            file.boundsMax.y = std::max(file.boundsMax.y, vertex.pos.y);
                            file.boundsMax.z = std::max(file.boundsMax.z, vertex.pos.z);
                        }
                    }
                    file.meshes.push_back(std::move(mesh));
                }
            }

            for (auto& mesh : file.meshes) {
                if (!DecodeSkyrimDiffuseTexture(data, dataSize, file, mesh) && mesh.shaderPropertyBlock >= 0) {
                    SKSE::log::debug("SkyLab: no diffuse texture decoded for mesh '{}' (shader block {})", mesh.name, mesh.shaderPropertyBlock);
                }
            }

            if (!file.nodes.empty()) {
                file.rootIndex = 0;
            }

            if (file.totalVerts == 0) {
                file.boundsMin = {};
                file.boundsMax = {};
            }
        }
    }

    NifFile NifParser::Parse(const fs::path& path)
    {
        NifFile file;
        std::vector<uint8_t> data;
        bool viaGameResourceStream = Utils::ReadGameResourceFile(path, data);
        if (!viaGameResourceStream && !Utils::ReadResourceFile(path, data, &viaGameResourceStream)) {
            file.error = std::format("Unable to read NIF from game resources: {}", path.string());
            SKSE::log::error("NIF parse failed: {}", file.error);
            return file;
        }
        SKSE::log::info("NIF resource source: {} ({}, {} bytes)",
            viaGameResourceStream ? "Skyrim resource stream (archive-aware)" : "loose file / active VFS",
            path.string(), data.size());

        file = ParseFromMemory(
            data.data(),
            data.size(),
            path.string());

        // The compact decoder intentionally covers common Skyrim blocks. Use
        // Nifly for NIF variants it cannot turn into renderable triangles,
        // including some FaceGen and material/shader variants.
        const auto lowerPath = Utils::ToLower(path.string());
        const bool isFaceGeometry = lowerPath.find("\\facegeom\\") != std::string::npos;
        const bool hasMissingDiffuse = std::any_of(file.meshes.begin(), file.meshes.end(),
            [](const NifMesh& mesh) { return mesh.diffuseTexture.empty(); });
        if (!file.isValid || file.totalVerts == 0 || file.meshes.empty() || isFaceGeometry || hasMissingDiffuse) {
            auto niflyFile = ParseWithNifly(data.data(), data.size(), path.string());
            if (niflyFile.isValid &&
                (isFaceGeometry || niflyFile.totalVerts > file.totalVerts ||
                 (niflyFile.totalVerts == file.totalVerts && niflyFile.meshes.size() >= file.meshes.size() &&
                  niflyFile.textureFiles.size() > file.textureFiles.size())))
                file = std::move(niflyFile);
        }

        if (file.isValid) {
            for (auto& mesh : file.meshes) {
                if (mesh.sourcePath.empty()) mesh.sourcePath = path.string();
            }
            SKSE::log::info(
                "NIF parsed successfully: {} "
                "(version {}, blocks {}, block types {}, vertices {}, triangles {})",
                path.string(),
                file.versionString,
                file.numBlocks,
                file.blockTypes.size(),
                file.totalVerts,
                file.totalTris);
        } else {
            SKSE::log::error(
                "NIF parse failed: {}",
                file.error);
        }

        return file;
    }

    NifFile NifParser::ParseWithNifly(const uint8_t* data, size_t size, const std::string& label)
    {
        NifFile result;
        if (!data || size == 0) {
            result.error = "Nifly received an empty NIF buffer";
            return result;
        }

        try {
            const std::string blob(reinterpret_cast<const char*>(data), size);
            std::istringstream stream(blob, std::ios::in | std::ios::binary);
            nifly::NifFile source;
            if (source.Load(stream) != 0 || !source.IsValid()) {
                result.error = "Nifly could not decode this NIF";
                return result;
            }

            result.isValid = true;
            result.isSkyrimSE = source.GetHeader().GetVersion().IsSSE();
            result.version = static_cast<uint32_t>(source.GetHeader().GetVersion().File());
            result.versionString = source.GetHeader().GetVersion().String();
            result.userVersion = source.GetHeader().GetVersion().User();
            result.bsVersion = source.GetHeader().GetVersion().Stream();
            result.nodes.push_back(NifNode{});
            result.rootIndex = 0;
            result.nodes.front().name = std::filesystem::path(label).stem().string();

            auto includeTexture = [&](const std::string& path) {
                if (!path.empty() && std::find(result.textureFiles.begin(), result.textureFiles.end(), path) == result.textureFiles.end())
                    result.textureFiles.push_back(path);
            };
            for (auto* shape : source.GetShapes()) {
                if (!shape) continue;
                std::vector<nifly::Vector3> positionStorage;
                source.GetVertsForShape(shape, positionStorage);
                const auto* positions = &positionStorage;
                if (positions->empty()) continue;
                std::vector<nifly::Triangle> sourceTriangles;
                if (!shape->GetTriangles(sourceTriangles) || sourceTriangles.empty()) continue;
                const auto* sourceUvs = source.GetUvsForShape(shape);
                const auto* sourceNormals = source.GetNormalsForShape(shape);

                auto transform = shape->GetTransformToParent();
                for (auto* parent = source.GetParentNode(shape); parent; parent = source.GetParentNode(parent))
                    transform = parent->GetTransformToParent().ComposeTransforms(transform);

                NifMesh mesh;
                mesh.name = shape->name.get().empty() ? "NiflyMesh" : shape->name.get();
                mesh.blockType = shape->GetBlockName();
                mesh.blockIndex = static_cast<uint32_t>(result.meshes.size());
                mesh.vertices.reserve(positions->size());
                for (std::size_t i = 0; i < positions->size(); ++i) {
                    const auto position = transform.ApplyTransform((*positions)[i]);
                    NifVertex vertex{};
                    vertex.pos = { position.x, position.y, position.z };
                    if (sourceUvs && i < sourceUvs->size()) {
                        vertex.uv = { (*sourceUvs)[i].u, (*sourceUvs)[i].v };
                        mesh.hasUV = true;
                    }
                    if (sourceNormals && i < sourceNormals->size()) {
                        auto normal = transform.ApplyTransformToDir((*sourceNormals)[i]);
                        normal.Normalize();
                        vertex.normal = { normal.x, normal.y, normal.z };
                        mesh.hasNormals = true;
                    }
                    mesh.vertices.push_back(vertex);
                }
                mesh.indices.reserve(sourceTriangles.size() * 3);
                for (const auto& triangle : sourceTriangles) {
                    if (triangle.p1 >= mesh.vertices.size() || triangle.p2 >= mesh.vertices.size() || triangle.p3 >= mesh.vertices.size()) continue;
                    mesh.indices.insert(mesh.indices.end(), { triangle.p1, triangle.p2, triangle.p3 });
                }
                if (mesh.indices.empty()) continue;
                mesh.hasTangents = shape->HasTangents();
                mesh.skinned = shape->IsSkinned();
                source.GetTextureSlot(shape, mesh.diffuseTexture, 0);
                source.GetTextureSlot(shape, mesh.normalTexture, 1);
                source.GetTextureSlot(shape, mesh.glowTexture, 2);
                source.GetTextureSlot(shape, mesh.specularTexture, 8);
                includeTexture(mesh.diffuseTexture);
                includeTexture(mesh.normalTexture);
                includeTexture(mesh.glowTexture);
                includeTexture(mesh.specularTexture);

                const auto meshIndex = static_cast<int32_t>(result.meshes.size());
                result.nodes.front().meshIndices.push_back(meshIndex);
                for (const auto& vertex : mesh.vertices) {
                    result.boundsMin.x = std::min(result.boundsMin.x, vertex.pos.x);
                    result.boundsMin.y = std::min(result.boundsMin.y, vertex.pos.y);
                    result.boundsMin.z = std::min(result.boundsMin.z, vertex.pos.z);
                    result.boundsMax.x = std::max(result.boundsMax.x, vertex.pos.x);
                    result.boundsMax.y = std::max(result.boundsMax.y, vertex.pos.y);
                    result.boundsMax.z = std::max(result.boundsMax.z, vertex.pos.z);
                }
                result.totalVerts += static_cast<uint32_t>(mesh.vertices.size());
                result.totalTris += static_cast<uint32_t>(mesh.indices.size() / 3);
                result.meshes.push_back(std::move(mesh));
            }
            if (result.meshes.empty()) {
                result.isValid = false;
                result.error = std::format("Nifly decoded no triangle meshes from '{}'", label);
            }
        } catch (const std::exception& exception) {
            result.isValid = false;
            result.error = std::format("Nifly decode failed for '{}': {}", label, exception.what());
        }
        return result;
    }

    NifFile NifParser::ParseFromMemory(
        const uint8_t* data,
        size_t size,
        const std::string& label)
    {
        NifFile file;

        if (!data || size < 4) {
            file.error = "NIF buffer is empty or too small";
            return file;
        }

        Reader reader(data, size);

        /*
         * NIF files begin with a text header before the binary version.
         *
         * Typical Skyrim:
         *
         *   Gamebryo File Format, Version 20.2.0.7
         *   [binary data]
         *
         * Find the newline rather than assuming a fixed header size.
         */
        size_t newline = std::string::npos;

        for (size_t i = 0; i < size && i < 4096; ++i) {
            if (data[i] == '\n') {
                newline = i;
                break;
            }
        }

        if (newline == std::string::npos) {
            file.error = "Missing NIF text header";
            return file;
        }

        file.headerString.assign(
            reinterpret_cast<const char*>(data),
            newline);

        while (!file.headerString.empty() &&
               (file.headerString.back() == '\r' ||
                file.headerString.back() == '\n')) {
            file.headerString.pop_back();
        }

        /*
         * The binary version immediately follows the text-header line.
         */
        if (!reader.Seek(newline + 1)) {
            file.error = "Invalid NIF header offset";
            return file;
        }

        if (!ReadHeader(reader, file)) {
            SKSE::log::error(
                "NIF '{}' header parse failed at offset {}: {}",
                label,
                reader.Position(),
                file.error);

            return file;
        }

        // Decode Skyrim inline triangle meshes from the original buffer.
        BuildInventory(data, size, file);

        file.isValid = true;
        if (!label.empty() && label != "memory") {
            for (auto& mesh : file.meshes) {
                if (mesh.sourcePath.empty()) mesh.sourcePath = label;
            }
        }

        return file;
    }

    NifFile NifParser::CaptureScene(RE::NiAVObject* root, const std::string& label)
    {
        std::vector<RE::NiAVObject*> roots;
        if (root) roots.push_back(root);
        return CaptureScene(roots, label);
    }

    NifFile NifParser::CaptureScene(const std::vector<RE::NiAVObject*>& roots, const std::string& label)
    {
        NifFile file;
        file.headerString = "Skyrim runtime scene snapshot";
        file.versionString = "Loaded by Skyrim";
        file.isSkyrimSE = true;

        auto firstRoot = std::find_if(roots.begin(), roots.end(), [](const RE::NiAVObject* candidate) { return candidate != nullptr; });
        if (firstRoot == roots.end()) {
            file.error = "The reference has no loaded 3D scene";
            return file;
        }
        RE::NiAVObject* root = *firstRoot;

        NifNode rootNode;
        rootNode.name = label.empty() ? "Loaded reference" : label;
        rootNode.blockType = "SceneSnapshot";
        file.nodes.push_back(std::move(rootNode));
        file.rootIndex = 0;

        const RE::NiTransform rootInverse = root->world.Invert();
        std::unordered_set<RE::NiAVObject*> visited;
        std::vector<RE::NiAVObject*> pending;
        pending.reserve(roots.size());
        for (auto* candidate : roots) {
            if (candidate) pending.push_back(candidate);
        }

        auto updateBounds = [&](const NifVec3& position) {
            file.boundsMin.x = std::min(file.boundsMin.x, position.x);
            file.boundsMin.y = std::min(file.boundsMin.y, position.y);
            file.boundsMin.z = std::min(file.boundsMin.z, position.z);
            file.boundsMax.x = std::max(file.boundsMax.x, position.x);
            file.boundsMax.y = std::max(file.boundsMax.y, position.y);
            file.boundsMax.z = std::max(file.boundsMax.z, position.z);
        };

        while (!pending.empty() && visited.size() < 100000) {
            RE::NiAVObject* object = pending.back();
            pending.pop_back();
            if (!object || !visited.insert(object).second) continue;

            if (auto* node = object->AsNode()) {
                // capacity() can include uninitialized slots; only traverse
                // the live child count or bogus pointers can hide valid
                // reference geometry (and potentially crash the editor).
                for (std::uint16_t i = 0; i < node->children.size(); ++i) {
                    if (auto* child = node->children[i].get()) pending.push_back(child);
                }
            }

            auto* geometry = object->AsGeometry();
            auto* shape = geometry ? geometry->AsTriShape() : nullptr;
            if (!geometry || !shape) continue;

            const auto& runtime = geometry->GetGeometryRuntimeData();
            struct GeometrySlice {
                RE::BSGraphics::TriShape* buffers{};
                RE::BSGraphics::VertexDesc vertexDesc{};
                std::uint32_t vertexCount{};
                std::uint32_t triangleCount{};
            };
            std::vector<GeometrySlice> slices;
            auto* primaryRendererData = runtime.rendererData;
            auto primaryVertexDesc = runtime.vertexDesc;
            const auto counts = shape->GetTrishapeRuntimeData();
            const auto primaryStride = primaryVertexDesc.GetSize();
            const bool primaryReady = primaryRendererData && primaryRendererData->rawVertexData && primaryRendererData->rawIndexData &&
                primaryVertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) &&
                counts.vertexCount > 0 && counts.triangleCount > 0 && primaryStride >= 12 && primaryStride <= 256;
            if (primaryReady) {
                slices.push_back({ primaryRendererData, primaryVertexDesc, counts.vertexCount, counts.triangleCount });
            } else if (runtime.skinInstance && runtime.skinInstance->skinPartition) {
                // Skinned actor bodies often keep their usable CPU buffers on
                // skin partitions instead of BSGeometry::rendererData. Read
                // each populated partition so body/clothing parts are included
                // in the preview just like attached weapons.
                auto* skinPartition = runtime.skinInstance->skinPartition.get();
                const auto partitionCount = std::min<std::size_t>(skinPartition->numPartitions, skinPartition->partitions.size());
                for (std::size_t partitionIndex = 0; partitionIndex < partitionCount; ++partitionIndex) {
                    const auto& partition = skinPartition->partitions[partitionIndex];
                    auto* partitionBuffers = partition.buffData;
                    auto partitionVertexDesc = partition.vertexDesc;
                    const auto partitionStride = partitionVertexDesc.GetSize();
                    if (!partitionBuffers || !partitionBuffers->rawVertexData || !partitionBuffers->rawIndexData ||
                        !partitionVertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX) ||
                        partition.vertices == 0 || partition.triangles == 0 || partitionStride < 12 || partitionStride > 256) {
                        continue;
                    }
                    slices.push_back({ partitionBuffers, partitionVertexDesc, partition.vertices, partition.triangles });
                }
            }

            for (const auto& slice : slices) {
            auto* rendererData = slice.buffers;
            auto vertexDesc = slice.vertexDesc;
            const std::uint32_t vertexCount = slice.vertexCount;
            const std::uint32_t triangleCount = slice.triangleCount;
            const std::uint32_t stride = vertexDesc.GetSize();
            NifMesh mesh;
            mesh.name = object->name.data() && *object->name.data() ? object->name.data() : "Mesh";
            mesh.blockType = object->GetRTTI() ? object->GetRTTI()->GetName() : "BSGeometry";
            mesh.blockIndex = static_cast<std::uint32_t>(file.meshes.size());
            mesh.shaderType = "Skyrim runtime material";
            mesh.skinned = vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_SKINNED);
            mesh.hasUV = vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_UV);
            mesh.hasNormals = vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_NORMAL);
            mesh.hasTangents = vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_TANGENT);
            mesh.hasVertexColors = vertexDesc.HasFlag(RE::BSGraphics::Vertex::VF_COLORS);

            const std::uint32_t uvOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
            const std::uint32_t normalOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_NORMAL);
            const std::uint32_t colorOffset = vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_COLOR);
            if ((mesh.hasUV && uvOffset + 4 > stride) ||
                (mesh.hasNormals && normalOffset + 4 > stride) ||
                (mesh.hasVertexColors && colorOffset + 4 > stride)) {
                SKSE::log::warn("SkyLab: runtime vertex descriptor is out of bounds for '{}'", mesh.name);
                continue;
            }

            mesh.vertices.reserve(vertexCount);
            const RE::NiTransform localToPreview = rootInverse * object->world;
            bool validVertices = true;
            for (std::uint32_t i = 0; i < vertexCount; ++i) {
                const std::uint8_t* record = rendererData->rawVertexData + static_cast<std::size_t>(i) * stride;
                float x = 0.0f, y = 0.0f, z = 0.0f;
                std::memcpy(&x, record, sizeof(float));
                std::memcpy(&y, record + sizeof(float), sizeof(float));
                std::memcpy(&z, record + sizeof(float) * 2, sizeof(float));
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    validVertices = false;
                    break;
                }

                const RE::NiPoint3 transformed = localToPreview * RE::NiPoint3(x, y, z);
                NifVertex vertex{};
                vertex.pos = { transformed.x, transformed.y, transformed.z };

                if (mesh.hasUV) {
                    Reader uvReader(record + uvOffset, stride - uvOffset);
                    if (!ReadHalf(uvReader, vertex.uv.u) || !ReadHalf(uvReader, vertex.uv.v)) {
                        validVertices = false;
                        break;
                    }
                }
                if (mesh.hasNormals) {
                    const auto* packed = record + normalOffset;
                    const RE::NiPoint3 localNormal{
                        (packed[0] / 255.0f) * 2.0f - 1.0f,
                        (packed[1] / 255.0f) * 2.0f - 1.0f,
                        (packed[2] / 255.0f) * 2.0f - 1.0f
                    };
                    RE::NiPoint3 previewNormal = localToPreview.rotate * localNormal;
                    const float normalLength = previewNormal.Length();
                    if (normalLength > 0.0001f) previewNormal = previewNormal / normalLength;
                    vertex.normal = { previewNormal.x, previewNormal.y, previewNormal.z };
                }
                if (mesh.hasVertexColors) {
                    const auto* color = record + colorOffset;
                    vertex.color = DirectX::XMFLOAT4(
                        color[0] / 255.0f, color[1] / 255.0f,
                        color[2] / 255.0f, color[3] / 255.0f);
                }
                mesh.vertices.push_back(vertex);
            }
            if (!validVertices || mesh.vertices.size() != vertexCount) continue;

            mesh.indices.reserve(static_cast<std::size_t>(triangleCount) * 3);
            bool validIndices = true;
            for (std::uint32_t i = 0; i < triangleCount * 3; ++i) {
                const std::uint16_t index = rendererData->rawIndexData[i];
                if (index >= vertexCount) {
                    validIndices = false;
                    break;
                }
                mesh.indices.push_back(index);
            }
            if (!validIndices || mesh.indices.empty()) continue;

            if (auto* shader = geometry->lightingShaderProp_cast()) {
                auto normalizeTexturePath = [](std::string path) {
                    std::replace(path.begin(), path.end(), '/', '\\');
                    std::string lowerPath = Utils::ToLower(path);
                    std::string dataRoot = Utils::GetDataPath();
                    std::replace(dataRoot.begin(), dataRoot.end(), '/', '\\');
                    const std::string lowerDataRoot = Utils::ToLower(dataRoot);
                    if (!lowerDataRoot.empty() && lowerPath.rfind(lowerDataRoot, 0) == 0) {
                        path.erase(0, dataRoot.size());
                    } else if (lowerPath.rfind("data\\", 0) == 0) {
                        path.erase(0, 5);
                    }
                    while (!path.empty() && path.front() == '\\') path.erase(path.begin());
                    return path;
                };

                auto* material = dynamic_cast<RE::BSLightingShaderMaterialBase*>(shader->GetBaseMaterial());
                auto texturePath = [&](RE::BSTextureSet::Texture slot) {
                    if (material) {
                        auto textureSet = material->GetTextureSet();
                        if (textureSet) {
                            const char* path = textureSet->GetTexturePath(slot);
                            if (path && *path) return normalizeTexturePath(path);
                        }
                    }
                    return std::string{};
                };

                mesh.diffuseTexture = texturePath(static_cast<RE::BSTextureSet::Texture>(0));
                mesh.normalTexture = texturePath(static_cast<RE::BSTextureSet::Texture>(1));
                mesh.glowTexture = texturePath(static_cast<RE::BSTextureSet::Texture>(3));
                mesh.specularTexture = texturePath(static_cast<RE::BSTextureSet::Texture>(7));

                if (mesh.diffuseTexture.empty()) {
                    if (auto* texture = shader->GetBaseTexture(); texture && texture->resourceStream) {
                        RE::BSFixedString textureName;
                        if (texture->resourceStream->DoGetName(textureName) && textureName.data() && *textureName.data()) {
                            mesh.diffuseTexture = normalizeTexturePath(textureName.data());
                        }
                    }
                }

                for (const auto* path : { &mesh.diffuseTexture, &mesh.normalTexture, &mesh.glowTexture, &mesh.specularTexture }) {
                    if (!path->empty() && std::find(file.textureFiles.begin(), file.textureFiles.end(), *path) == file.textureFiles.end()) {
                        file.textureFiles.push_back(*path);
                    }
                }
            }

            NifNode meshNode;
            meshNode.name = mesh.name;
            meshNode.blockType = mesh.blockType;
            meshNode.blockIndex = mesh.blockIndex;
            meshNode.parent = 0;
            meshNode.meshIndices.push_back(static_cast<std::int32_t>(file.meshes.size()));
            file.nodes.front().children.push_back(static_cast<std::int32_t>(file.nodes.size()));
            file.nodes.push_back(std::move(meshNode));

            for (const auto& vertex : mesh.vertices) updateBounds(vertex.pos);
            file.totalVerts += static_cast<std::uint32_t>(mesh.vertices.size());
            file.totalTris += static_cast<std::uint32_t>(mesh.indices.size() / 3);
            file.meshes.push_back(std::move(mesh));
            }
        }

        if (!file.meshes.empty()) {
            file.isValid = true;
            file.numBlocks = static_cast<std::uint32_t>(file.meshes.size());
            SKSE::log::info("SkyLab: captured runtime scene '{}': {} meshes, {} vertices, {} triangles",
                label, file.meshes.size(), file.totalVerts, file.totalTris);
        } else {
            file.error = "No runtime triangle geometry was available for this reference";
        }
        return file;
    }

    NifFile NifParser::ParseGameResource(const fs::path& path)
    {
        NifFile file;
        fs::path resourcePath = path.lexically_normal();
        const fs::path dataRoot = fs::path(Utils::GetDataPath()).lexically_normal();
        if (resourcePath.is_absolute()) {
            resourcePath = resourcePath.lexically_relative(dataRoot);
            if (resourcePath.empty() || *resourcePath.begin() == "..") {
                file.error = "Engine NIF fallback only loads resources under Skyrim Data";
                return file;
            }
        }

        std::string resourceName = resourcePath.generic_string();
        std::replace(resourceName.begin(), resourceName.end(), '/', '\\');
        if (Utils::ToLower(resourceName).rfind("data\\", 0) == 0) resourceName.erase(0, 5);
        if (resourceName.empty()) {
            file.error = "Empty game resource path";
            return file;
        }

        RE::NiPointer<RE::NiNode> model;
        RE::BSModelDB::DBTraits::ArgsType args{};
        const auto result = RE::BSModelDB::Demand(resourceName.c_str(), model, args);
        if (result != RE::BSResource::ErrorCode::kNone || !model) {
            file.error = std::format("Skyrim could not load model resource '{}' (error {})",
                resourceName, static_cast<int>(result));
            return file;
        }

        auto scene = CaptureScene(model.get(), resourceName);
        for (auto& mesh : scene.meshes) {
            mesh.sourcePath = resourceName;
        }
        return scene;
    }

    std::string NifParser::Summarize(const NifFile& file)
    {
        if (!file.isValid) {
            return std::format(
                "Invalid NIF: {}",
                file.error);
        }

        return std::format(
            "NIF {}\n"
            "Version: {}\n"
            "UserVersion: {}\n"
            "Bethesda Stream: {}\n"
            "Blocks: {}\n"
            "Block Types: {}\n"
            "Strings: {}\n"
            "Nodes: {}\n"
            "Meshes: {}\n"
            "Vertices: {}\n"
            "Triangles: {}\n"
            "Collisions: {}\n",
            file.headerString,
            file.versionString,
            file.userVersion,
            file.bsVersion,
            file.numBlocks,
            file.blockTypes.size(),
            file.strings.size(),
            file.nodes.size(),
            file.meshes.size(),
            file.totalVerts,
            file.totalTris,
            file.collisions.size());
    }
}
