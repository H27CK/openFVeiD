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

#include "trackmesh.h"
#include "renderer/vulkan/vulkancontext.h"
#include "mnode.h"
#include "section.h"
#include "track.h"
#include <iostream>
#include <algorithm>
#include "renderer/viewport.h"
#include "customstyle.h"
#include "dummies.h"
#include "logger.h"
#include <chrono>
#include <map>

static void releaseAssetMesh(track_asset_mesh_t& assetMesh) {
    if (gVulkanContext)
        gVulkanContext->waitIdle();
    assetMesh.baseVertexBuffer.destroy();
    assetMesh.baseIndexBuffer.destroy();
    assetMesh.instanceBuffer.destroy();
    assetMesh.baseUploaded = false;
}

trackMesh::trackMesh(track* parent) {
    isInit = false;
    customStyle = nullptr;
    primitiveCylinder = nullptr;
    primitiveBox = nullptr;
    currentMeshQuality = -1;

    trackVertexSize = 0;
    numRails = 0;
    supportsSize = 0;
    heartlineSize = 0;
    trackData = parent;
}

trackMesh::~trackMesh() {
    if (customStyle) {
        delete customStyle;
        customStyle = nullptr;
    }
    for (auto& am : instancedAssets)
        releaseAssetMesh(am);
    instancedAssets.clear();

    for (auto& am : instancedExtrusions)
        releaseAssetMesh(am);
    instancedExtrusions.clear();

    if (primitiveCylinder)
        delete primitiveCylinder;
    if (primitiveBox)
        delete primitiveBox;

    if (gVulkanContext)
        gVulkanContext->waitIdle();
    splineBuffer.destroy();
    heartlineBuffer.destroy();
}

void trackMesh::init() {
    isInit = true;
    buildMeshes(0);
}

void trackMesh::appendMeshNode(std::vector<meshnode_t>& list) {
    meshnode_t temp;
    temp.node = nextNode;
    temp.pos = glm::vec3(nextPos);
    list.push_back(temp);
}

