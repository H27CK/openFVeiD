#ifndef TRACKMESH_H
#define TRACKMESH_H

/*
#    FVD++, an advanced coaster design tool for NoLimits
#    Copyright (C) 2012-2015, Stephan "Lenny" Alt <alt.stephan@web.de>
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
#    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <vector>
#include <string>
#include <map>
#include <glm/glm.hpp>

#include "renderer/vulkan/vulkanbuffer.h"

typedef struct tracknode_s {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float vel;
    float rollSpeed;
    float yForce;
    float xForce;
    float flexion;
    float selected;
    int node;
} tracknode_t;

typedef struct meshnode_s {
    glm::vec3 pos;
    int node;
} meshnode_t;

typedef struct pipeoption_s {
    int edges;
    glm::vec2 radius;
    glm::vec2 offset;
    bool smooth;
} pipeoption_t;

class track;
class section;
class mnode;
class CustomTrackStyle;

struct track_asset_instance_t {
    glm::mat4 matrix;
    glm::vec4 attributes1; // x=selected, y=vel, z=rollSpeed, w=yForce
    glm::vec4 attributes2; // x=xForce, y=flexion, z=0, w=inverted (1.0 or 0.0)
    glm::vec3 color;
};

struct track_asset_mesh_t {
    VulkanBuffer baseVertexBuffer;
    VulkanBuffer baseIndexBuffer;
    VulkanBuffer instanceBuffer;
    bool baseUploaded = false;
    int indexCount = 0;
    std::vector<track_asset_instance_t> instances;
    CustomTrackStyle* sourceModel = nullptr;
    bool smoothNormalsAlongSpline = true;
};

struct gpu_spline_node_t {
    glm::vec4 pos; // xyz=pos, w=unused
    glm::vec4 lat;
    glm::vec4 norm;
    glm::vec4 dir;
};

class trackMesh {
public:
    trackMesh(track* parent = NULL);
    ~trackMesh();

    bool isInit;
    CustomTrackStyle* customStyle;
    std::string loadedStyleFile;

    std::vector<gpu_spline_node_t> gpuSpline;

    int currentMeshQuality = -1;

    void create3dsBox(std::vector<float>* _vertices, std::vector<unsigned int>* _indices,
                      glm::dvec3 P1l, glm::dvec3 P2l, glm::dvec3 P3l, glm::dvec3 P4l,
                      glm::dvec3 P1r, glm::dvec3 P2r, glm::dvec3 P3r, glm::dvec3 P4r);

    void create3dsQuad(std::vector<float>* _vertices, std::vector<unsigned int>* _indices,
                       glm::dvec3 P1, glm::dvec3 P2, glm::dvec3 P3, glm::dvec3 P4);

    void buildMeshes(int fromNode);
    void build3ds(const int _sec, std::vector<float>* _vertices,
                  std::vector<unsigned int>* _indices,
                  std::vector<unsigned int>* _borders);
    void updateVertexArrays(int fromNode = 0);
    void clearParametricStyles();

    void append3dsNode(std::vector<float>* _vertices);

    void appendMeshNode(std::vector<meshnode_t>& list);

    void recolorTrack(void);

    std::vector<tracknode_t> rails;
    std::vector<int> nodeList;
    std::vector<int> pipeIndices, shadowIndices;
    std::vector<int> pipeBorders;
    std::vector<tracknode_t> crossties;
    std::vector<tracknode_t> rendersupports;

    std::vector<track_asset_mesh_t> instancedAssets;
    std::vector<track_asset_mesh_t> instancedExtrusions;

    CustomTrackStyle* primitiveCylinder;
    CustomTrackStyle* primitiveBox;

    void generatePrimitives();

    std::vector<meshnode_t> supports;
    std::vector<meshnode_t> heartline;

    std::vector<pipeoption_t> options;

    std::vector<int> posList;
    std::vector<int> secList;

    VulkanBuffer splineBuffer;
    VkDescriptorSet splineStorageSet = VK_NULL_HANDLE;
    VulkanBuffer heartlineBuffer;

    track* trackData;

    int trackVertexSize, supportsSize, numRails, heartlineSize;

    void init();

    // Performance Caches
    std::vector<mnode*> allPoints;
    size_t gpuSplineCapacity = 0;

private:
    int j;
    int nextNode;
    glm::dvec3 nextPos;
    glm::dvec3 nextNorm;
    mnode* curNode;
    section* curSection;
};

#endif // TRACKMESH_H
