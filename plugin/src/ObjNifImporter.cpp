#include "PCH.h"
#include "ObjNifImporter.h"
#include "Utils.h"
#include <NifFile.hpp>
#include <charconv>
#include <cmath>
#include <compare>

namespace SkyLab::ObjNifImporter
{
    namespace
    {
        struct ObjIndex
        {
            int position{ -1 };
            int uv{ -1 };
            int normal{ -1 };
            auto operator<=>(const ObjIndex&) const = default;
        };

        bool ResolveIndex(std::string_view value, std::size_t count, int& result)
        {
            if (value.empty()) return false;
            int parsed{};
            const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (ec != std::errc{} || end != value.data() + value.size() || parsed == 0) return false;
            const auto resolved = parsed > 0 ? parsed - 1 : static_cast<int>(count) + parsed;
            if (resolved < 0 || static_cast<std::size_t>(resolved) >= count) return false;
            result = resolved;
            return true;
        }

        bool ParseFaceIndex(std::string_view token, std::size_t positions, std::size_t uvs,
                            std::size_t normals, ObjIndex& result)
        {
            const auto firstSlash = token.find('/');
            const auto secondSlash = firstSlash == std::string_view::npos
                ? std::string_view::npos : token.find('/', firstSlash + 1);
            const auto positionPart = token.substr(0, firstSlash);
            if (!ResolveIndex(positionPart, positions, result.position)) return false;
            if (firstSlash != std::string_view::npos) {
                const auto uvPart = token.substr(firstSlash + 1,
                    secondSlash == std::string_view::npos ? std::string_view::npos : secondSlash - firstSlash - 1);
                if (!uvPart.empty() && !ResolveIndex(uvPart, uvs, result.uv)) return false;
                if (secondSlash != std::string_view::npos) {
                    const auto normalPart = token.substr(secondSlash + 1);
                    if (!normalPart.empty() && !ResolveIndex(normalPart, normals, result.normal)) return false;
                }
            }
            return true;
        }
    }