void trackMesh::buildMeshes(int fromNode) {

    if (trackData->lSections.empty()) {
        rails.clear();
        crossties.clear();
        rendersupports.clear();
        supports.clear();
        heartline.clear();
        nodeList.clear();
        pipeIndices.clear();
        shadowIndices.clear();
        posList.clear();
        secList.clear();
        allPoints.clear();
        trackVertexSize = 0;
        supportsSize = 0;
        heartlineSize = 0;
        numRails = 0;

        for (auto& am : instancedExtrusions) {
            am.instances.clear();
        }
        for (auto& am : instancedAssets) {
            am.instances.clear();
        }

        updateVertexArrays(0);
        return;
    }

    if (loadedStyleFile != trackData->customStyleFile ||
        instancedExtrusions.size() != trackData->customExtrusions.size() ||
        instancedAssets.size() != trackData->customAssets.size()) {
        loadedStyleFile = trackData->customStyleFile;
        fromNode = 0;
    }

    generatePrimitives();

    auto start = std::chrono::high_resolution_clock::now();

    int numPoints = trackData->getNumPoints();
    float totalTrackLength = numPoints > 0 ? (float)trackData->getPoint(numPoints)->fTotalLength : 0.0f;

    // Performance Optimization: Cache flattened points incrementally
    if (fromNode == 0) {
        allPoints.clear();
        allPoints.reserve(numPoints + 1);
        allPoints.push_back(trackData->anchorNode);
    } else if (fromNode < (int)allPoints.size()) {
        allPoints.resize(fromNode);
    }

    if ((int)allPoints.size() <= numPoints) {
        int sectionIdx, nodeInSec;
        trackData->getSecNode(allPoints.size(), &nodeInSec, &sectionIdx);
        for (int s = sectionIdx; s < (int)trackData->lSections.size(); ++s) {
            auto sec = trackData->lSections[s];
            int startNode = (s == sectionIdx) ? nodeInSec : 1;
            for (int i = startNode; i < (int)sec->lNodes.size(); ++i) {
                allPoints.push_back(&sec->lNodes[i]);
            }
        }
    }

    float startDist = 0.0f;
    if (fromNode > 0 && fromNode <= numPoints) {
        startDist = std::max(0.0f, (float)allPoints[fromNode]->fTotalLength - 0.2f); // Small overlap for safety
    }

    int spliceSpline = std::floor(startDist / 0.1f);
    if (spliceSpline < (int)gpuSpline.size()) {
        gpuSpline.resize(spliceSpline);
    } else {
        spliceSpline = (int)gpuSpline.size();
    }

    // Performance Optimization: Use a linear cursor instead of binary search for every node
    int currentSplineIdx = (spliceSpline == 0) ? 0 : trackData->getIndexFromDist(spliceSpline * 0.1f);

    for (float d = spliceSpline * 0.1f; d <= totalTrackLength + 0.15f; d += 0.1f) {
        while (currentSplineIdx < numPoints && allPoints[currentSplineIdx + 1]->fTotalLength <= d) {
            currentSplineIdx++;
        }
        int idx0 = currentSplineIdx;
        int idx1 = std::min(idx0 + 1, numPoints);
        mnode* n0 = allPoints[idx0];
        mnode* n1 = allPoints[idx1];

        gpu_spline_node_t gn;
        if (n0 && n1) {
            double d0 = n0->fTotalLength;
            double d1 = n1->fTotalLength;
            double t = (d1 > d0) ? (double)((d - d0) / (d1 - d0)) : 0.0;
            t = glm::clamp(t, 0.0, 1.0);

            double heartOffset = (double)trackData->fHeart;
            glm::dvec3 pos = glm::mix(n0->vPos, n1->vPos, t);
            glm::dvec3 lat = glm::normalize(glm::mix(n0->vLatHeart(heartOffset), n1->vLatHeart(heartOffset), t));
            glm::dvec3 norm = glm::normalize(glm::mix(n0->vNorm, n1->vNorm, t));
            glm::dvec3 dir = glm::normalize(glm::mix(n0->vDirHeart(heartOffset), n1->vDirHeart(heartOffset), t));

            pos += heartOffset * norm;

            float sel = 0.0f;
            int sectionIdx = -1, nodeInSec = -1;

            // Find which section this node belongs to (more efficiently)
            int runningSum = 0;
            for (int s = 0; s < (int)trackData->lSections.size(); ++s) {
                int secNodes = (int)trackData->lSections[s]->lNodes.size() - 1;
                if (idx0 <= runningSum + secNodes) {
                    sectionIdx = s;
                    nodeInSec = idx0 - runningSum;
                    break;
                }
                runningSum += secNodes;
            }

            if (sectionIdx >= 0) {
                section* curSec = trackData->lSections[sectionIdx];
                if (trackData == gloParent->curTrack() && curSec == trackData->activeSection) {
                    sel = curSec->isInFunction(nodeInSec, gloParent->selectedFunc) ? 2.0f : 1.0f;
                }
            }

            gn.pos = glm::vec4(pos, (double)d);
            gn.lat = glm::vec4(lat, (double)sel);
            gn.norm = glm::vec4(norm, 0.0);
            gn.dir = glm::vec4(dir, 0.0);
        } else {
            gn.pos = glm::vec4(0.0);
            gn.lat = glm::vec4(1, 0, 0, 0);
            gn.norm = glm::vec4(0, 1, 0, 0);
            gn.dir = glm::vec4(0, 0, 1, 0);
        }
        gpuSpline.push_back(gn);
    }

    if (fromNode == 0) {
        rails.clear();
        crossties.clear();
        rendersupports.clear();
        supports.clear();
        heartline.clear();
        nodeList.clear();
        pipeIndices.clear();
        shadowIndices.clear();
        posList.clear();
        secList.clear();
        trackVertexSize = 0;
        supportsSize = 0;
        heartlineSize = 0;
    } else {
        if (fromNode < (int)heartline.size()) {
            heartline.resize(fromNode);
            nodeList.resize(fromNode);
            heartlineSize = fromNode;
        }
    }

    int startN = (fromNode == 0) ? 0 : (int)heartline.size();
    for (int n = startN; n <= numPoints; ++n) {
        mnode* node = trackData->getPoint(n);
        nextPos = node->vPos;
        nextNode = n;
        nodeList.push_back(n);
        appendMeshNode(heartline);
        heartlineSize++;
    }

    while (instancedExtrusions.size() < trackData->customExtrusions.size()) {
        instancedExtrusions.push_back({});
    }
    while (instancedExtrusions.size() > trackData->customExtrusions.size()) {
        releaseAssetMesh(instancedExtrusions.back());
        instancedExtrusions.pop_back();
    }

    for (size_t extIdx = 0; extIdx < trackData->customExtrusions.size(); ++extIdx) {
        auto& ext = trackData->customExtrusions[extIdx];
        auto& am = instancedExtrusions[extIdx];

        CustomTrackStyle* targetModel = (ext.shape == track::ExtrusionShape::Cylindrical) ? primitiveCylinder : primitiveBox;
        if (am.sourceModel != targetModel) {
            releaseAssetMesh(am);
            am.sourceModel = targetModel;
            am.instances.clear(); // Full rebuild if model changed
        }
        am.indexCount = (int)am.sourceModel->indices.size();

        int spliceExt = std::floor(startDist / 1.0f);
        if (spliceExt < (int)am.instances.size()) {
            am.instances.resize(spliceExt);
        } else if (startDist == 0.0f) {
            am.instances.clear();
        }

        int currentExtIdx = (am.instances.empty()) ? 0 : trackData->getIndexFromDist(am.instances.size() * 1.0f);

        for (float d = am.instances.size() * 1.0f; d < totalTrackLength; d += 1.0f) {
            while (currentExtIdx < numPoints && allPoints[currentExtIdx + 1]->fTotalLength <= d) {
                currentExtIdx++;
            }
            int nodeIdx = currentExtIdx;

            float selectedState = 0.0f;
            int sectionIdx = -1, nodeInSec = -1;

            int runningSum = 0;
            for (int s = 0; s < (int)trackData->lSections.size(); ++s) {
                int secNodes = (int)trackData->lSections[s]->lNodes.size() - 1;
                if (nodeIdx <= runningSum + secNodes) {
                    sectionIdx = s;
                    nodeInSec = nodeIdx - runningSum;
                    break;
                }
                runningSum += secNodes;
            }

            if (sectionIdx >= 0) {
                section* curSection = trackData->lSections[sectionIdx];
                if (trackData == gloParent->curTrack() && curSection == trackData->activeSection) {
                    selectedState = curSection->isInFunction(nodeInSec, gloParent->selectedFunc) ? 2.0f : 1.0f;
                }
            }
            mnode* n = allPoints[nodeIdx];

            glm::mat4 mat(1.0f);
            mat[0] = glm::vec4(ext.size.x * 0.5f, 0, 0, 0);
            mat[1] = glm::vec4(0, (ext.shape == track::ExtrusionShape::Cylindrical ? ext.size.x : ext.size.y) * 0.5f, 0, 0);
            mat[3].x = d;
            mat[3].y = 0.0f;
            mat[3].z = 0.0f;

            track_asset_instance_t inst;
            inst.matrix = mat;
            inst.attributes1 = glm::vec4(selectedState, (float)n->fVel, (float)n->fRollSpeed, (float)n->forceNormal);
            inst.attributes2 = glm::vec4((float)n->forceLateral, (float)n->fFlexion(), (float)ext.offset.x, (float)ext.offset.y);
            inst.color = glm::vec3(0.6f);
            am.instances.push_back(inst);
        }
    }

    while (instancedAssets.size() < trackData->customAssets.size()) {
        instancedAssets.push_back({});
    }
    while (instancedAssets.size() > trackData->customAssets.size()) {
        releaseAssetMesh(instancedAssets.back());
        instancedAssets.pop_back();
    }

    for (size_t assetIdx = 0; assetIdx < trackData->customAssets.size(); ++assetIdx) {
        auto& asset = trackData->customAssets[assetIdx];
        auto& assetMesh = instancedAssets[assetIdx];

        if (asset.filepath.empty()) {
            assetMesh.instances.clear();
            continue;
        }

        if (!asset.loadedModel) {
            asset.loadedModel = new CustomTrackStyle();
            if (!asset.loadedModel->load(asset.filepath)) {
                delete asset.loadedModel;
                asset.loadedModel = nullptr;
                continue;
            }
        }
        if (!asset.loadedModel->isValid)
            continue;

        assetMesh.smoothNormalsAlongSpline = asset.smoothAlongSpline;

        if (assetMesh.sourceModel != asset.loadedModel) {
            releaseAssetMesh(assetMesh);
            assetMesh.sourceModel = asset.loadedModel;
            assetMesh.instances.clear();
        }
        assetMesh.indexCount = (int)asset.loadedModel->indices.size();

        float minZ = 999999.0f;
        for (const auto& v : asset.loadedModel->vertices)
            if (v.pos.z < minZ)
                minZ = v.pos.z;

        float start = std::max(0.0f, asset.startDist);
        float actualEndDist = (asset.endDist < 0.0f) ? totalTrackLength : asset.endDist;
        float end = std::min(totalTrackLength, actualEndDist);
        float interval = std::max(0.01f, asset.interval);

        float relStart = startDist - start;
        if (relStart > 0.0f) {
            int spliceAsset = std::floor(relStart / interval);
            if (spliceAsset < (int)assetMesh.instances.size()) {
                assetMesh.instances.resize(spliceAsset);
            }
        } else if (startDist == 0.0f) {
            assetMesh.instances.clear();
        }

        int currentAssetIdx = (assetMesh.instances.empty()) ? 0 : trackData->getIndexFromDist(start + assetMesh.instances.size() * interval);

        for (float d = start + assetMesh.instances.size() * interval; d <= end; d += interval) {
            while (currentAssetIdx < numPoints && allPoints[currentAssetIdx + 1]->fTotalLength <= d) {
                currentAssetIdx++;
            }
            int nodeIdx = currentAssetIdx;

            float selectedState = 0.0f;
            int sectionIdx = -1, nodeInSec = -1;

            int runningSum = 0;
            for (int s = 0; s < (int)trackData->lSections.size(); ++s) {
                int secNodes = (int)trackData->lSections[s]->lNodes.size() - 1;
                if (nodeIdx <= runningSum + secNodes) {
                    sectionIdx = s;
                    nodeInSec = nodeIdx - runningSum;
                    break;
                }
                runningSum += secNodes;
            }

            if (sectionIdx >= 0) {
                section* curSection = trackData->lSections[sectionIdx];
                if (trackData == gloParent->curTrack() && curSection == trackData->activeSection) {
                    selectedState = curSection->isInFunction(nodeInSec, gloParent->selectedFunc) ? 2.0f : 1.0f;
                }
            }
            mnode* n = allPoints[nodeIdx];

            glm::mat4 mat(1.0f);
            mat[3].x = d;
            mat[3].y = 0.0f; // Uniform used instead
            mat[3].z = minZ;
            mat[3].w = 1.0f;

            track_asset_instance_t inst;
            inst.matrix = mat;

            inst.attributes1 = glm::vec4(selectedState, (float)n->fVel, (float)n->fRollSpeed, (float)n->forceNormal);
            inst.attributes2 = glm::vec4((float)n->forceLateral, (float)n->fFlexion(), 0.0f, 0.0f);
            inst.color = asset.color;
            assetMesh.instances.push_back(inst);
        }
    }

    updateVertexArrays(fromNode);
}

