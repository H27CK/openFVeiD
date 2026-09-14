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

struct alignas(16) FloorUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::vec4 eyePos;
    glm::vec4 floorColor;
    glm::vec4 mistColor;
    glm::vec4 lightDir;
    glm::vec4 ambientColor;
    glm::vec4 sunColor;
    glm::vec4 gridColor;
    float floorHeight;
    float grdTexSize;
    float opacity;
    int32_t border;
    int32_t grid;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float ambientStrength;
    float sunStrength;
    float gridMajorThickness;
    float gridMinorThickness;
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
    glm::vec4 ambientColor;
    glm::vec4 sunColor;
    int32_t colorMode;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float ambientStrength;
    float sunStrength;
    float padding0;
    float padding1;
};

struct alignas(16) TrackInstancedUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::vec4 eyePos;
    glm::vec4 lightDir;
    glm::vec4 defaultColor;
    glm::vec4 sectionColor;
    glm::vec4 transitionColor;
    glm::vec4 mistColor;
    glm::vec4 ambientColor;
    glm::vec4 sunColor;
    int32_t colorMode;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float trackLength;
    float heartline;
    int32_t isAsset;
    int32_t smoothAlongSpline;
    float ambientStrength;
    float sunStrength;
    int32_t materialEnabled;
    float padding1;
};

struct StlUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::vec4 eyePos;
    glm::vec4 lightDir;
    glm::vec4 solidColor;
    glm::vec4 mistColor;
    glm::vec4 ambientColor;
    glm::vec4 sunColor;
    int32_t wire;
    float edgeWidth;
    int32_t mistEnabled;
    float mistNear;
    float mistFar;
    float ambientStrength;
    float sunStrength;
    float padding0;
};

struct alignas(16) SimpleShadowUniforms {
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
    glm::mat4 anchorBase;
    glm::mat4 shadowMatrix;
    glm::vec4 shadowColor;
    float uTrackLength;
    float heartline;
    int32_t isInstanced;
    int32_t isAsset;
    float shadowStrength;
    float floorHeight;
    int32_t softShadowsEnabled;
    float padding2;
};

static_assert(sizeof(FloorUniforms) == 288, "FloorUniforms must match the std140 shader block");
static_assert(sizeof(TrackInstancedUniforms) == 368, "TrackInstancedUniforms must match the std140 shader block");
static_assert(sizeof(SimpleShadowUniforms) == 304, "SimpleShadowUniforms must match the std140 shader block");
