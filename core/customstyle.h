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

#ifndef CUSTOMSTYLE_H
#define CUSTOMSTYLE_H

#include <string>
#include <vector>
#include <glm/glm.hpp>

struct StyleVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float faceRefZ;
};

class CustomTrackStyle {
public:
    CustomTrackStyle();
    ~CustomTrackStyle();

    bool load(const std::string& filepath);

    std::vector<StyleVertex> vertices;
    std::vector<unsigned int> indices;

    float length;
    bool isValid;
};

#endif // CUSTOMSTYLE_H