void trackMesh::recolorTrack() {
    if (static_cast<int>(trackData->lSections.size()) == 0)
        return;

    for (int i = 0; i < (int)rails.size(); ++i) {
        int node = rails[i].node;
        int sectionIdx;
        trackData->getSecNode(node, &node, &sectionIdx);

        if (sectionIdx >= 0) {
            section* curSec = trackData->lSections[sectionIdx];
            rails[i].selected =
                (trackData == gloParent->curTrack() &&
                 curSec == trackData->activeSection)
                    ? (curSec->isInFunction(node, gloParent->selectedFunc) ? 2.f : 1.f)
                    : 0.f;
        } else {
            rails[i].selected = 0.0f;
        }
    }

    for (int i = 0; i < static_cast<int>(crossties.size()); ++i) {
        int node = crossties[i].node;
        int sectionIdx;
        trackData->getSecNode(node, &node, &sectionIdx);

        if (sectionIdx >= 0) {
            section* curSec = trackData->lSections[sectionIdx];
            crossties[i].selected =
                (trackData == gloParent->curTrack() &&
                 curSec == trackData->activeSection)
                    ? 1.f
                    : 0.f;
        } else {
            crossties[i].selected = 0.0f;
        }
    }
    updateVertexArrays(0);
}

void trackMesh::clearParametricStyles() {
    for (auto& am : instancedExtrusions) {
        releaseAssetMesh(am);
        am.instances.clear();
    }
    instancedExtrusions.clear();

    for (auto& am : instancedAssets) {
        releaseAssetMesh(am);
        am.instances.clear();
    }
    instancedAssets.clear();

    loadedStyleFile = "";
}

