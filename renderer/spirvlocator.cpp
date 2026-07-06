/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
#    Copyright (C) 2026 Ercan Akyürek <ercan.akyuerek@gmail.com>
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "spirvlocator.h"
#include "core/workingdirectory.h"

#include <cstdio>
#include <filesystem>

std::string locateSpirvShader(const std::string& shaderName) {
    const std::string fileName = shaderName + ".spv";
    std::error_code ignored;

#ifdef FVD_SPIRV_DIR
    std::filesystem::path buildTreePath = std::filesystem::path(FVD_SPIRV_DIR) / fileName;
    if (std::filesystem::exists(buildTreePath, ignored))
        return buildTreePath.string();
#endif

    std::string resources = platformResourcesDirectory();
    if (!resources.empty()) {
        std::filesystem::path bundlePath = std::filesystem::path(resources) / "shaders_vulkan" / fileName;
        if (std::filesystem::exists(bundlePath, ignored))
            return bundlePath.string();
    }

    std::filesystem::path workingDirectoryPath = std::filesystem::path("shaders_vulkan") / fileName;
    if (std::filesystem::exists(workingDirectoryPath, ignored))
        return workingDirectoryPath.string();

    fprintf(stderr, "[vulkan] shader %s not found in any location\n", fileName.c_str());
    return workingDirectoryPath.string();
}
