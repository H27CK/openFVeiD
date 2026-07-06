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

#include "viewport.h"
#include "shaderprogram.h"
#include "gltexture.h"
#include "glframebuffer.h"
#include "trackhandler.h"
#include "track.h"
#include "trackmesh.h"
#include "mnode.h"
#include "lenassert.h"
#include "dummies.h"
#include "stlreader.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include "imgui.h"
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Viewport::Viewport()
    : msaaFBO(0), msaaColorTex(0), msaaDepthRB(0),
      normalMapFb(nullptr),
      occlusionFb(nullptr), finalOutputFb(nullptr),
      skyShader(nullptr), floorShader(nullptr), trackShader(nullptr),
      trackInstancedShader(nullptr), normalMapShader(nullptr),
      occlusionShader(nullptr), simpleShadowShader(nullptr), stlShader(nullptr),
      skyTexture(nullptr), floorTexture(nullptr), rasterTexture(nullptr),
      viewPortWidth(1280), viewPortHeight(720),
      fov(90.0f), mistColor(0.15f, 0.15f, 0.15f),
      povMode(false), povPos(0), povNode(nullptr),
      viewMode(ViewMode::Perspective), orthoScale(50.0f),
      shadowMode(0), curTrackShader(0), msaaSamples(4),
      sceneDirty(true) {
    lookAtOrigin();
    setLightDirection(gloParent->mOptions->sunPitch, gloParent->mOptions->sunYaw);
}

void Viewport::setViewMode(ViewMode mode) {
    if (viewMode == mode)
        return;

    if (mode != ViewMode::Perspective) {
        povMode = false;
    }

    viewMode = mode;
    sceneDirty = true;
    resetView();
}

void Viewport::resetView() {
    sceneDirty = true;
    int sectionIdx = -1;
    if (activeTrack && activeTrack->trackData) {
        track* curTrack = activeTrack->trackData;
        if (curTrack->activeSection) {
            auto it = std::find(curTrack->lSections.begin(), curTrack->lSections.end(), curTrack->activeSection);
            if (it != curTrack->lSections.end()) {
                sectionIdx = std::distance(curTrack->lSections.begin(), it);
            }
        }
        focusOnSection(sectionIdx);
        return;
    }

    glm::vec3 anchor(0.0f);
    if (viewMode == ViewMode::Perspective) {
        freeFlyPos = anchor + glm::vec3(20.0f, 20.0f, 20.0f);
        freeFlyDir = glm::normalize(anchor - freeFlyPos);
        freeFlySide = glm::normalize(glm::cross(freeFlyDir, glm::vec3(0.0f, 1.0f, 0.0f)));
    } else if (viewMode == ViewMode::Top) {
        freeFlyPos = anchor + glm::vec3(0, 100, 0);
        freeFlyDir = glm::vec3(0, -1, 0);
        freeFlySide = glm::vec3(0, 0, -1);
    } else if (viewMode == ViewMode::Side) {
        freeFlyPos = anchor + glm::vec3(-100, 0, 0);
        freeFlyDir = glm::vec3(1, 0, 0);
        freeFlySide = glm::vec3(0, 0, -1);
    } else if (viewMode == ViewMode::Front) {
        freeFlyPos = anchor + glm::vec3(0, 0, 100);
        freeFlyDir = glm::vec3(0, 0, -1);
        freeFlySide = glm::vec3(-1, 0, 0);
    }
    orthoScale = 50.0f;
}

void Viewport::lookAtOrigin() {
    resetView();
    sceneDirty = true;
}

Viewport::~Viewport() {
    delete skyShader;
    delete floorShader;
    delete trackShader;
    delete trackInstancedShader;
    delete normalMapShader;
    delete occlusionShader;
    delete simpleShadowShader;
    delete stlShader;
    delete skyTexture;
    delete floorTexture;
    delete rasterTexture;
    delete normalMapFb;
    delete occlusionFb;
    delete finalOutputFb;

    glDeleteVertexArrays(1, &skyMesh.object);
    glDeleteBuffers(1, &skyMesh.buffer);
    glDeleteVertexArrays(1, &floorMesh.object);
    glDeleteBuffers(1, &floorMesh.buffer);
    glDeleteVertexArrays(1, &markerMesh.object);
    glDeleteBuffers(1, &markerMesh.buffer);
    if (markerMesh.ebo != 0)
        glDeleteBuffers(1, &markerMesh.ebo);

    if (orthoGridMesh.object != 0) {
        glDeleteVertexArrays(1, &orthoGridMesh.object);
    }
    if (orthoGridMesh.buffer != 0) {
        glDeleteBuffers(1, &orthoGridMesh.buffer);
    }

    if (msaaFBO) {
        glDeleteFramebuffers(1, &msaaFBO);
        glDeleteTextures(1, &msaaColorTex);
        glDeleteRenderbuffers(1, &msaaDepthRB);
    }
}

void Viewport::setMistColor(glm::vec3 color) {
    mistColor = color;
    sceneDirty = true;
    if (finalOutputFb)
        finalOutputFb->setClearColor(color.x, color.y, color.z);
}

void Viewport::setShadowMode(int mode) {
    if (mode == shadowMode)
        return;
    shadowMode = mode;
    sceneDirty = true;
    shadowIsHighQuality = false;
}

void Viewport::setLightDirection(float pitch, float yaw) {
    float p = glm::radians(pitch);
    float y = glm::radians(yaw);

    // Standard spherical to cartesian conversion
    lightDir.x = cos(p) * sin(y);
    lightDir.y = sin(p);
    lightDir.z = cos(p) * cos(y);
    lightDir = glm::normalize(lightDir);
    sceneDirty = true;
}

void Viewport::setMSAASamples(int samples) {
    if (msaaSamples == samples)
        return;
    msaaSamples = samples;
    sceneDirty = true;
    if (viewPortWidth > 0 && viewPortHeight > 0) {
        setOrthoScale(orthoScale); // Just to trigger some state if needed, wait no.
        // Re-init MSAA FBO
        if (msaaFBO) {
            glDeleteFramebuffers(1, &msaaFBO);
            glDeleteTextures(1, &msaaColorTex);
            glDeleteRenderbuffers(1, &msaaDepthRB);
            msaaFBO = 0;
        }
        resize(viewPortWidth, viewPortHeight);
    }
}

void Viewport::initialize(int width, int height) {
    viewPortWidth = width;
    viewPortHeight = height;

    GlTexture::initialize();
    initShaders();
    initTextures();
    initFloorMesh();

    finalOutputFb = new GlFramebuffer(width, height, GL_RGBA, GL_RGBA, true);

    normalMapFb = new GlFramebuffer(width, height, GL_RGBA16F, GL_RGBA, true);
    occlusionFb = new GlFramebuffer(width, height, GL_RED, GL_RED);
}

void Viewport::resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == viewPortWidth && height == viewPortHeight))
        return;
    sceneDirty = true;
    viewPortWidth = width;
    viewPortHeight = height;
    finalOutputFb->resize(width, height);
    normalMapFb->resize(width, height);
    occlusionFb->resize(width, height);

    if (msaaSamples > 0) {
        if (msaaFBO == 0) {
            glGenFramebuffers(1, &msaaFBO);
            glGenTextures(1, &msaaColorTex);
            glGenRenderbuffers(1, &msaaDepthRB);
        }

        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaaColorTex);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, msaaSamples, GL_RGBA8, width, height, GL_TRUE);

        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRB);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaaSamples, GL_DEPTH_STENCIL, width, height);

        glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, msaaColorTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRB);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "MSAA Framebuffer is not complete!" << std::endl;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else if (msaaFBO != 0) {
        glDeleteFramebuffers(1, &msaaFBO);
        glDeleteTextures(1, &msaaColorTex);
        glDeleteRenderbuffers(1, &msaaDepthRB);
        msaaFBO = 0;
        msaaColorTex = 0;
        msaaDepthRB = 0;
    }
}

