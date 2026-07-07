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

#pragma once

#include <glm/glm.hpp>
#include <cstdint>

struct SkyUniforms {
    glm::vec4 topLeft;
    glm::vec4 topRight;
    glm::vec4 bottomLeft;
    glm::vec4 bottomRight;
    glm::vec4 fallbackColor;
    int32_t hasTexture;
    int32_t padding0;
    int32_t padding1;
    int32_t padding2;
};

struct FloorUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::vec4 eyePos;
    glm::vec4 floorColor;
    glm::vec4 mistColor;
    float grdTexSize;
    float opacity;
    int32_t border;
    int32_t grid;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float padding;
};

struct TrackUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::vec4 eyePos;
    glm::vec4 lightDir;
    glm::vec4 defaultColor;
    glm::vec4 sectionColor;
    glm::vec4 transitionColor;
    glm::vec4 mistColor;
    int32_t colorMode;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
};

struct TrackInstancedUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::vec4 eyePos;
    glm::vec4 lightDir;
    glm::vec4 defaultColor;
    glm::vec4 sectionColor;
    glm::vec4 transitionColor;
    glm::vec4 mistColor;
    int32_t colorMode;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float trackLength;
    float heartline;
    int32_t isAsset;
    int32_t smoothAlongSpline;
    int32_t padding;
};

struct StlUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::vec4 eyePos;
    glm::vec4 lightDir;
    glm::vec4 solidColor;
    glm::vec4 mistColor;
    int32_t wire;
    float edgeWidth;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float padding0;
    float padding1;
    float padding2;
};

struct SimpleShadowUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::mat4 shadowMatrix;
    float uTrackLength;
    float heartline;
    int32_t isInstanced;
    int32_t isAsset;
};