void trackMesh::updateVertexArrays(int fromNode) {
    (void)fromNode;
    if (!gVulkanContext)
        return;

    gVulkanContext->waitIdle();

    if (!gpuSpline.empty()) {
        size_t requiredSize = gpuSpline.size() * sizeof(gpu_spline_node_t);
        bool grew = requiredSize > splineBuffer.capacity;
        splineBuffer.ensureCapacity(*gVulkanContext, requiredSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        splineBuffer.write(gpuSpline.data(), requiredSize);
        if (splineStorageSet == VK_NULL_HANDLE) {
            splineStorageSet = gVulkanContext->allocateStorageBufferSet(splineBuffer.buffer, splineBuffer.capacity);
        } else if (grew) {
            gVulkanContext->updateStorageBufferSet(splineStorageSet, splineBuffer.buffer, splineBuffer.capacity);
        }
        gpuSplineCapacity = splineBuffer.capacity;
    }

    auto uploadInstanced = [&](track_asset_mesh_t& am) {
        if (am.instances.empty() || am.sourceModel == nullptr)
            return;
        if (!am.baseUploaded) {
            size_t vertexBytes = am.sourceModel->vertices.size() * sizeof(StyleVertex);
            size_t indexBytes = am.sourceModel->indices.size() * sizeof(unsigned int);
            am.baseVertexBuffer.ensureCapacity(*gVulkanContext, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            am.baseVertexBuffer.write(am.sourceModel->vertices.data(), vertexBytes);
            am.baseIndexBuffer.ensureCapacity(*gVulkanContext, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            am.baseIndexBuffer.write(am.sourceModel->indices.data(), indexBytes);
            am.baseUploaded = true;
        }
        size_t instanceBytes = am.instances.size() * sizeof(track_asset_instance_t);
        am.instanceBuffer.ensureCapacity(*gVulkanContext, instanceBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        am.instanceBuffer.write(am.instances.data(), instanceBytes);
    };
    for (auto& am : instancedAssets)
        uploadInstanced(am);
    for (auto& am : instancedExtrusions)
        uploadInstanced(am);

    if (!heartline.empty()) {
        size_t heartlineBytes = heartline.size() * sizeof(meshnode_t);
        heartlineBuffer.ensureCapacity(*gVulkanContext, heartlineBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        heartlineBuffer.write(heartline.data(), heartlineBytes);
    }
}

void trackMesh::generatePrimitives() {
    int quality = gloParent->mOptions->meshQuality;
    if (quality == currentMeshQuality && primitiveCylinder != nullptr)
        return;
    currentMeshQuality = quality;

    if (primitiveCylinder)
        delete primitiveCylinder;
    if (primitiveBox)
        delete primitiveBox;
    for (auto& am : instancedExtrusions) {
        releaseAssetMesh(am);
        am.sourceModel = nullptr;
    }

    int sides = 8, rings = 5;
    if (quality == 1) {
        sides = 12;
        rings = 10;
    } else if (quality == 2) {
        sides = 16;
        rings = 20;
    } else if (quality == 3) {
        sides = 24;
        rings = 30;
    } else if (quality == 4) {
        sides = 32;
        rings = 50;
    }

    primitiveCylinder = new CustomTrackStyle();
    primitiveCylinder->length = 1.0f;
    for (int r = 0; r <= rings; ++r) {
        float z = (float)r / (float)rings;
        for (int s = 0; s < sides; ++s) {
            float angle = (float)s * 2.0f * 3.14159265358979323846 / (float)sides;
            StyleVertex v;
            v.pos = glm::vec3(cos(angle), sin(angle), z);
            v.normal = glm::vec3(cos(angle), sin(angle), 0.0f);
            v.uv = glm::vec2((float)s / (float)sides, z);
            v.faceRefZ = 0.0f;
            primitiveCylinder->vertices.push_back(v);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sides; ++s) {
            int i0 = r * sides + s, i1 = r * sides + (s + 1) % sides, i2 = (r + 1) * sides + s, i3 = (r + 1) * sides + (s + 1) % sides;
            primitiveCylinder->indices.push_back(i0);
            primitiveCylinder->indices.push_back(i2);
            primitiveCylinder->indices.push_back(i1);
            primitiveCylinder->indices.push_back(i1);
            primitiveCylinder->indices.push_back(i2);
            primitiveCylinder->indices.push_back(i3);
        }
    }
    primitiveCylinder->isValid = true;

    primitiveBox = new CustomTrackStyle();
    primitiveBox->length = 1.0f;
    for (int r = 0; r <= rings; ++r) {
        float z = (float)r / (float)rings;
        glm::vec3 p[4] = {glm::vec3(-1, -1, z), glm::vec3(1, -1, z), glm::vec3(1, 1, z), glm::vec3(-1, 1, z)};
        glm::vec3 n[4] = {glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0)};

        for (int f = 0; f < 4; ++f) {
            StyleVertex v1, v2;
            v1.pos = p[f];
            v1.normal = n[f];
            v1.uv = glm::vec2(0.0f, z);
            v1.faceRefZ = 0.0f;
            v2.pos = p[(f + 1) % 4];
            v2.normal = n[f];
            v2.uv = glm::vec2(1.0f, z);
            v2.faceRefZ = 0.0f;
            primitiveBox->vertices.push_back(v1);
            primitiveBox->vertices.push_back(v2);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int f = 0; f < 4; ++f) {
            int base = r * 8 + f * 2;
            int next = (r + 1) * 8 + f * 2;
            primitiveBox->indices.push_back(base);
            primitiveBox->indices.push_back(next);
            primitiveBox->indices.push_back(base + 1);
            primitiveBox->indices.push_back(base + 1);
            primitiveBox->indices.push_back(next);
            primitiveBox->indices.push_back(next + 1);
        }
    }
    for (size_t t = 0; t + 2 < primitiveBox->indices.size(); t += 3) {
        unsigned int i0 = primitiveBox->indices[t];
        unsigned int i1 = primitiveBox->indices[t + 1];
        unsigned int i2 = primitiveBox->indices[t + 2];
        float avgZ = (primitiveBox->vertices[i0].pos.z + primitiveBox->vertices[i1].pos.z + primitiveBox->vertices[i2].pos.z) / 3.0f;
        primitiveBox->vertices[i0].faceRefZ = avgZ;
        primitiveBox->vertices[i1].faceRefZ = avgZ;
        primitiveBox->vertices[i2].faceRefZ = avgZ;
    }
    primitiveBox->isValid = true;
}