void Viewport::render(const std::vector<trackHandler*>& trackList) {
    for (auto track : trackList) {
        if (track->trackData) {
            track->trackData->processPendingUpdates();
            if (track->trackData->hasChanged) {
                sceneDirty = true;
            }
        }
    }

    bool interactionActive = isCaptured || ImGui::IsAnyItemActive();

    if (!sceneDirty && !interactionActive) {
        return;
    }

    buildMatrices();

    // 1. Scene Setup
    occlusionFb->setClearColor(1.0f, 0.0f, 0.0f);
    occlusionFb->clear();

    if (msaaSamples > 0 && msaaFBO != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFBO);
    } else {
        finalOutputFb->bind();
    }

    glViewport(0, 0, viewPortWidth, viewPortHeight);
    glClearColor(mistColor.r, mistColor.g, mistColor.b, 1.0f);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 2. Base Pass (Sky, Floor)
    drawSky();

    // Draw Floor and mark it in the Stencil Buffer (set to 1)
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    drawFloor();
    drawOrthoGrid();
    glDisable(GL_STENCIL_TEST);

    // 3. Planar Shadow Pass (Geometry Projection)
    if (shadowMode > 0) {
        bool shouldUpdateShadows = true;

        if (shouldUpdateShadows) {
            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_STENCIL_TEST);

            // Only draw where floor is (stencil == 1)
            glStencilFunc(GL_EQUAL, 1, 0xFF);
            // If it passes, increment stencil to 2 to prevent double-shadowing
            glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

            for (auto track : trackList) {
                if (track->trackData->drawHeartline != 3) {
                    drawTrack(track, RenderPass::PlanarShadow);
                }
            }
            if (gloParent->mOptions->stlShadowsEnabled) {
                drawStls(RenderPass::PlanarShadow);
            }

            glDisable(GL_STENCIL_TEST);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
        }
    }

    // 4. Scene Pass (Track and STLs)
    for (auto track : trackList) {
        if (track->trackData->drawHeartline != 3) {
            if (track->trackData->hasChanged) {
                track->mMesh->recolorTrack();
                track->trackData->hasChanged = false;
            }
            drawTrack(track, RenderPass::Main);
        }
    }
    drawStls(RenderPass::Main);

    // 4. UI/Markers Pass
    drawMarkers(RenderPass::Main);

    if (msaaSamples > 0 && msaaFBO != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, finalOutputFb->getHandle());
        glBlitFramebuffer(0, 0, viewPortWidth, viewPortHeight, 0, 0, viewPortWidth, viewPortHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        finalOutputFb->unbind();
    }

    sceneDirty = false;
}

void Viewport::update(float deltaTime) {
    (void)deltaTime;
}

void Viewport::movePOVCamera(float deltaZ, float deltaTime) {
    if (!povMode || deltaZ == 0.0f)
        return;
    sceneDirty = true;
    float boost = 1.0f;
    povPos += static_cast<int>(1000.0f * deltaTime * boost * deltaZ);
}

void Viewport::moveCamera(glm::vec3 delta) {
    if (povMode || (delta.x == 0.0f && delta.y == 0.0f && delta.z == 0.0f))
        return;

    sceneDirty = true;
    if (viewMode == ViewMode::Perspective) {
        freeFlyPos += delta.z * freeFlyDir;
        freeFlyPos += delta.x * freeFlySide;
        freeFlyPos += delta.y * glm::cross(freeFlySide, freeFlyDir);
    }
}

void Viewport::panCamera(float dx, float dy) {
    if (povMode || viewMode == ViewMode::Perspective || (dx == 0.0f && dy == 0.0f))
        return;

    sceneDirty = true;
    glm::vec3 up_vec(0.0f, 1.0f, 0.0f);
    if (viewMode == ViewMode::Top)
        up_vec = glm::vec3(-1.0f, 0.0f, 0.0f);

    glm::vec3 right_vec = glm::normalize(glm::cross(freeFlyDir, up_vec));

    float scale = orthoScale / (float)viewPortHeight;
    freeFlyPos += dx * right_vec * scale;
    freeFlyPos -= dy * up_vec * scale;
}

void Viewport::rotateCamera(float yaw, float pitch) {
    if (povMode || viewMode != ViewMode::Perspective || (yaw == 0.0f && pitch == 0.0f))
        return;

    sceneDirty = true;
    float rotateX = TO_RAD(yaw);
    float rotateY = TO_RAD(pitch);

    glm::vec3 up = glm::vec3(0, 1, 0);

    freeFlyDir = glm::angleAxis(rotateX, up) * freeFlyDir;
    freeFlySide = glm::angleAxis(rotateX, up) * freeFlySide;
    freeFlyDir = glm::angleAxis(rotateY, freeFlySide) * freeFlyDir;

    freeFlyDir = glm::normalize(freeFlyDir);
    freeFlySide = glm::normalize(glm::cross(freeFlyDir, up));
}

void Viewport::zoomCamera(float delta) {
    if (povMode || delta == 0.0f)
        return;
    sceneDirty = true;
    if (viewMode != ViewMode::Perspective) {
        orthoScale -= delta;
        if (orthoScale < 0.1f)
            orthoScale = 0.1f;
    } else {
        freeFlyPos += freeFlyDir * delta;
    }
}
void Viewport::captureScreenshot(int multiplier, const std::string& path, const std::vector<trackHandler*>& trackList) {
    int originalWidth = viewPortWidth;
    int originalHeight = viewPortHeight;
    int hiResWidth = originalWidth * multiplier;
    int hiResHeight = originalHeight * multiplier;

    resize(hiResWidth, hiResHeight);
    render(trackList);

    finalOutputFb->bind();
    std::vector<unsigned char> pixels(hiResWidth * hiResHeight * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, hiResWidth, hiResHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4); // Restore default
    finalOutputFb->unbind();

    stbi_flip_vertically_on_write(true);
    stbi_write_png(path.c_str(), hiResWidth, hiResHeight, 3, pixels.data(), hiResWidth * 3);

    resize(originalWidth, originalHeight);
}

GLuint Viewport::getOutputTexture() {
    return finalOutputFb->getTextureHandle();
}

