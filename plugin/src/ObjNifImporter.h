#pragma once

#include "PCH.h"

namespace SkyLab::ObjNifImporter
{
    bool ImportAsSkyrimSENif(const fs::path& objPath, const fs::path& nifPath,
                             std::string diffuseTexture, std::string& error);
}
