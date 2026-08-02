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

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

#include "renderer/vulkan/vulkanbuffer.h"
#include "renderer/vulkan/vulkanframebuffer.h"
#include "renderer/vulkan/vulkanpipeline.h"
#include "renderer/vulkan/vulkantexture.h"

class trackHandler;
class mnode;

struct Mesh {
    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;
    int count = 0;
};

struct GlbPrimitive {
    Mesh mesh;
    int vertexCount;
    int indexCount;
    glm::vec4 baseColorFactor = glm::vec4(1.0f);
    bool hasTexture = false;
    VulkanTexture texture;
};

struct GlbMesh {
    std::vector<GlbPrimitive> primitives;
    std::string path;
    bool visible = true;
    bool selected = false;
    glm::vec3 minAABB = glm::vec3(0.0f);
    glm::vec3 maxAABB = glm::vec3(0.0f);
};

class Viewport {
public:
    Viewport();
    ~Viewport();

    enum class ViewMode {
        Perspective,
        Top,
        Side,
        Front
    };

    void initialize(int width, int height);
    void shutdown();
    void resize(int width, int height);
    void render(const std::vector<trackHandler*>& trackList);
    void update(float deltaTime);

    void moveCamera(glm::vec3 delta);
    void rotateCamera(float yaw, float pitch);
    void panCamera(float dx, float dy);
    void zoomCamera(float delta);
    void lookAtOrigin();
    void resetView();
    void focusOnSection(int sectionIdx);

    void setMistColor(glm::vec3 color);
    void setFOV(float f) {
        if (fov != f) {
            fov = f;
            sceneDirty = true;
        }
    }
    void setShadowMode(int mode);
    void setLightDirection(float pitch, float yaw);
    void setMSAASamples(int samples);
    void setGroundTextureSize(float size) {
        if (grdTexSize != size) {
            grdTexSize = size;
            sceneDirty = true;
        }
    }
    bool loadGroundTexture(const std::string& path);
    bool addGlbMesh(const std::string& path);
    void removeGlbMesh(int index);
    bool selectGlbAtRay(float ndcX, float ndcY);
    bool hasSelectedGlb() const;
    void deleteSelectedGlb();

    void markSceneDirty() {
        sceneDirty = true;
    }
    void setCaptured(bool captured) {
        isCaptured = captured;
    }
    bool getCaptured() const {
        return isCaptured;
    }

    ViewMode getViewMode() const {
        return viewMode;
    }
    void setViewMode(ViewMode mode);

    bool getPOVMode() const {
        return povMode;
    }
    int getPOVPos() const {
        return povPos;
    }
    void setPOVMode(bool enabled);
    void setPOVPos(int pos) {
        if (povPos != pos) {
            povPos = pos;
            sceneDirty = true;
        }
    }
    void setActiveTrack(trackHandler* track);
    void movePOVCamera(float deltaZ, float deltaTime);
    void adjustPOVHeight(float delta) {
        if (delta != 0.0f) {
            povHeightOffset += delta;
            sceneDirty = true;
        }
    }
    void resetPOVHeight() {
        if (povHeightOffset != 0.0f) {
            povHeightOffset = 0.0f;
            sceneDirty = true;
        }
    }
    mnode* getPOVNode() const {
        return povNode;
    }

    void setShowPOVMarker3D(bool show) {
        if (showPOVMarker3D != show) {
            showPOVMarker3D = show;
            sceneDirty = true;
        }
    }

    bool getOrthoMode() const {
        return viewMode != ViewMode::Perspective;
    }
    void setOrthoMode(bool enabled) {
        setViewMode(enabled ? ViewMode::Top : ViewMode::Perspective);
    }
    float getOrthoScale() const {
        return orthoScale;
    }
    void setOrthoScale(float scale) {
        if (orthoScale != scale) {
            orthoScale = scale;
            sceneDirty = true;
        }
    }

    void setTrackShaderMode(int mode) {
        if (curTrackShader != mode) {
            curTrackShader = mode;
            sceneDirty = true;
        }
    }
    int getTrackShaderMode() const {
        return curTrackShader;
    }

    void captureScreenshot(int multiplier, const std::string& path, const std::vector<trackHandler*>& trackList);

    void* getOutputTexture();
    std::vector<GlbMesh> glbMeshes;

private:
    void initPipelines();
    void destroyPipelines();
    void initTextures();
    void initFloorMesh();

    void buildMatrices(float offset = 0.0f);
    void drawSky(VkCommandBuffer commandBuffer);
    void drawFloor(VkCommandBuffer commandBuffer);
    void drawOrthoGrid(VkCommandBuffer commandBuffer);
    enum class RenderPass {
        Main,
        PlanarShadow
    };

    void drawTrack(VkCommandBuffer commandBuffer, trackHandler* track, RenderPass pass);
    void drawGlbs(VkCommandBuffer commandBuffer, RenderPass pass);
    void drawMarkers(VkCommandBuffer commandBuffer);
    void refreshOutputTexture();

    VulkanFramebuffer finalOutputFb;
    void* outputTextureId = nullptr;

    VulkanPipeline skyPipeline;
    VulkanPipeline floorPipeline;
    VulkanPipeline heartlinePipeline;
    VulkanPipeline trackInstancedPipeline;
    VulkanPipeline shadowInstancedPipeline;
    VulkanPipeline shadowGlbPipeline;
    VulkanPipeline glbPipeline;
    VulkanPipeline markerPipeline;
    VulkanPipeline orthoGridPipeline;

    VulkanTexture* skyTexture = nullptr;
    VulkanTexture floorTexture;
    VulkanTexture rasterTexture;
    VulkanTexture dummyCubeTexture;
    VulkanTexture dummyWhiteTexture;
    VulkanBuffer zeroAttributeBuffer;

    Mesh skyMesh;
    Mesh floorMesh;
    Mesh markerMesh;

    glm::vec3 freeFlyPos;
    glm::vec3 freeFlyDir;
    glm::vec3 freeFlySide;
    glm::vec3 cameraPos;
    glm::vec3 lightDir;
    glm::mat4 shadowMatrix;

    glm::mat4 ProjectionMatrix;
    glm::mat4 ModelMatrix;
    glm::mat4 ProjectionModelMatrix;

    float fov;
    glm::vec3 mistColor;
    float grdTexSize = 440.0f;
    int viewPortWidth, viewPortHeight;

    bool povMode;
    int povPos;
    mnode* povNode;
    float povHeightOffset = 0.0f;
    bool showPOVMarker3D = true;
    trackHandler* activeTrack = nullptr;

    ViewMode viewMode;
    float orthoScale;

    int shadowMode;
    int curTrackShader;
    int msaaSamples;
    VkSampleCountFlagBits currentSampleCount = VK_SAMPLE_COUNT_1_BIT;

    bool sceneDirty = true;
    bool isCaptured = false;
    double lastShadowUpdateTime = 0.0;
    bool shadowIsHighQuality = false;
};

#endif