void Viewport::initShaders() {
    skyShader = new ShaderProgram("shaders/sky.vert", "shaders/sky.frag");
    skyShader->useAttribute(0, "aPosition");
    skyShader->linkProgram();

    floorShader = new ShaderProgram("shaders/floor.vert", "shaders/floor.frag");
    floorShader->useAttribute(0, "aPosition");
    floorShader->useAttribute(1, "aNormal");
    floorShader->linkProgram();

    trackShader = new ShaderProgram("shaders/track.vert", "shaders/track.frag");
    trackInstancedShader = new ShaderProgram("shaders/track_instanced.vert", "shaders/track.frag");
    trackShader->useAttribute(0, "aPosition");
    trackShader->useAttribute(1, "aVel");
    trackShader->useAttribute(2, "aRoll");
    trackShader->useAttribute(3, "aNForce");
    trackShader->useAttribute(4, "aLForce");
    trackShader->useAttribute(5, "aFlex");
    trackShader->useAttribute(6, "aselected");
    trackShader->useAttribute(7, "aNormal");
    trackShader->linkProgram();

    trackInstancedShader->useAttribute(0, "aPosition");
    trackInstancedShader->useAttribute(1, "aNormal");
    trackInstancedShader->useAttribute(2, "aUv");
    trackInstancedShader->useAttribute(3, "aInstanceMatrix");
    trackInstancedShader->useAttribute(7, "aAttributes1");
    trackInstancedShader->useAttribute(8, "aAttributes2");
    trackInstancedShader->linkProgram();

    normalMapShader = new ShaderProgram("shaders/normals.vert", "shaders/normals.frag");
    normalMapShader->useAttribute(0, "aPosition");
    normalMapShader->useAttribute(7, "aNormal");
    normalMapShader->linkProgram();

    simpleShadowShader = new ShaderProgram("shaders/simple_shadow.vert", "shaders/simple_shadow.frag");
    simpleShadowShader->useAttribute(0, "aPosition");
    simpleShadowShader->useAttribute(3, "aInstanceMatrix");
    simpleShadowShader->linkProgram();

    stlShader = new ShaderProgram("shaders/stl.vert", "shaders/stl.frag");
    stlShader->useAttribute(0, "aPosition");
    stlShader->useAttribute(7, "aNormal");
    stlShader->linkProgram();
}

bool Viewport::addStlMesh(const std::string& path) {
    std::vector<Triangle> triangles;
    if (!readStl(path, triangles)) {
        return false;
    }

    std::vector<glm::dvec3> vertices = extractVerticesNormal(triangles);
    if (vertices.empty())
        return false;

    StlMesh sm;
    sm.path = path;
    sm.color = glm::vec3(0.7f, 0.7f, 0.7f);
    sm.visible = true;
    sm.showWireframe = false;
    sm.vertexCount = (int)vertices.size() / 2;

    glGenVertexArrays(1, &sm.mesh.object);
    glGenBuffers(1, &sm.mesh.buffer);
    glBindVertexArray(sm.mesh.object);
    glBindBuffer(GL_ARRAY_BUFFER, sm.mesh.buffer);

    std::vector<float> bufferData;
    bufferData.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        bufferData.push_back((float)v.x);
        bufferData.push_back((float)v.y);
        bufferData.push_back((float)v.z);
    }

    glBufferData(GL_ARRAY_BUFFER, bufferData.size() * sizeof(float), bufferData.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(7);

    glBindVertexArray(0);
    stlMeshes.push_back(sm);
    sceneDirty = true;
    return true;
}

void Viewport::removeStlMesh(int index) {
    if (index < 0 || index >= (int)stlMeshes.size())
        return;

    glDeleteVertexArrays(1, &stlMeshes[index].mesh.object);
    glDeleteBuffers(1, &stlMeshes[index].mesh.buffer);
    stlMeshes.erase(stlMeshes.begin() + index);
    sceneDirty = true;
}

void Viewport::initTextures() {
#define RASTER_SIZE (600)
    unsigned char* rasterData = new unsigned char[RASTER_SIZE * RASTER_SIZE * 4];
    for (int i = 0; i < RASTER_SIZE; ++i) {
        for (int j = 0; j < RASTER_SIZE; ++j) {
            float major = std::min(fabsf(i - RASTER_SIZE / 2.0f), fabsf(j - RASTER_SIZE / 2.0f)) / (RASTER_SIZE / 2.0f);
            float minor = std::min((float)std::min(fabsf(((i + (RASTER_SIZE / 20)) % (RASTER_SIZE / 10) - (RASTER_SIZE / 20.0f))),
                                                   fabsf(((j + (RASTER_SIZE / 20)) % (RASTER_SIZE / 10) - (RASTER_SIZE / 20.0f)))),
                                   1.0f);
            major = 1.0f - major;
            minor = 1.0f - minor;
            for (int k = 0; k < 9; ++k) {
                major *= major;
                minor *= minor;
            }
            major = 1.0f - major;
            minor = 1.0f - 0.5f * minor;
            int col = (int)(std::min(major, minor) * 255.0f);
            int idx = (i * RASTER_SIZE + j) * 4;
            rasterData[idx + 0] = col;
            rasterData[idx + 1] = col;
            rasterData[idx + 2] = col;
            rasterData[idx + 3] = 255;
        }
    }
    rasterTexture = new GlTexture(rasterData, RASTER_SIZE, RASTER_SIZE, 1);
    delete[] rasterData;

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    floorTexture = new GlTexture(whitePixel, 1, 1, 1);

    bool customSkybox = true;
    for (int i = 0; i < 6; ++i) {
        std::string path = "skybox/cubemap_" + std::to_string(i) + ".png";
        if (!std::filesystem::exists(path)) {
            customSkybox = false;
            break;
        }
    }

    gloParent->skyboxAvailable = customSkybox;

    if (customSkybox) {
        skyTexture = new GlTexture("skybox/cubemap_0.png", "skybox/cubemap_1.png", "skybox/cubemap_2.png",
                                   "skybox/cubemap_3.png", "skybox/cubemap_4.png", "skybox/cubemap_5.png");
    } else {
        skyTexture = nullptr;
    }
}