    bool ImportAsSkyrimSENif(const fs::path& objPath, const fs::path& nifPath,
                             std::string diffuseTexture, std::string& error)
    {
        error.clear();
        if (Utils::ToLower(objPath.extension().string()) != ".obj") {
            error = "Select a Wavefront OBJ file.";
            return false;
        }
        if (Utils::ToLower(nifPath.extension().string()) != ".nif") {
            error = "The output filename must end in .nif.";
            return false;
        }
        std::ifstream input(objPath);
        if (!input) {
            error = "Could not open the OBJ file.";
            return false;
        }

        std::vector<nifly::Vector3> sourcePositions;
        std::vector<nifly::Vector2> sourceUvs;
        std::vector<nifly::Vector3> sourceNormals;
        std::vector<nifly::Vector3> vertices;
        std::vector<nifly::Vector2> uvs;
        std::vector<nifly::Vector3> normals;
        std::vector<bool> hasNormal;
        std::vector<nifly::Triangle> triangles;
        std::map<ObjIndex, uint32_t> vertexLookup;
        std::string line;
        std::size_t lineNumber = 0;

        auto mapIndex = [&](const ObjIndex& obj, uint32_t& index) {
            const auto found = vertexLookup.find(obj);
            if (found != vertexLookup.end()) {
                index = found->second;
                return true;
            }
            if (vertices.size() >= std::numeric_limits<uint16_t>::max()) return false;
            index = static_cast<uint32_t>(vertices.size());
            vertexLookup.emplace(obj, index);
            vertices.push_back(sourcePositions[obj.position]);
            uvs.push_back(obj.uv >= 0 ? sourceUvs[obj.uv] : nifly::Vector2(0.0f, 0.0f));
            normals.push_back(obj.normal >= 0 ? sourceNormals[obj.normal] : nifly::Vector3(0.0f, 0.0f, 0.0f));
            hasNormal.push_back(obj.normal >= 0);
            return true;
        };

        while (std::getline(input, line)) {
            ++lineNumber;
            std::istringstream row(line);
            std::string command;
            row >> command;
            if (command.empty() || command[0] == '#') continue;
            if (command == "v") {
                nifly::Vector3 value;
                if (!(row >> value.x >> value.y >> value.z)) {
                    error = std::format("Invalid vertex at OBJ line {}.", lineNumber);
                    return false;
                }
                sourcePositions.push_back(value);
            } else if (command == "vt") {
                nifly::Vector2 value;
                if (!(row >> value.u >> value.v)) {
                    error = std::format("Invalid texture coordinate at OBJ line {}.", lineNumber);
                    return false;
                }
                value.v = 1.0f - value.v;
                sourceUvs.push_back(value);
            } else if (command == "vn") {
                nifly::Vector3 value;
                if (!(row >> value.x >> value.y >> value.z)) {
                    error = std::format("Invalid normal at OBJ line {}.", lineNumber);
                    return false;
                }
                sourceNormals.push_back(value);
            } else if (command == "f") {
                std::vector<uint32_t> face;
                std::string token;
                while (row >> token) {
                    if (token.starts_with('#')) break;
                    ObjIndex obj;
                    if (!ParseFaceIndex(token, sourcePositions.size(), sourceUvs.size(), sourceNormals.size(), obj)) {
                        error = std::format("Invalid face index at OBJ line {}.", lineNumber);
                        return false;
                    }
                    uint32_t mapped{};
                    if (!mapIndex(obj, mapped)) {
                        error = "OBJ has too many distinct vertices for a Skyrim SE mesh (limit: 65,535).";
                        return false;
                    }
                    face.push_back(mapped);
                }
                if (face.size() < 3) {
                    error = std::format("Face has fewer than three vertices at OBJ line {}.", lineNumber);
                    return false;
                }
                for (std::size_t i = 1; i + 1 < face.size(); ++i)
                    triangles.emplace_back(static_cast<uint16_t>(face[0]), static_cast<uint16_t>(face[i]), static_cast<uint16_t>(face[i + 1]));
            }
        }

        if (vertices.empty() || triangles.empty()) {
            error = "OBJ contains no triangulated mesh data.";
            return false;
        }

        bool allNormalsPresent = std::all_of(hasNormal.begin(), hasNormal.end(), [](bool present) { return present; });
        if (!allNormalsPresent) {
            std::vector<nifly::Vector3> accumulated(vertices.size(), nifly::Vector3(0.0f, 0.0f, 0.0f));
            for (const auto& triangle : triangles) {
                const auto& a = vertices[triangle.p1];
                const auto& b = vertices[triangle.p2];
                const auto& c = vertices[triangle.p3];
                const auto edge1 = b - a;
                const auto edge2 = c - a;
                const auto faceNormal = edge1.cross(edge2);
                for (const auto index : { triangle.p1, triangle.p2, triangle.p3 }) {
                    if (!hasNormal[index]) accumulated[index] += faceNormal;
                }
            }
            for (std::size_t i = 0; i < normals.size(); ++i) {
                if (hasNormal[i]) continue;
                auto normal = accumulated[i];
                const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (length > 0.00001f) normal = normal / length;
                else normal = nifly::Vector3(0.0f, 0.0f, 1.0f);
                normals[i] = normal;
            }
        }

        const nifly::NiVersion version(nifly::V20_2_0_7, 12, 100);
        nifly::NifFile nif;
        nif.Create(version);
        auto* shape = nif.CreateShapeFromData(objPath.stem().string(), &vertices, &triangles,
            sourceUvs.empty() ? nullptr : &uvs, normals.empty() ? nullptr : &normals);
        if (!shape) {
            error = "Nifly could not create a Skyrim SE mesh from the OBJ data.";
            return false;
        }
        if (!diffuseTexture.empty()) {
            std::replace(diffuseTexture.begin(), diffuseTexture.end(), '/', '\\');
            nif.SetTextureSlot(shape, diffuseTexture, 0);
        }

        std::error_code ec;
        if (!nifPath.parent_path().empty()) fs::create_directories(nifPath.parent_path(), ec);
        if (ec) {
            error = std::format("Could not create the output folder: {}", ec.message());
            return false;
        }
        if (nif.Save(nifPath) != 0) {
            error = "Nifly failed to write the Skyrim SE NIF.";
            return false;
        }
        return true;
    }
}
