/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
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

#ifndef GLB_READER_H
#define GLB_READER_H

#include <vector>
#include <string>
#include <glm/glm.hpp>

struct GlbVertex {
    glm::vec3 pos;    // Location 0
    glm::vec3 normal; // Location 7
    glm::vec2 uv;     // Location 8
};

struct GlbTextureData {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

struct GlbPrimitiveData {
    std::vector<GlbVertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    bool hasTexture = false;
    GlbTextureData textureData;
};

struct GlbModelData {
    std::vector<GlbPrimitiveData> primitives;
    glm::vec3 minAABB = glm::vec3(3.40282347e+38F);
    glm::vec3 maxAABB = glm::vec3(-3.40282347e+38F);
};

bool readGlb(const std::string& fileName, GlbModelData& modelData);

#endif // GLB_READER_H