void Viewport::initFloorMesh() {
    glGenVertexArrays(1, &floorMesh.object);
    glGenBuffers(1, &floorMesh.buffer);
    glBindVertexArray(floorMesh.object);
    glBindBuffer(GL_ARRAY_BUFFER, floorMesh.buffer);

    float a = 1000.f;
    float floor[4 * 6] = {
        -a, 0.f, -a, 0.f, 1.0f, 0.f,
        -a, 0.f, +a, 0.f, 1.0f, 0.f,
        +a, 0.f, -a, 0.f, 1.0f, 0.f,
        +a, 0.f, +a, 0.f, 1.0f, 0.f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(floor), floor, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    glGenVertexArrays(1, &skyMesh.object);
    glGenBuffers(1, &skyMesh.buffer);
    glBindVertexArray(skyMesh.object);
    glBindBuffer(GL_ARRAY_BUFFER, skyMesh.buffer);
    float sky[12] = {1.f, 1.f, 0.9999f, 1.f, -1.f, 0.9999f, -1.f, 1.f, 0.9999f, -1.f, -1.f, 0.9999f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(sky), sky, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
    glEnableVertexAttribArray(0);

    glGenVertexArrays(1, &markerMesh.object);
    glGenBuffers(1, &markerMesh.buffer);
    glBindVertexArray(markerMesh.object);
    glBindBuffer(GL_ARRAY_BUFFER, markerMesh.buffer);

    std::vector<float> markerData;
    std::vector<GLuint> indices;
    float radius = 0.15f;
    int rings = 8;
    int sectors = 12;
    for (int r = 0; r <= rings; ++r) {
        float v = (float)r / rings;
        float phi = v * F_PI;
        for (int s = 0; s <= sectors; ++s) {
            float u = (float)s / sectors;
            float theta = u * F_PI * 2.0f;
            float x = radius * cos(theta) * sin(phi);
            float y = radius * cos(phi);
            float z = radius * sin(theta) * sin(phi);
            markerData.push_back(x);
            markerData.push_back(y);
            markerData.push_back(z);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            int i0 = r * (sectors + 1) + s;
            int i1 = i0 + 1;
            int i2 = (r + 1) * (sectors + 1) + s;
            int i3 = i2 + 1;
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    glGenBuffers(1, &markerMesh.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, markerMesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    glBufferData(GL_ARRAY_BUFFER, markerData.size() * sizeof(float), markerData.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(7);

    // Initialize orthographic grid mesh VAO/VBO
    glGenVertexArrays(1, &orthoGridMesh.object);
    glGenBuffers(1, &orthoGridMesh.buffer);
    glBindVertexArray(orthoGridMesh.object);
    glBindBuffer(GL_ARRAY_BUFFER, orthoGridMesh.buffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    markerMesh.count = indices.size();
}

void Viewport::buildMatrices(float offset) {
    float aspect = (viewPortWidth > 0) ? (float)viewPortWidth / viewPortHeight : 1.0f;

    if (viewMode != ViewMode::Perspective) {
        float halfHeight = orthoScale * 0.5f;
        float halfWidth = halfHeight * aspect;
        ProjectionMatrix = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -2000.0f, 2000.0f);
    } else {
        float side = static_cast<float>(tan(fov * 3.14159265f / 360.0f)) * 0.1f;
        float invAspect = 1.0f / aspect;
        ProjectionMatrix = glm::frustum(-side, side, -invAspect * side,
                                        invAspect * side, 0.1f, 4000.0f);
    }

    if (activeTrack && activeTrack->trackData && activeTrack->trackData->lSections.size() > 0) {
        track* curTrack = activeTrack->trackData;

        if (povPos < 0)
            povPos += curTrack->getNumPoints();
        if (povPos > curTrack->getNumPoints())
            povPos = 0;

        povNode = curTrack->getPoint(povPos);

        if (povMode && povNode) {
            glm::mat4 anchorBase = glm::translate(glm::mat4(1.0f), glm::vec3(curTrack->startPos)) *
                                   glm::rotate(glm::mat4(1.0f), (float)TO_RAD(curTrack->startYaw - 90.f), glm::vec3(0.f, 1.f, 0.f));
            glm::dvec3 posD = povNode->vRelPos(curTrack->povPos.y, curTrack->povPos.x);
            glm::vec3 pos = glm::vec3(posD);

            int nextPos = povPos + 1;
            mnode* nextNode = nullptr;
            glm::dvec3 nextPD;
            glm::dvec3 diff(0.0);

            // Search forward to find a node sufficiently far away to prevent precision jitter
            while (nextPos < curTrack->getNumPoints()) {
                nextNode = curTrack->getPoint(nextPos);
                if (nextNode) {
                    nextPD = nextNode->vRelPos(curTrack->povPos.y, curTrack->povPos.x);
                    diff = nextPD - posD;
                    if (glm::length(diff) > 0.05) { // 5cm minimum distance for stable vector
                        break;
                    }
                }
                nextPos++;
            }

            glm::vec3 direction;
            if (glm::length(diff) > 0.05) {
                direction = glm::normalize(glm::vec3(diff));
            } else {
                direction = glm::vec3(povNode->vDirHeart(curTrack->fHeart));
            }
            glm::vec3 upVec = -glm::vec3(povNode->vNorm);
            glm::vec3 side = glm::normalize(glm::cross(direction, upVec));
            glm::vec3 down = glm::cross(direction, side);

            glm::vec3 worldPos = glm::vec3(anchorBase * glm::vec4(pos, 1.0f));
            glm::vec3 worldTarget = glm::vec3(anchorBase * glm::vec4(pos + direction, 1.0f));
            glm::vec3 worldUp = -glm::vec3(anchorBase * glm::vec4(down, 0.0f));

            worldPos += worldUp * povHeightOffset;
            worldTarget += worldUp * povHeightOffset;

            ModelMatrix = glm::lookAt(worldPos, worldTarget, worldUp);
            cameraPos = worldPos;
        } else {
            cameraPos = freeFlyPos;
            glm::vec3 upVec(0.0f, 1.0f, 0.0f);
            if (viewMode == ViewMode::Top)
                upVec = glm::vec3(-1.0f, 0.0f, 0.0f);
            ModelMatrix = glm::lookAt(cameraPos, cameraPos + freeFlyDir, upVec);
        }
    } else {
        povMode = false;
        povNode = nullptr;
        cameraPos = freeFlyPos;
        glm::vec3 upVec(0.0f, 1.0f, 0.0f);
        if (viewMode == ViewMode::Top)
            upVec = glm::vec3(-1.0f, 0.0f, 0.0f);
        ModelMatrix = glm::lookAt(cameraPos, cameraPos + freeFlyDir, upVec);
    }

    ProjectionModelMatrix = ProjectionMatrix * ModelMatrix;

    // Calculate Planar Shadow Matrix (Projects onto Y=0.01 plane to prevent Z-fighting)
    // Formula: S = I - (L * N) / (N . L) where N is plane normal and L is light direction
    glm::vec3 L = glm::normalize(lightDir);
    glm::vec3 N = glm::vec3(0.0f, 1.0f, 0.0f);
    float dotNL = glm::dot(N, L);

    // Calculate Planar Shadow Matrix (Projects onto Y=0.01 plane to prevent Z-fighting)
    shadowMatrix = glm::mat4(1.0f);
    if (std::abs(dotNL) > 0.0001f) {
        shadowMatrix[0][0] = 1.0f;
        shadowMatrix[1][0] = -L.x / L.y;
        shadowMatrix[2][0] = 0.0f;
        shadowMatrix[3][0] = 0.01f * (L.x / L.y); // Small bias

        shadowMatrix[0][1] = 0.0f;
        shadowMatrix[1][1] = 0.0f;
        shadowMatrix[2][1] = 0.0f;
        shadowMatrix[3][1] = 0.01f;

        shadowMatrix[0][2] = 0.0f;
        shadowMatrix[1][2] = -L.z / L.y;
        shadowMatrix[2][2] = 1.0f;
        shadowMatrix[3][2] = 0.01f * (L.z / L.y); // Small bias

        shadowMatrix[0][3] = 0.0f;
        shadowMatrix[1][3] = 0.0f;
        shadowMatrix[2][3] = 0.0f;
        shadowMatrix[3][3] = 1.0f;
    }
}

void Viewport::focusOnSection(int sectionIdx) {
    if (!activeTrack || !activeTrack->trackData)
        return;
    track* curTrack = activeTrack->trackData;

    glm::dvec3 startPos(0.0);
    glm::dvec3 direction(0.0, 0.0, 1.0);

    if (sectionIdx == -1) {
        startPos = curTrack->anchorNode->vPosHeart(curTrack->fHeart);
        direction = curTrack->anchorNode->vDirHeart(curTrack->fHeart);
    } else if (sectionIdx >= 0 && sectionIdx < (int)curTrack->lSections.size()) {
        section* sec = curTrack->lSections[sectionIdx];
        if (!sec->lNodes.empty()) {
            startPos = sec->lNodes[0].vPosHeart(curTrack->fHeart);
            direction = sec->lNodes[0].vDirHeart(curTrack->fHeart);
        } else {
            if (sectionIdx == 0) {
                startPos = curTrack->anchorNode->vPosHeart(curTrack->fHeart);
                direction = curTrack->anchorNode->vDirHeart(curTrack->fHeart);
            } else {
                section* prevSec = curTrack->lSections[sectionIdx - 1];
                if (!prevSec->lNodes.empty()) {
                    startPos = prevSec->lNodes.back().vPosHeart(curTrack->fHeart);
                    direction = prevSec->lNodes.back().vDirHeart(curTrack->fHeart);
                }
            }
        }
    }

    glm::mat4 anchorBase = glm::translate(glm::mat4(1.0f), glm::vec3(curTrack->startPos)) *
                           glm::rotate(glm::mat4(1.0f), (float)TO_RAD(curTrack->startYaw - 90.f), glm::vec3(0.f, 1.f, 0.f));

    glm::vec3 worldPos = glm::vec3(anchorBase * glm::vec4(glm::vec3(startPos), 1.0f));
    glm::vec3 worldDir = glm::normalize(glm::vec3(anchorBase * glm::vec4(glm::vec3(direction), 0.0f)));

    povMode = false;

    if (viewMode == ViewMode::Perspective) {
        freeFlyPos = worldPos - worldDir * 20.0f + glm::vec3(0, 8, 0);
        freeFlyDir = glm::normalize(worldPos - freeFlyPos);
        freeFlySide = glm::normalize(glm::cross(freeFlyDir, glm::vec3(0, 1, 0)));
    } else if (viewMode == ViewMode::Top) {
        freeFlyPos = worldPos + glm::vec3(0, 100, 0);
        freeFlyDir = glm::vec3(0, -1, 0);
        freeFlySide = glm::vec3(0, 0, -1);
        orthoScale = 50.0f;
    } else if (viewMode == ViewMode::Side) {
        freeFlyPos = worldPos + glm::vec3(-100, 0, 0);
        freeFlyDir = glm::vec3(1, 0, 0);
        freeFlySide = glm::vec3(0, 0, -1);
        orthoScale = 50.0f;
    } else if (viewMode == ViewMode::Front) {
        freeFlyPos = worldPos + glm::vec3(0, 0, 100);
        freeFlyDir = glm::vec3(0, 0, -1);
        freeFlySide = glm::vec3(-1, 0, 0);
        orthoScale = 50.0f;
    }
}

void Viewport::drawSky() {
    glDepthMask(GL_FALSE);
    skyShader->bind();

    skyShader->useUniform("mistEnabled", (GLuint)(gloParent->mOptions->mistEnabled ? 1 : 0));
    skyShader->useUniform("mistNear", gloParent->mOptions->mistNear);
    skyShader->useUniform("mistFar", gloParent->mOptions->mistFar);
    skyShader->useUniform("mistColor", gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b);
    skyShader->useUniform("eyePos", cameraPos.x, cameraPos.y, cameraPos.z);

    if (skyTexture && gloParent->mOptions->skyboxEnabled) {
        skyShader->useUniform("hasTexture", (GLuint)1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyTexture->getHandle());
        skyShader->useUniform("skyTex", (GLuint)0);
    } else {
        skyShader->useUniform("hasTexture", (GLuint)0);
        skyShader->useUniform("fallbackColor", gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b);
    }

    glm::mat4 invV = glm::inverse(glm::mat4(glm::mat3(ModelMatrix)));
    glm::mat4 invP = glm::inverse(ProjectionMatrix);
    glm::mat4 invPV = invV * invP;

    glm::vec4 tl = invPV * glm::vec4(-1, 1, 1, 1);
    glm::vec3 topLeft = glm::normalize(glm::vec3(tl));

    glm::vec4 tr = invPV * glm::vec4(1, 1, 1, 1);
    glm::vec3 topRight = glm::normalize(glm::vec3(tr));

    glm::vec4 bl = invPV * glm::vec4(-1, -1, 1, 1);
    glm::vec3 bottomLeft = glm::normalize(glm::vec3(bl));

    glm::vec4 br = invPV * glm::vec4(1, -1, 1, 1);
    glm::vec3 bottomRight = glm::normalize(glm::vec3(br));

    skyShader->useUniform("TL", topLeft.x, topLeft.y, topLeft.z);
    skyShader->useUniform("TR", topRight.x, topRight.y, topRight.z);
    skyShader->useUniform("BL", bottomLeft.x, bottomLeft.y, bottomLeft.z);
    skyShader->useUniform("BR", bottomRight.x, bottomRight.y, bottomRight.z);

    glBindVertexArray(skyMesh.object);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDepthMask(GL_TRUE);
}

bool Viewport::loadGroundTexture(const std::string& path) {
    if (floorTexture) {
        delete floorTexture;
        floorTexture = nullptr;
    }

    sceneDirty = true;
    if (!path.empty()) {
        floorTexture = new GlTexture(path.c_str(), 2);
        if (floorTexture->getHandle() != 0) {
            return true;
        }
        delete floorTexture;
    }

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    floorTexture = new GlTexture(whitePixel, 1, 1, 1);
    return false;
}

void Viewport::drawFloor() {
    floorShader->bind();
    floorShader->useUniform("projectionMatrix", &ProjectionMatrix);
    floorShader->useUniform("modelMatrix", &ModelMatrix);
    floorShader->useUniform("eyePos", &cameraPos);
    floorShader->useUniform("lightSpaceMatrix", &lightSpaceMatrix);

    floorShader->useUniform("mistEnabled", (GLuint)(gloParent->mOptions->mistEnabled ? 1 : 0));
    floorShader->useUniform("mistNear", gloParent->mOptions->mistNear);
    floorShader->useUniform("mistFar", gloParent->mOptions->mistFar);
    floorShader->useUniform("mistColor", gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b);
    floorShader->useUniform("floorColor", gloParent->mOptions->floorColor.r, gloParent->mOptions->floorColor.g, gloParent->mOptions->floorColor.b);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, rasterTexture->getHandle());
    floorShader->useUniform("rasterTex", (GLuint)2);

    if (floorTexture) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, floorTexture->getHandle());
        floorShader->useUniform("floorTex", (GLuint)1);
    }
    floorShader->useUniform("grdTexSize", grdTexSize);

    floorShader->useUniform("border", (GLuint)1);
    floorShader->useUniform("grid", (GLuint)(gloParent->mOptions->drawGrid ? 1 : 0));
    floorShader->useUniform("shadowMode", (GLuint)shadowMode);
    floorShader->useUniform("opacity", 1.0f);
    glBindVertexArray(floorMesh.object);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Viewport::drawMarkers(RenderPass pass) {
    if (pass != RenderPass::Main || !activeTrack || !activeTrack->trackData)
        return;
    if (activeTrack->trackData->drawHeartline == 3 || povMode)
        return;

    ShaderProgram* shader = stlShader;
    shader->bind();

    shader->useUniform("projectionMatrix", &ProjectionMatrix);
    shader->useUniform("modelMatrix", &ModelMatrix);
    shader->useUniform("eyePos", &cameraPos);
    shader->useUniform("lightDir", &lightDir);
    shader->useUniform("wire", (GLuint)0);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, occlusionFb->getTextureHandle());
    shader->useUniform("occlusionTex", (GLuint)2);

    shader->useUniform("mistEnabled", (GLuint)(gloParent->mOptions->mistEnabled ? 1 : 0));
    shader->useUniform("mistNear", gloParent->mOptions->mistNear);
    shader->useUniform("mistFar", gloParent->mOptions->mistFar);
    shader->useUniform("mistColor", gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_CUBE_MAP, skyTexture ? skyTexture->getHandle() : 0);
    shader->useUniform("skyTex", (GLuint)4);

    track* myTrack = activeTrack->trackData;
    glm::mat4 baseAnchorBase = glm::translate(glm::mat4(1.0f), glm::vec3(myTrack->startPos)) *
                               glm::rotate(glm::mat4(1.0f), (float)TO_RAD(myTrack->startYaw - 90.f), glm::vec3(0.f, 1.f, 0.f));

    glBindVertexArray(markerMesh.object);

    for (size_t i = 0; i < myTrack->trainOffsets.size(); ++i) {
        const auto& offset = myTrack->trainOffsets[i];
        int totalPoints = myTrack->getNumPoints();
        if (totalPoints == 0)
            continue;

        mnode* povNode = getPOVNode();
        double povDist = 0.0;
        if (povNode) {
            povDist = povNode->fTotalLength;
        }

        double targetDist = povDist + offset.offset.z;
        int searchIdx = myTrack->getIndexFromDist(targetDist);

        mnode* targetNode = myTrack->getPoint(searchIdx);
        if (!targetNode)
            continue;

        glm::vec3 pos = targetNode->vPos + targetNode->vLat * (double)offset.offset.x - targetNode->vNorm * (double)offset.offset.y;

        float markerScale = 1.0f;
        glm::vec3 markerColor = offset.color;

        if (myTrack->forceHighlightMode == 1) {
            if ((int)i == myTrack->maxNormalOffsetIdx) {
                markerScale = 2.0f;
                markerColor = glm::vec3(1.0f, 0.0f, 0.0f);
            } else if ((int)i == myTrack->minNormalOffsetIdx) {
                markerScale = 2.0f;
                markerColor = glm::vec3(0.0f, 0.0f, 1.0f);
            } else {
                markerColor *= 0.2f;
            }
        } else if (myTrack->forceHighlightMode == 2) {
            if ((int)i == myTrack->maxLateralOffsetIdx) {
                markerScale = 2.0f;
                markerColor = glm::vec3(1.0f, 0.0f, 0.0f);
            } else if ((int)i == myTrack->minLateralOffsetIdx) {
                markerScale = 2.0f;
                markerColor = glm::vec3(0.0f, 0.0f, 1.0f);
            } else {
                markerColor *= 0.2f;
            }
        }

        glm::mat4 currentAnchorBase = baseAnchorBase * glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(markerScale));
        shader->useUniform("anchorBase", &currentAnchorBase);
        shader->useUniform("solidColor", markerColor.x, markerColor.y, markerColor.z);

        glDrawElements(GL_TRIANGLES, markerMesh.count, GL_UNSIGNED_INT, 0);
    }

    if (showPOVMarker3D && !povMode) {
        mnode* povNode = getPOVNode();
        if (povNode) {
            glm::vec3 pos = povNode->vPos;
            glm::mat4 currentAnchorBase = baseAnchorBase * glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.7f));
            shader->useUniform("anchorBase", &currentAnchorBase);
            shader->useUniform("solidColor", 0.0f, 0.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, markerMesh.count, GL_UNSIGNED_INT, 0);
        }
    }

    glBindVertexArray(0);
}

void Viewport::drawStls(RenderPass pass) {
    if (stlMeshes.empty() || pass == RenderPass::NormalMap)
        return;

    ShaderProgram* shader = nullptr;
    if (pass == RenderPass::PlanarShadow) {
        shader = simpleShadowShader;
    } else {
        shader = stlShader;
    }
    shader->bind();

    glm::mat4 identity = glm::mat4(1.0f);
    if (pass == RenderPass::PlanarShadow) {
        shader->useUniform("projectionMatrix", &ProjectionMatrix);
        shader->useUniform("modelMatrix", &ModelMatrix);
        shader->useUniform("shadowMatrix", &shadowMatrix);
        shader->useUniform("anchorBase", &identity);
        shader->useUniform("isInstanced", (GLuint)0);
    } else {
        shader->useUniform("projectionMatrix", &ProjectionMatrix);
        shader->useUniform("modelMatrix", &ModelMatrix);
        shader->useUniform("anchorBase", &identity);
        shader->useUniform("eyePos", &cameraPos);
        shader->useUniform("lightDir", &lightDir);
    }

    if (pass == RenderPass::Main) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, occlusionFb->getTextureHandle());
        shader->useUniform("occlusionTex", (GLuint)2);

        shader->useUniform("mistEnabled", (GLuint)(gloParent->mOptions->mistEnabled ? 1 : 0));
        shader->useUniform("mistNear", gloParent->mOptions->mistNear);
        shader->useUniform("mistFar", gloParent->mOptions->mistFar);
        shader->useUniform("mistColor", gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b);

        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP, skyTexture ? skyTexture->getHandle() : 0);
        shader->useUniform("skyTex", (GLuint)4);

        shader->useUniform("edgeWidth", 0.006f);
    }

    for (const auto& sm : stlMeshes) {
        if (!sm.visible)
            continue;
        if (pass == RenderPass::Main) {
            shader->useUniform("solidColor", sm.color.x, sm.color.y, sm.color.z);
            shader->useUniform("wire", (GLuint)(sm.showWireframe ? 1 : 0));
        }

        glBindVertexArray(sm.mesh.object);
        glDrawArrays(GL_TRIANGLES, 0, sm.vertexCount);
    }
}

void Viewport::drawTrack(trackHandler* hTrack, RenderPass pass) {
    if (pass == RenderPass::NormalMap)
        return;
    if (hTrack->trackData->drawHeartline == 3)
        return;

    ShaderProgram* shader = nullptr;
    if (pass == RenderPass::PlanarShadow) {
        shader = simpleShadowShader;
    } else {
        shader = trackShader;
    }

    if (!hTrack->mMesh->isInit)
        hTrack->mMesh->init();

    trackMesh* mesh = hTrack->mMesh;
    track* myTrack = hTrack->trackData;

    glm::mat4 anchorBase = glm::translate(glm::mat4(1.0f), glm::vec3(myTrack->startPos)) *
                           glm::rotate(glm::mat4(1.0f), (float)TO_RAD(myTrack->startYaw - 90.f), glm::vec3(0.f, 1.f, 0.f));

    shader->bind();

    if (pass == RenderPass::PlanarShadow) {
        shader->useUniform("projectionMatrix", &ProjectionMatrix);
        shader->useUniform("modelMatrix", &ModelMatrix);
        shader->useUniform("shadowMatrix", &shadowMatrix);
        shader->useUniform("anchorBase", &anchorBase);
        shader->useUniform("isInstanced", (GLuint)0);
    } else {
        shader->useUniform("projectionMatrix", &ProjectionMatrix);
        shader->useUniform("modelMatrix", &ModelMatrix);
        shader->useUniform("anchorBase", &anchorBase);
        shader->useUniform("eyePos", &cameraPos);
        shader->useUniform("lightDir", &lightDir);
    }

    // Bind Spline SSBO (Binding point 0)
    if (mesh->SplineBuffer != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mesh->SplineBuffer);
    }

    if (pass == RenderPass::Main) {
        shader->useUniform("colorMode", (GLuint)curTrackShader);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, occlusionFb->getTextureHandle());
        shader->useUniform("occlusionTex", (GLuint)2);

        shader->useUniform("mistEnabled", (GLuint)(gloParent->mOptions->mistEnabled ? 1 : 0));
        shader->useUniform("mistNear", gloParent->mOptions->mistNear);
        shader->useUniform("mistFar", gloParent->mOptions->mistFar);
        shader->useUniform("mistColor", gloParent->mOptions->mistColor.r, gloParent->mOptions->mistColor.g, gloParent->mOptions->mistColor.b);

        shader->useUniform("shadowMode", (GLuint)shadowMode);

        shader->useUniform("trackShine", 120.0f);
        shader->useUniform("defaultColor", hTrack->trackColors[0].r, hTrack->trackColors[0].g, hTrack->trackColors[0].b);
        shader->useUniform("sectionColor", hTrack->trackColors[1].r, hTrack->trackColors[1].g, hTrack->trackColors[1].b);
        shader->useUniform("transitionColor", hTrack->trackColors[2].r, hTrack->trackColors[2].g, hTrack->trackColors[2].b);
    }

    if (myTrack->drawHeartline != 2 && !myTrack->lSections.empty()) {
        if (!mesh->customStyle && !mesh->pipeBorders.empty() && mesh->TrackObject[0] != 0) {
            glBindVertexArray(mesh->TrackObject[0]);
            for (int i = 0; i < (int)mesh->pipeBorders.size() - 1; ++i) {
                glDrawElements(GL_TRIANGLE_STRIP, mesh->pipeBorders[i + 1] - mesh->pipeBorders[i],
                               GL_UNSIGNED_INT, (GLvoid*)(sizeof(GLuint) * mesh->pipeBorders[i]));
            }
        }

        if (!mesh->crossties.empty() && mesh->TrackObject[3] != 0) {
            glBindVertexArray(mesh->TrackObject[3]);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh->crossties.size()));
        }

        // Draw instanced custom assets and native extrusions
        if (!mesh->instancedAssets.empty() || !mesh->instancedExtrusions.empty()) {
            ShaderProgram* iShader = (pass == RenderPass::PlanarShadow) ? simpleShadowShader : trackInstancedShader;
            iShader->bind();

            iShader->useUniform("uTrackLength", (float)myTrack->getTotalLength());
            iShader->useUniform("heartline", (float)myTrack->fHeart);

            if (pass == RenderPass::PlanarShadow) {
                iShader->useUniform("projectionMatrix", &ProjectionMatrix);
                iShader->useUniform("modelMatrix", &ModelMatrix);
                iShader->useUniform("shadowMatrix", &shadowMatrix);
                iShader->useUniform("anchorBase", &anchorBase);
                iShader->useUniform("isInstanced", (GLuint)1);
            } else {
                iShader->useUniform("projectionMatrix", &ProjectionMatrix);
                iShader->useUniform("modelMatrix", &ModelMatrix);
                iShader->useUniform("anchorBase", &anchorBase);
                iShader->useUniform("eyePos", &cameraPos);
                iShader->useUniform("lightDir", &lightDir);

                if (pass == RenderPass::Main) {
                    iShader->useUniform("colorMode", (GLuint)curTrackShader);
                    iShader->useUniform("defaultColor", hTrack->trackColors[0].r, hTrack->trackColors[0].g, hTrack->trackColors[0].b);
                    iShader->useUniform("sectionColor", hTrack->trackColors[1].r, hTrack->trackColors[1].g, hTrack->trackColors[1].b);
                    iShader->useUniform("transitionColor", hTrack->trackColors[2].r, hTrack->trackColors[2].g, hTrack->trackColors[2].b);
                    iShader->useUniform("trackShine", 120.0f);

                    glActiveTexture(GL_TEXTURE2);
                    glBindTexture(GL_TEXTURE_2D, occlusionFb->getTextureHandle());
                    iShader->useUniform("occlusionTex", (GLuint)2);
                }
            }

            for (size_t assetIdx = 0; assetIdx < mesh->instancedAssets.size(); ++assetIdx) {
                auto& am = mesh->instancedAssets[assetIdx];
                if (am.vao != 0 && !am.instances.empty()) {
                    if (assetIdx < myTrack->customAssets.size()) {
                        const auto& asset = myTrack->customAssets[assetIdx];
                        if (!asset.visible)
                            continue;
                    }
                    iShader->useUniform("isAsset", (GLuint)1);
                    glBindVertexArray(am.vao);
                    glDrawElementsInstanced(GL_TRIANGLES, am.indexCount, GL_UNSIGNED_INT, 0, (GLsizei)am.instances.size());
                }
            }
            for (auto& am : mesh->instancedExtrusions) {
                if (am.vao != 0 && !am.instances.empty()) {
                    iShader->useUniform("isAsset", (GLuint)0);
                    glBindVertexArray(am.vao);
                    glDrawElementsInstanced(GL_TRIANGLES, am.indexCount, GL_UNSIGNED_INT, 0, (GLsizei)am.instances.size());
                }
            }
            shader->bind(); // switch back to base shader
        }

        if (mesh->supportsSize > 0 && mesh->TrackObject[4] != 0) {
            glBindVertexArray(mesh->TrackObject[4]);
            if (pass == RenderPass::PlanarShadow) {
                shader->bind();
                shader->useUniform("isInstanced", (GLuint)0);
            } else {
                shader->useUniform("defaultColor", 0.6f, 0.6f, 0.6f);
            }
            for (int i = 0; i < mesh->supportsSize; ++i) {
                glDrawArrays(GL_TRIANGLE_STRIP, i * 61, 61);
            }
        }
    }

    if (pass == RenderPass::Main) {
        shader->useUniform("defaultColor", 0.9f, 0.9f, 0.4f);
        if (myTrack->drawHeartline != 1 && !mesh->heartline.empty() && mesh->HeartObject[0] != 0) {
            glBindVertexArray(mesh->HeartObject[0]);
            glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(mesh->heartline.size()));
        }
    }
}

void Viewport::drawOrthoGrid() {
    if (viewMode == ViewMode::Perspective)
        return;
    if (!gloParent->mOptions->drawGrid)
        return;

    std::vector<float> minorVertices;
    std::vector<float> majorVertices;
    std::vector<float> xAxisVertices;
    std::vector<float> yAxisVertices;
    std::vector<float> zAxisVertices;

    float halfHeight = orthoScale * 0.5f;
    float halfWidth = halfHeight * ((float)viewPortWidth / (float)viewPortHeight);

    // Fixed spacing matching the procedurally generated ground grid:
    // Major grid lines = every 10 meters.
    // Minor grid lines (subgrid) = every 1 meter.
    float spacing = 10.0f;
    float subSpacing = 1.0f;

    // Hide subgrid minor lines when zoomed out too far to prevent high line density
    bool drawMinor = (orthoScale <= 250.0f);

    if (viewMode == ViewMode::Side) {
        // Camera looks along X. We are drawing on YZ plane.
        float startY = std::floor((cameraPos.y - halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endY = std::ceil((cameraPos.y + halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float startZ = std::floor((cameraPos.z - halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endZ = std::ceil((cameraPos.z + halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);

        // Horizontal lines (constant Y):
        for (float y = startY; y <= endY; y += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(y) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(y / spacing) * spacing - y) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                zAxisVertices.push_back(0.0f);
                zAxisVertices.push_back(y);
                zAxisVertices.push_back(startZ);
                zAxisVertices.push_back(0.0f);
                zAxisVertices.push_back(y);
                zAxisVertices.push_back(endZ);
            } else if (isMajor) {
                majorVertices.push_back(0.0f);
                majorVertices.push_back(y);
                majorVertices.push_back(startZ);
                majorVertices.push_back(0.0f);
                majorVertices.push_back(y);
                majorVertices.push_back(endZ);
            } else if (drawMinor) {
                minorVertices.push_back(0.0f);
                minorVertices.push_back(y);
                minorVertices.push_back(startZ);
                minorVertices.push_back(0.0f);
                minorVertices.push_back(y);
                minorVertices.push_back(endZ);
            }
        }
        // Vertical lines (constant Z):
        for (float z = startZ; z <= endZ; z += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(z) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(z / spacing) * spacing - z) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                yAxisVertices.push_back(0.0f);
                yAxisVertices.push_back(startY);
                yAxisVertices.push_back(z);
                yAxisVertices.push_back(0.0f);
                yAxisVertices.push_back(endY);
                yAxisVertices.push_back(z);
            } else if (isMajor) {
                majorVertices.push_back(0.0f);
                majorVertices.push_back(startY);
                majorVertices.push_back(z);
                majorVertices.push_back(0.0f);
                majorVertices.push_back(endY);
                majorVertices.push_back(z);
            } else if (drawMinor) {
                minorVertices.push_back(0.0f);
                minorVertices.push_back(startY);
                minorVertices.push_back(z);
                minorVertices.push_back(0.0f);
                minorVertices.push_back(endY);
                minorVertices.push_back(z);
            }
        }
    } else if (viewMode == ViewMode::Front) {
        // Camera looks along Z. We are drawing on XY plane.
        float startY = std::floor((cameraPos.y - halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endY = std::ceil((cameraPos.y + halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float startX = std::floor((cameraPos.x - halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endX = std::ceil((cameraPos.x + halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);

        // Horizontal lines (constant Y):
        for (float y = startY; y <= endY; y += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(y) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(y / spacing) * spacing - y) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                xAxisVertices.push_back(startX);
                xAxisVertices.push_back(y);
                xAxisVertices.push_back(0.0f);
                xAxisVertices.push_back(endX);
                xAxisVertices.push_back(y);
                xAxisVertices.push_back(0.0f);
            } else if (isMajor) {
                majorVertices.push_back(startX);
                majorVertices.push_back(y);
                majorVertices.push_back(0.0f);
                majorVertices.push_back(endX);
                majorVertices.push_back(y);
                majorVertices.push_back(0.0f);
            } else if (drawMinor) {
                minorVertices.push_back(startX);
                minorVertices.push_back(y);
                minorVertices.push_back(0.0f);
                minorVertices.push_back(endX);
                minorVertices.push_back(y);
                minorVertices.push_back(0.0f);
            }
        }
        // Vertical lines (constant X):
        for (float x = startX; x <= endX; x += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(x) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(x / spacing) * spacing - x) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                yAxisVertices.push_back(x);
                yAxisVertices.push_back(startY);
                yAxisVertices.push_back(0.0f);
                yAxisVertices.push_back(x);
                yAxisVertices.push_back(endY);
                yAxisVertices.push_back(0.0f);
            } else if (isMajor) {
                majorVertices.push_back(x);
                majorVertices.push_back(startY);
                majorVertices.push_back(0.0f);
                majorVertices.push_back(x);
                majorVertices.push_back(endY);
                majorVertices.push_back(0.0f);
            } else if (drawMinor) {
                minorVertices.push_back(x);
                minorVertices.push_back(startY);
                minorVertices.push_back(0.0f);
                minorVertices.push_back(x);
                minorVertices.push_back(endY);
                minorVertices.push_back(0.0f);
            }
        }
    } else {
        return; // Top view uses ground floor grid
    }

    if (majorVertices.empty() && minorVertices.empty() && xAxisVertices.empty() && yAxisVertices.empty() && zAxisVertices.empty())
        return;

    // Draw the lines using stlShader
    stlShader->bind();
    stlShader->useUniform("projectionMatrix", &ProjectionMatrix);
    stlShader->useUniform("modelMatrix", &ModelMatrix);

    glm::mat4 identity(1.0f);
    stlShader->useUniform("anchorBase", &identity);

    stlShader->useUniform("eyePos", &cameraPos);
    stlShader->useUniform("wire", (GLuint)0);
    stlShader->useUniform("mistEnabled", (GLuint)0);

    // Calculate adaptive, high-contrast grid colors dynamically based on linear interpolation
    glm::vec3 bg = gloParent->mOptions->backgroundColor;
    float bgBrightness = (bg.r + bg.g + bg.b) / 3.0f;
    glm::vec3 oppositeColor = (bgBrightness > 0.5f) ? glm::vec3(0.0f) : glm::vec3(1.0f);

    // Minor lines (subgrid): 15% mix towards opposite, keeping it soft and subtle
    // Major lines (main grid): 55% mix towards opposite, making it highly distinct and crisp
    glm::vec3 minorColor = glm::mix(bg, oppositeColor, 0.15f);
    glm::vec3 majorColor = glm::mix(bg, oppositeColor, 0.55f);

    // Get current line width to restore later
    float oldLineWidth = 1.0f;
    glGetFloatv(GL_LINE_WIDTH, &oldLineWidth);

    // 1. Draw minor lines (subgrid) with a very soft, low-contrast color, thin line
    if (!minorVertices.empty()) {
        glLineWidth(1.0f);
        stlShader->useUniform("solidColor", minorColor.x, minorColor.y, minorColor.z);
        glBindBuffer(GL_ARRAY_BUFFER, orthoGridMesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, minorVertices.size() * sizeof(float), minorVertices.data(), GL_STREAM_DRAW);
        glBindVertexArray(orthoGridMesh.object);
        glDrawArrays(GL_LINES, 0, (GLsizei)(minorVertices.size() / 3));
    }

    // 2. Draw major lines (main grid) with a distinct high-contrast color, thicker line
    if (!majorVertices.empty()) {
        glLineWidth(1.5f);
        stlShader->useUniform("solidColor", majorColor.x, majorColor.y, majorColor.z);
        glBindBuffer(GL_ARRAY_BUFFER, orthoGridMesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, majorVertices.size() * sizeof(float), majorVertices.data(), GL_STREAM_DRAW);
        glBindVertexArray(orthoGridMesh.object);
        glDrawArrays(GL_LINES, 0, (GLsizei)(majorVertices.size() / 3));
    }

    // 3. Draw Axis Highlights with distinct CAD colors, thicker lines
    glLineWidth(2.0f);
    if (!xAxisVertices.empty()) {
        stlShader->useUniform("solidColor", 0.70f, 0.20f, 0.20f); // X-Axis (Red)
        glBindBuffer(GL_ARRAY_BUFFER, orthoGridMesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, xAxisVertices.size() * sizeof(float), xAxisVertices.data(), GL_STREAM_DRAW);
        glBindVertexArray(orthoGridMesh.object);
        glDrawArrays(GL_LINES, 0, (GLsizei)(xAxisVertices.size() / 3));
    }
    if (!yAxisVertices.empty()) {
        stlShader->useUniform("solidColor", 0.20f, 0.70f, 0.20f); // Y-Axis (Green)
        glBindBuffer(GL_ARRAY_BUFFER, orthoGridMesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, yAxisVertices.size() * sizeof(float), yAxisVertices.data(), GL_STREAM_DRAW);
        glBindVertexArray(orthoGridMesh.object);
        glDrawArrays(GL_LINES, 0, (GLsizei)(yAxisVertices.size() / 3));
    }
    if (!zAxisVertices.empty()) {
        stlShader->useUniform("solidColor", 0.20f, 0.20f, 0.70f); // Z-Axis (Blue)
        glBindBuffer(GL_ARRAY_BUFFER, orthoGridMesh.buffer);
        glBufferData(GL_ARRAY_BUFFER, zAxisVertices.size() * sizeof(float), zAxisVertices.data(), GL_STREAM_DRAW);
        glBindVertexArray(orthoGridMesh.object);
        glDrawArrays(GL_LINES, 0, (GLsizei)(zAxisVertices.size() / 3));
    }

    // Restore line width
    glLineWidth(oldLineWidth);
    glBindVertexArray(0);
}
