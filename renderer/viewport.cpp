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

#include "viewport.h"
#include "renderer/shaderuniforms.h"
#include "renderer/spirvlocator.h"
#include "renderer/vulkan/vulkancontext.h"
#include "trackhandler.h"
#include "track.h"
#include "trackmesh.h"
#include "mnode.h"
#include "lenassert.h"
#include "dummies.h"
#include "glbreader.h"
#include "customstyle.h"
#include "assets.h"
#include "logger.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include "imgui.h"
#include "imgui_impl_vulkan.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static bool loadEmbeddedTexture(const char* name, VulkanTexture& texture) {
    const AssetData* asset = getEmbeddedAsset(name);
    if (!asset || !asset->data || asset->size == 0)
        return false;

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(asset->data, (int)asset->size,
                                                  &width, &height, &channels, 4);
    if (!pixels)
        return false;

    texture.create2d(*gVulkanContext, (uint32_t)width, (uint32_t)height, pixels);
    stbi_image_free(pixels);
    return true;
}

static std::string lowerCase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static bool isSupportedSkyboxImage(const std::filesystem::path& path) {
    std::string extension = lowerCase(path.extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".bmp" || extension == ".tga";
}

static constexpr const char* SOLID_COLOR_SKYBOX = "Solid Color";

static std::string absolutePathOrInput(const std::string& path) {
    std::error_code error;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
    return (error ? std::filesystem::path(path) : absolutePath).lexically_normal().string();
}

static std::string findSkyboxFace(const std::filesystem::path& directory,
                                  const std::string& requestedStem) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
        return {};
    const std::string wanted = lowerCase(requestedStem);
    for (const auto& entry : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (entry.is_regular_file(error) && isSupportedSkyboxImage(entry.path()) &&
            lowerCase(entry.path().stem().string()) == wanted) {
            return entry.path().lexically_normal().string();
        }
    }
    return {};
}

Viewport::Viewport()
    : viewPortWidth(1280), viewPortHeight(720),
      fov(90.0f), mistColor(0.15f, 0.15f, 0.15f),
      grdHeight(0.0f),
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
    shutdown();
}

void Viewport::shutdown() {
    if (!gVulkanContext)
        return;
    gVulkanContext->waitIdle();
    if (outputTextureId) {
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)outputTextureId);
        outputTextureId = nullptr;
    }
    unloadSkybox(false);
    destroyPipelines();
    floorTexture.destroy();
    rasterTexture.destroy();
    dummyCubeTexture.destroy();
    dummyWhiteTexture.destroy();
    metalNormalTexture.destroy();
    metalRoughnessTexture.destroy();
    zeroAttributeBuffer.destroy();
    skyMesh.vertexBuffer.destroy();
    floorMesh.vertexBuffer.destroy();
    markerMesh.vertexBuffer.destroy();
    markerMesh.indexBuffer.destroy();
    for (auto& sm : glbMeshes) {
        for (auto& prim : sm.primitives) {
            prim.mesh.vertexBuffer.destroy();
            prim.mesh.indexBuffer.destroy();
            if (prim.hasTexture) {
                prim.texture.destroy();
            }
        }
    }
    finalOutputFb.destroy();
}

void Viewport::setMistColor(glm::vec3 color) {
    mistColor = color;
    sceneDirty = true;
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
    // Rotating the cubemap lookup direction makes the visible environment move
    // in the opposite direction, so apply the inverse offset to the sun vector.
    float y = glm::radians(yaw - gloParent->mOptions->skyboxRotation);

    lightDir.x = cos(p) * sin(y);
    lightDir.y = sin(p);
    lightDir.z = cos(p) * cos(y);
    lightDir = glm::normalize(lightDir);
    sceneDirty = true;
}

float Viewport::getShadowStrength() const {
    const glm::vec3 luminanceWeights(0.2126f, 0.7152f, 0.0722f);
    float sunLuminance = glm::dot(gloParent->mOptions->sunLightColor, luminanceWeights);
    float horizonFade = std::clamp(-lightDir.y / 0.1f, 0.0f, 1.0f);
    return std::clamp(gloParent->mOptions->sunLightStrength * sunLuminance * horizonFade,
                      0.0f, 1.0f);
}

void Viewport::setMSAASamples(int samples) {
    if (msaaSamples == samples)
        return;
    msaaSamples = samples;
    sceneDirty = true;
    if (!gVulkanContext || finalOutputFb.colorImage == VK_NULL_HANDLE)
        return;

    VkSampleCountFlagBits clampedCount = gVulkanContext->clampSampleCount(samples);
    if (clampedCount == currentSampleCount)
        return;
    currentSampleCount = clampedCount;

    gVulkanContext->waitIdle();
    destroyPipelines();
    initPipelines();
    finalOutputFb.destroy();
    finalOutputFb.create(*gVulkanContext, viewPortWidth, viewPortHeight, VK_FORMAT_R8G8B8A8_UNORM, currentSampleCount);
    refreshOutputTexture();
}

void Viewport::destroyPipelines() {
    skyPipeline.destroy();
    floorPipeline.destroy();
    heartlinePipeline.destroy();
    trackInstancedPipeline.destroy();
    shadowInstancedPipeline.destroy();
    shadowGlbPipeline.destroy();
    glbPipeline.destroy();
    markerPipeline.destroy();
    orthoGridPipeline.destroy();
}

void Viewport::initialize(int width, int height) {
    viewPortWidth = width;
    viewPortHeight = height;

    char zeros[64] = {};
    zeroAttributeBuffer.create(*gVulkanContext, sizeof(zeros), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    zeroAttributeBuffer.write(zeros, sizeof(zeros));

    currentSampleCount = gVulkanContext->clampSampleCount(msaaSamples);
    initTextures();
    initFloorMesh();
    initPipelines();

    finalOutputFb.create(*gVulkanContext, width, height, VK_FORMAT_R8G8B8A8_UNORM, currentSampleCount);
    refreshOutputTexture();
}

void Viewport::refreshOutputTexture() {
    if (outputTextureId)
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)outputTextureId);
    outputTextureId = (void*)ImGui_ImplVulkan_AddTexture(finalOutputFb.sampler, finalOutputFb.colorView,
                                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void* Viewport::getOutputTexture() {
    return outputTextureId;
}

void Viewport::resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == viewPortWidth && height == viewPortHeight))
        return;
    sceneDirty = true;
    viewPortWidth = width;
    viewPortHeight = height;
    finalOutputFb.resize(width, height);
    refreshOutputTexture();
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
    rasterTexture.create2d(*gVulkanContext, RASTER_SIZE, RASTER_SIZE, rasterData);
    delete[] rasterData;

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    floorTexture.create2d(*gVulkanContext, 1, 1, whitePixel);
    dummyWhiteTexture.create2d(*gVulkanContext, 1, 1, whitePixel);

    unsigned char cubeFaces[6 * 4] = {};
    dummyCubeTexture.createCube(*gVulkanContext, 1, 1, cubeFaces);

    if (!loadEmbeddedTexture("metal_normal.png", metalNormalTexture)) {
        const unsigned char flatNormal[4] = {128, 128, 255, 255};
        metalNormalTexture.create2d(*gVulkanContext, 1, 1, flatNormal);
    }
    if (!loadEmbeddedTexture("metal_roughness.jpg", metalRoughnessTexture)) {
        const unsigned char defaultRoughness[4] = {160, 160, 160, 255};
        metalRoughnessTexture.create2d(*gVulkanContext, 1, 1, defaultRoughness);
    }

    refreshSkyboxes(true);
    refreshEnvironmentPresets();
}

uint64_t Viewport::getFileSignature(const std::string& path) const {
    std::error_code error;
    const std::filesystem::path filePath(path);
    if (!std::filesystem::is_regular_file(filePath, error))
        return 0;

    uint64_t signature = static_cast<uint64_t>(std::filesystem::file_size(filePath, error));
    if (error)
        return 0;
    auto writeTime = std::filesystem::last_write_time(filePath, error);
    if (error)
        return 0;
    signature ^= static_cast<uint64_t>(writeTime.time_since_epoch().count()) +
                 0x9e3779b97f4a7c15ULL + (signature << 6) + (signature >> 2);
    return signature;
}

uint64_t Viewport::getSkyboxDirectorySignature() const {
    const std::filesystem::path root("skybox");
    std::error_code error;
    if (!std::filesystem::exists(root, error))
        return 0;

    std::vector<std::string> entries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (entry.is_regular_file(error) && isSupportedSkyboxImage(entry.path())) {
            entries.push_back(entry.path().lexically_normal().string() + ":" +
                              std::to_string(getFileSignature(entry.path().string())));
        }
    }
    std::sort(entries.begin(), entries.end());

    uint64_t signature = 1469598103934665603ULL;
    for (const std::string& entry : entries) {
        for (unsigned char c : entry) {
            signature ^= c;
            signature *= 1099511628211ULL;
        }
    }
    return signature;
}

void Viewport::unloadSkybox(bool bindFallback) {
    if (skyTexture) {
        skyTexture->destroy();
        delete skyTexture;
        skyTexture = nullptr;
    }
    activeSkyboxName.clear();
    activeSkyboxSignature = 0;
    if (bindFallback && dummyCubeTexture.view != VK_NULL_HANDLE)
        skyPipeline.bindTexture(0, dummyCubeTexture.view, dummyCubeTexture.sampler);
    sceneDirty = true;
}

bool Viewport::loadSkybox(const SkyboxDefinition& definition) {
    if (definition.facePaths.size() != 6)
        return false;

    int width = 0;
    int height = 0;
    for (const std::string& path : definition.facePaths) {
        int faceWidth = 0;
        int faceHeight = 0;
        int channels = 0;
        if (!stbi_info(path.c_str(), &faceWidth, &faceHeight, &channels) || faceWidth <= 0 ||
            faceWidth != faceHeight || (width > 0 && (faceWidth != width || faceHeight != height))) {
            LOG_WARN("Invalid or mismatched skybox face: %s", path.c_str());
            return false;
        }
        width = faceWidth;
        height = faceHeight;
    }

    const uint64_t faceBytes = static_cast<uint64_t>(width) * height * 4;
    const uint64_t totalBytes = faceBytes * definition.facePaths.size();
    if (totalBytes > 512ULL * 1024ULL * 1024ULL ||
        totalBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        LOG_WARN("Skybox '%s' is too large to load safely (%llu MiB decoded)",
                 definition.name.c_str(), static_cast<unsigned long long>(totalBytes / (1024 * 1024)));
        return false;
    }

    std::vector<unsigned char> faces(static_cast<size_t>(totalBytes));
    for (size_t i = 0; i < definition.facePaths.size(); ++i) {
        int faceWidth = 0;
        int faceHeight = 0;
        int channels = 0;
        unsigned char* data = stbi_load(definition.facePaths[i].c_str(), &faceWidth,
                                        &faceHeight, &channels, 4);
        if (!data) {
            LOG_WARN("Failed to decode skybox face: %s", definition.facePaths[i].c_str());
            return false;
        }
        const size_t byteCount = static_cast<size_t>(faceBytes);
        std::copy(data, data + byteCount, faces.begin() + i * byteCount);
        stbi_image_free(data);
    }

    gVulkanContext->waitIdle();
    unloadSkybox();
    skyTexture = new VulkanTexture();
    skyTexture->createCube(*gVulkanContext, static_cast<uint32_t>(width),
                           static_cast<uint32_t>(height), faces.data());
    skyPipeline.bindTexture(0, skyTexture->view, skyTexture->sampler);
    activeSkyboxName = definition.name;
    activeSkyboxSignature = definition.signature;
    sceneDirty = true;
    LOG_INFO("Loaded skybox '%s' (%dx%d)", definition.name.c_str(), width, height);
    return true;
}

void Viewport::refreshSkyboxes(bool force) {
    std::error_code error;
    std::filesystem::create_directories("skybox", error);
    uint64_t directorySignature = getSkyboxDirectorySignature();
    bool directoryChanged = directorySignature != skyboxDirectorySignature;
    if (!force && !directoryChanged) {
        auto selected = std::find_if(skyboxDefinitions.begin(), skyboxDefinitions.end(),
                                     [&](const SkyboxDefinition& definition) {
                                         return definition.name == gloParent->mOptions->skyboxName;
                                     });
        if ((selected == skyboxDefinitions.end() && !skyTexture) ||
            (selected != skyboxDefinitions.end() && activeSkyboxName == selected->name &&
             activeSkyboxSignature == selected->signature)) {
            return;
        }
    }
    skyboxDirectorySignature = directorySignature;

    std::vector<SkyboxDefinition> discovered;
    auto addDirectory = [&](const std::filesystem::path& directory, const std::string& name) {
        SkyboxDefinition definition;
        definition.name = name;
        definition.directory = directory.lexically_normal().string();

        // Vulkan cube-array order is +X, -X, +Y, -Y, +Z, -Z.
        const char* namedFaces[] = {"px", "nx", "py", "ny", "pz", "nz"};
        for (const char* stem : namedFaces)
            definition.facePaths.push_back(findSkyboxFace(directory, stem));

        bool namedComplete = std::all_of(definition.facePaths.begin(), definition.facePaths.end(),
                                         [](const std::string& path) { return !path.empty(); });
        if (!namedComplete) {
            definition.facePaths.clear();
            for (int i = 0; i < 6; ++i)
                definition.facePaths.push_back(findSkyboxFace(directory, "cubemap_" + std::to_string(i)));
        }

        bool complete = std::all_of(definition.facePaths.begin(), definition.facePaths.end(),
                                    [](const std::string& path) { return !path.empty(); });
        if (!complete)
            return;

        definition.signature = 1469598103934665603ULL;
        for (const std::string& path : definition.facePaths) {
            uint64_t faceSignature = getFileSignature(path);
            definition.signature ^= faceSignature + 0x9e3779b97f4a7c15ULL +
                                    (definition.signature << 6) + (definition.signature >> 2);
        }
        discovered.push_back(std::move(definition));
    };

    const std::filesystem::path root("skybox");
    for (const auto& entry : std::filesystem::directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error)) {
        if (entry.is_directory(error))
            addDirectory(entry.path(), entry.path().filename().string());
    }
    std::sort(discovered.begin(), discovered.end(),
              [](const SkyboxDefinition& left, const SkyboxDefinition& right) {
                  return lowerCase(left.name) < lowerCase(right.name);
              });

    skyboxDefinitions = std::move(discovered);
    availableSkyboxNames = {SOLID_COLOR_SKYBOX};
    for (const SkyboxDefinition& definition : skyboxDefinitions)
        availableSkyboxNames.push_back(definition.name);
    gloParent->skyboxAvailable = !skyboxDefinitions.empty();

    std::string requestedName = gloParent->mOptions->skyboxName;
    if (requestedName.empty()) {
        requestedName = SOLID_COLOR_SKYBOX;
        gloParent->mOptions->skyboxName = requestedName;
    }

    if (requestedName == SOLID_COLOR_SKYBOX) {
        if (skyTexture) {
            gVulkanContext->waitIdle();
            unloadSkybox();
        }
        return;
    }

    auto requested = std::find_if(skyboxDefinitions.begin(), skyboxDefinitions.end(),
                                  [&](const SkyboxDefinition& definition) {
                                      return definition.name == requestedName;
                                  });
    if (requested == skyboxDefinitions.end()) {
        if (skyTexture) {
            gVulkanContext->waitIdle();
            unloadSkybox();
        }
        return;
    }

    if (activeSkyboxName != requested->name || activeSkyboxSignature != requested->signature)
        loadSkybox(*requested);
}

bool Viewport::selectSkybox(const std::string& name) {
    gloParent->mOptions->skyboxName = name;
    refreshSkyboxes(true);
    return name == SOLID_COLOR_SKYBOX || activeSkyboxName == name;
}

void Viewport::setSkyboxRotation(float degrees) {
    gloParent->mOptions->skyboxRotation = degrees;
    setLightDirection(gloParent->mOptions->sunPitch, gloParent->mOptions->sunYaw);
}

void Viewport::refreshEnvironmentPresets() {
    const std::filesystem::path root("skybox");
    std::error_code iteratorError;
    std::vector<EnvironmentPresetDefinition> discovered;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, iteratorError)) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) ||
            lowerCase(entry.path().extension().string()) != ".fvdenv") {
            continue;
        }

        std::ifstream input(entry.path());
        std::string magic;
        if (!(input >> magic) || magic != "FVD_ENVIRONMENT_V1")
            continue;

        std::error_code relativeError;
        std::filesystem::path relativePath = std::filesystem::relative(entry.path(), root, relativeError);
        if (relativeError)
            relativePath = entry.path().filename();

        std::filesystem::path labelPath = relativePath;
        labelPath.replace_extension();
        std::string name;
        if (lowerCase(labelPath.filename().string()) == "environment" && labelPath.has_parent_path() &&
            labelPath.parent_path() != ".") {
            name = labelPath.parent_path().generic_string();
        } else {
            name = labelPath.generic_string();
        }

        if (std::any_of(discovered.begin(), discovered.end(), [&](const EnvironmentPresetDefinition& preset) {
                return preset.name == name;
            })) {
            name += " (" + relativePath.generic_string() + ")";
        }
        discovered.push_back({name, absolutePathOrInput(entry.path().string())});
    }

    std::sort(discovered.begin(), discovered.end(),
              [](const EnvironmentPresetDefinition& left, const EnvironmentPresetDefinition& right) {
                  return lowerCase(left.name) < lowerCase(right.name);
              });
    environmentPresetDefinitions = std::move(discovered);
    availableEnvironmentPresetNames.clear();
    selectedEnvironmentPresetName.clear();
    for (const EnvironmentPresetDefinition& preset : environmentPresetDefinitions) {
        availableEnvironmentPresetNames.push_back(preset.name);
        if (preset.path == watchedEnvironmentPath)
            selectedEnvironmentPresetName = preset.name;
    }

    if (selectedEnvironmentPresetName.empty() && !watchedEnvironmentPath.empty() &&
        getFileSignature(watchedEnvironmentPath) != 0) {
        selectedEnvironmentPresetName = std::filesystem::path(watchedEnvironmentPath).stem().string();
    }
}

bool Viewport::selectEnvironmentPreset(const std::string& name) {
    auto preset = std::find_if(environmentPresetDefinitions.begin(), environmentPresetDefinitions.end(),
                               [&](const EnvironmentPresetDefinition& definition) {
                                   return definition.name == name;
                               });
    return preset != environmentPresetDefinitions.end() && loadEnvironmentPreset(preset->path);
}

void Viewport::applyProjectEnvironment() {
    watchedEnvironmentPath.clear();
    watchedEnvironmentSignature = 0;
    selectedEnvironmentPresetName.clear();

    setMistColor(gloParent->mOptions->mistColor);
    setShadowMode(gloParent->mOptions->shadowsEnabled ? 1 : 0);
    setSkyboxRotation(gloParent->mOptions->skyboxRotation);
    selectSkybox(gloParent->mOptions->skyboxName);
    sceneDirty = true;
}

std::string Viewport::suggestedEnvironmentPresetPath() const {
    auto selected = std::find_if(skyboxDefinitions.begin(), skyboxDefinitions.end(),
                                 [&](const SkyboxDefinition& definition) {
                                     return definition.name == gloParent->mOptions->skyboxName;
                                 });
    std::filesystem::path directory = selected == skyboxDefinitions.end()
                                          ? std::filesystem::path("skybox")
                                          : std::filesystem::path(selected->directory);
    return (directory / "environment.fvdenv").string();
}

bool Viewport::saveEnvironmentPreset(const std::string& path) {
    std::ofstream output(path, std::ios::trunc);
    if (!output)
        return false;

    const DummyOptions& options = *gloParent->mOptions;
    output << "FVD_ENVIRONMENT_V1\n"
           << "ambient_strength " << options.ambientLightStrength << "\n"
           << "ambient_color " << options.ambientLightColor.x << " "
           << options.ambientLightColor.y << " " << options.ambientLightColor.z << "\n"
           << "sun_strength " << options.sunLightStrength << "\n"
           << "sun_color " << options.sunLightColor.x << " " << options.sunLightColor.y << " "
           << options.sunLightColor.z << "\n"
           << "sun_pitch " << options.sunPitch << "\n"
           << "sun_yaw " << options.sunYaw << "\n"
           << "shadows " << options.shadowsEnabled << "\n"
           << "track_texture " << options.trackTextureEnabled << "\n"
           << "skybox " << std::quoted(options.skyboxName) << "\n"
           << "skybox_color " << options.backgroundColor.x << " "
           << options.backgroundColor.y << " " << options.backgroundColor.z << "\n"
           << "skybox_rotation " << options.skyboxRotation << "\n"
           << "mist_enabled " << options.mistEnabled << "\n"
           << "mist_near " << options.mistNear << "\n"
           << "mist_far " << options.mistFar << "\n"
           << "mist_color " << options.mistColor.x << " " << options.mistColor.y << " "
           << options.mistColor.z << "\n"
           << "end\n";
    output.close();
    if (!output)
        return false;

    watchedEnvironmentPath = absolutePathOrInput(path);
    watchedEnvironmentSignature = getFileSignature(watchedEnvironmentPath);
    refreshEnvironmentPresets();
    return true;
}

bool Viewport::loadEnvironmentPreset(const std::string& path) {
    std::ifstream input(path);
    std::string magic;
    if (!(input >> magic) || magic != "FVD_ENVIRONMENT_V1")
        return false;

    DummyOptions preset = *gloParent->mOptions;
    bool complete = false;
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        std::istringstream values(line);
        std::string key;
        if (!(values >> key) || key[0] == '#')
            continue;
        if (key == "end") {
            complete = true;
            break;
        }
        if (key == "ambient_strength")
            values >> preset.ambientLightStrength;
        else if (key == "ambient_color")
            values >> preset.ambientLightColor.x >> preset.ambientLightColor.y >> preset.ambientLightColor.z;
        else if (key == "sun_strength")
            values >> preset.sunLightStrength;
        else if (key == "sun_color")
            values >> preset.sunLightColor.x >> preset.sunLightColor.y >> preset.sunLightColor.z;
        else if (key == "sun_pitch")
            values >> preset.sunPitch;
        else if (key == "sun_yaw")
            values >> preset.sunYaw;
        else if (key == "shadows")
            values >> preset.shadowsEnabled;
        else if (key == "track_texture")
            values >> preset.trackTextureEnabled;
        else if (key == "skybox")
            values >> std::quoted(preset.skyboxName);
        else if (key == "skybox_color")
            values >> preset.backgroundColor.x >> preset.backgroundColor.y >> preset.backgroundColor.z;
        else if (key == "skybox_rotation")
            values >> preset.skyboxRotation;
        else if (key == "mist_enabled")
            values >> preset.mistEnabled;
        else if (key == "mist_near")
            values >> preset.mistNear;
        else if (key == "mist_far")
            values >> preset.mistFar;
        else if (key == "mist_color")
            values >> preset.mistColor.x >> preset.mistColor.y >> preset.mistColor.z;
        if (values.fail())
            return false;
    }
    if (!complete)
        return false;

    preset.ambientLightStrength = std::clamp(preset.ambientLightStrength, 0.0f, 2.0f);
    preset.sunLightStrength = std::clamp(preset.sunLightStrength, 0.0f, 2.0f);
    preset.sunPitch = std::clamp(preset.sunPitch, -90.0f, 0.0f);
    preset.mistNear = std::clamp(preset.mistNear, 0.0f, 5000.0f);
    preset.mistFar = std::clamp(preset.mistFar, preset.mistNear, 10000.0f);
    preset.ambientLightColor = glm::clamp(preset.ambientLightColor, glm::vec3(0.0f), glm::vec3(1.0f));
    preset.sunLightColor = glm::clamp(preset.sunLightColor, glm::vec3(0.0f), glm::vec3(1.0f));
    preset.mistColor = glm::clamp(preset.mistColor, glm::vec3(0.0f), glm::vec3(1.0f));
    preset.backgroundColor = glm::clamp(preset.backgroundColor, glm::vec3(0.0f), glm::vec3(1.0f));

    *gloParent->mOptions = preset;
    setMistColor(preset.mistColor);
    setShadowMode(preset.shadowsEnabled ? 1 : 0);
    setSkyboxRotation(preset.skyboxRotation);
    selectSkybox(preset.skyboxName);
    sceneDirty = true;

    watchedEnvironmentPath = absolutePathOrInput(path);
    watchedEnvironmentSignature = getFileSignature(watchedEnvironmentPath);
    refreshEnvironmentPresets();
    LOG_INFO("Loaded environment preset: %s", watchedEnvironmentPath.c_str());
    return true;
}

void Viewport::pollEnvironmentFiles(float deltaTime) {
    environmentPollAccumulator += std::max(deltaTime, 0.0f);
    if (environmentPollAccumulator < 1.0f)
        return;
    environmentPollAccumulator = 0.0f;

    refreshSkyboxes();
    refreshEnvironmentPresets();
    if (watchedEnvironmentPath.empty())
        return;
    uint64_t signature = getFileSignature(watchedEnvironmentPath);
    if (signature != 0 && signature != watchedEnvironmentSignature)
        loadEnvironmentPreset(watchedEnvironmentPath);
}

bool Viewport::loadGroundTexture(const std::string& path) {
    gVulkanContext->waitIdle();
    floorTexture.destroy();

    sceneDirty = true;
    if (!path.empty()) {
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
        stbi_set_flip_vertically_on_load(false);
        if (data) {
            floorTexture.create2d(*gVulkanContext, width, height, data);
            stbi_image_free(data);
            floorPipeline.bindTexture(1, floorTexture.view, floorTexture.sampler);
            return true;
        }
    }

    unsigned char whitePixel[4] = {255, 255, 255, 255};
    floorTexture.create2d(*gVulkanContext, 1, 1, whitePixel);
    floorPipeline.bindTexture(1, floorTexture.view, floorTexture.sampler);
    return false;
}

void Viewport::initFloorMesh() {
    float a = 1000.f;
    float floor[4 * 6] = {
        -a, 0.f, -a, 0.f, 1.0f, 0.f,
        -a, 0.f, +a, 0.f, 1.0f, 0.f,
        +a, 0.f, -a, 0.f, 1.0f, 0.f,
        +a, 0.f, +a, 0.f, 1.0f, 0.f};
    floorMesh.vertexBuffer.create(*gVulkanContext, sizeof(floor), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    floorMesh.vertexBuffer.write(floor, sizeof(floor));
    floorMesh.count = 4;

    float sky[12] = {1.f, 1.f, 0.9999f, 1.f, -1.f, 0.9999f, -1.f, 1.f, 0.9999f, -1.f, -1.f, 0.9999f};
    skyMesh.vertexBuffer.create(*gVulkanContext, sizeof(sky), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skyMesh.vertexBuffer.write(sky, sizeof(sky));
    skyMesh.count = 4;

    std::vector<float> markerData;
    std::vector<uint32_t> indices;
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

    markerMesh.vertexBuffer.create(*gVulkanContext, markerData.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    markerMesh.vertexBuffer.write(markerData.data(), markerData.size() * sizeof(float));
    markerMesh.indexBuffer.create(*gVulkanContext, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    markerMesh.indexBuffer.write(indices.data(), indices.size() * sizeof(uint32_t));
    markerMesh.count = (int)indices.size();
}

void Viewport::initPipelines() {
    const uint32_t zeroBinding = 1;

    VulkanPipelineConfig skyConfig = {
        .vertexSpirvPath = locateSpirvShader("sky.vert"),
        .fragmentSpirvPath = locateSpirvShader("sky.frag"),
        .vertexBindings = {{0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX}},
        .vertexAttributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .depthTest = false,
        .depthWrite = false,
        .sampledImageCount = 1,
        .uniformBufferSize = sizeof(SkyUniforms),
    };
    skyConfig.sampleCount = currentSampleCount;
    skyPipeline.create(*gVulkanContext, skyConfig);
    skyPipeline.bindTexture(0, skyTexture ? skyTexture->view : dummyCubeTexture.view,
                            skyTexture ? skyTexture->sampler : dummyCubeTexture.sampler);

    VulkanPipelineConfig floorConfig = {
        .vertexSpirvPath = locateSpirvShader("floor.vert"),
        .fragmentSpirvPath = locateSpirvShader("floor.frag"),
        .vertexBindings = {{0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX}},
        .vertexAttributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}},
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .alphaBlend = true,
        .stencilMode = VulkanPipelineConfig::StencilMode::writeReference,
        .sampledImageCount = 2,
        .uniformBufferSize = sizeof(FloorUniforms),
    };
    floorConfig.sampleCount = currentSampleCount;
    floorPipeline.create(*gVulkanContext, floorConfig);
    floorPipeline.bindTexture(0, rasterTexture.view, rasterTexture.sampler);
    floorPipeline.bindTexture(1, floorTexture.view, floorTexture.sampler);

    VulkanPipelineConfig heartlineConfig = {
        .vertexSpirvPath = locateSpirvShader("track.vert"),
        .fragmentSpirvPath = locateSpirvShader("track.frag"),
        .vertexBindings = {
            {0, sizeof(glm::vec3) + sizeof(int), VK_VERTEX_INPUT_RATE_VERTEX},
            {zeroBinding, 0, VK_VERTEX_INPUT_RATE_VERTEX},
        },
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {1, zeroBinding, VK_FORMAT_R32_SFLOAT, 0},
            {2, zeroBinding, VK_FORMAT_R32_SFLOAT, 0},
            {3, zeroBinding, VK_FORMAT_R32_SFLOAT, 0},
            {4, zeroBinding, VK_FORMAT_R32_SFLOAT, 0},
            {5, zeroBinding, VK_FORMAT_R32_SFLOAT, 0},
            {6, zeroBinding, VK_FORMAT_R32_SFLOAT, 0},
            {7, zeroBinding, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {8, zeroBinding, VK_FORMAT_R32G32_SFLOAT, 0},
        },
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        .alphaBlend = true,
        .uniformBufferSize = sizeof(TrackUniforms),
    };
    heartlineConfig.sampleCount = currentSampleCount;
    heartlinePipeline.create(*gVulkanContext, heartlineConfig);

    std::vector<VkVertexInputBindingDescription> instancedBindings = {
        {0, sizeof(StyleVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(track_asset_instance_t), VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    std::vector<VkVertexInputAttributeDescription> instancedAttributes = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
        {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
        {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48},
        {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64},
        {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 80},
        {9, 1, VK_FORMAT_R32G32B32_SFLOAT, 96},
        {10, 0, VK_FORMAT_R32_SFLOAT, offsetof(StyleVertex, faceRefZ)},
    };
    VulkanPipelineConfig trackInstancedConfig = {
        .vertexSpirvPath = locateSpirvShader("track_instanced.vert"),
        .fragmentSpirvPath = locateSpirvShader("track_instanced.frag"),
        .vertexBindings = instancedBindings,
        .vertexAttributes = instancedAttributes,
        .alphaBlend = true,
        .sampledImageCount = 2,
        .uniformBufferSize = sizeof(TrackInstancedUniforms),
        .usesStorageSet = true,
    };
    trackInstancedConfig.sampleCount = currentSampleCount;
    trackInstancedPipeline.create(*gVulkanContext, trackInstancedConfig);
    trackInstancedPipeline.bindTexture(0, metalNormalTexture.view, metalNormalTexture.sampler);
    trackInstancedPipeline.bindTexture(1, metalRoughnessTexture.view, metalRoughnessTexture.sampler);

    VulkanPipelineConfig shadowInstancedConfig = {
        .vertexSpirvPath = locateSpirvShader("simple_shadow.vert"),
        .fragmentSpirvPath = locateSpirvShader("simple_shadow.frag"),
        .vertexBindings = instancedBindings,
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
            {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
            {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48},
            {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64},
            {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 80},
        },
        .depthTest = false,
        .depthWrite = false,
        .alphaBlend = true,
        .stencilMode = VulkanPipelineConfig::StencilMode::testReferenceIncrement,
        .uniformBufferSize = sizeof(SimpleShadowUniforms),
        .usesStorageSet = true,
    };
    shadowInstancedConfig.sampleCount = currentSampleCount;
    shadowInstancedPipeline.create(*gVulkanContext, shadowInstancedConfig);

    VulkanPipelineConfig shadowGlbConfig = {
        .vertexSpirvPath = locateSpirvShader("simple_shadow.vert"),
        .fragmentSpirvPath = locateSpirvShader("simple_shadow.frag"),
        .vertexBindings = {
            {0, sizeof(GlbVertex), VK_VERTEX_INPUT_RATE_VERTEX},
            {zeroBinding, 0, VK_VERTEX_INPUT_RATE_INSTANCE},
        },
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GlbVertex, pos)},
            {3, zeroBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {4, zeroBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {5, zeroBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {6, zeroBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {7, zeroBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
            {8, zeroBinding, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
        },
        .depthTest = false,
        .depthWrite = false,
        .alphaBlend = true,
        .stencilMode = VulkanPipelineConfig::StencilMode::testReferenceIncrement,
        .uniformBufferSize = sizeof(SimpleShadowUniforms),
        .usesStorageSet = true,
    };
    shadowGlbConfig.sampleCount = currentSampleCount;
    shadowGlbPipeline.create(*gVulkanContext, shadowGlbConfig);

    VulkanPipelineConfig glbConfig = {
        .vertexSpirvPath = locateSpirvShader("glb.vert"),
        .fragmentSpirvPath = locateSpirvShader("glb.frag"),
        .vertexBindings = {{0, sizeof(GlbVertex), VK_VERTEX_INPUT_RATE_VERTEX}},
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GlbVertex, pos)},
            {7, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GlbVertex, normal)},
            {8, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GlbVertex, uv)},
        },
        .alphaBlend = true,
        .sampledImageCount = 1,
        .uniformBufferSize = sizeof(StlUniforms),
    };
    glbConfig.sampleCount = currentSampleCount;
    glbPipeline.create(*gVulkanContext, glbConfig);
    glbPipeline.bindTexture(0, dummyWhiteTexture.view, dummyWhiteTexture.sampler);

    VulkanPipelineConfig markerConfig = {
        .vertexSpirvPath = locateSpirvShader("glb.vert"),
        .fragmentSpirvPath = locateSpirvShader("glb.frag"),
        .vertexBindings = {
            {0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            {zeroBinding, 0, VK_VERTEX_INPUT_RATE_VERTEX},
        },
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {7, zeroBinding, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {8, zeroBinding, VK_FORMAT_R32G32_SFLOAT, 0},
        },
        .alphaBlend = true,
        .sampledImageCount = 1,
        .uniformBufferSize = sizeof(StlUniforms),
    };
    markerConfig.sampleCount = currentSampleCount;
    markerPipeline.create(*gVulkanContext, markerConfig);
    markerPipeline.bindTexture(0, dummyWhiteTexture.view, dummyWhiteTexture.sampler);

    VulkanPipelineConfig orthoGridConfig = {
        .vertexSpirvPath = locateSpirvShader("glb.vert"),
        .fragmentSpirvPath = locateSpirvShader("glb.frag"),
        .vertexBindings = {
            {0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
            {zeroBinding, 0, VK_VERTEX_INPUT_RATE_VERTEX},
        },
        .vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {7, zeroBinding, VK_FORMAT_R32G32B32_SFLOAT, 0},
            {8, zeroBinding, VK_FORMAT_R32G32_SFLOAT, 0},
        },
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .alphaBlend = true,
        .stencilMode = VulkanPipelineConfig::StencilMode::writeReference,
        .sampledImageCount = 1,
        .uniformBufferSize = sizeof(StlUniforms),
    };
    orthoGridConfig.sampleCount = currentSampleCount;
    orthoGridPipeline.create(*gVulkanContext, orthoGridConfig);
    orthoGridPipeline.bindTexture(0, dummyWhiteTexture.view, dummyWhiteTexture.sampler);
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

    VkCommandBuffer commandBuffer = gVulkanContext->currentCommandBuffer();
    if (!commandBuffer)
        return;

    buildMatrices();

    for (auto track : trackList) {
        if (track->trackData->drawHeartline != 3 && track->trackData->hasChanged) {
            track->mMesh->recolorTrack();
            track->trackData->hasChanged = false;
        }
    }

    finalOutputFb.beginRendering(commandBuffer, mistColor.r, mistColor.g, mistColor.b);

    drawSky(commandBuffer);
    drawFloor(commandBuffer);
    drawOrthoGrid(commandBuffer);

    if (shadowMode > 0 && getShadowStrength() > 0.0001f) {
        for (auto track : trackList) {
            if (track->trackData->drawHeartline != 3) {
                drawTrack(commandBuffer, track, RenderPass::PlanarShadow);
            }
        }
        if (gloParent->mOptions->glbShadowsEnabled) {
            drawGlbs(commandBuffer, RenderPass::PlanarShadow);
        }
    }

    for (auto track : trackList) {
        if (track->trackData->drawHeartline != 3) {
            drawTrack(commandBuffer, track, RenderPass::Main);
        }
    }
    drawGlbs(commandBuffer, RenderPass::Main);
    drawMarkers(commandBuffer);

    finalOutputFb.endRendering(commandBuffer);

    sceneDirty = false;
}

void Viewport::update(float deltaTime) {
    pollEnvironmentFiles(deltaTime);
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
    sceneDirty = true;

    VkCommandBuffer commandBuffer = gVulkanContext->currentCommandBuffer();
    bool ownCommands = (commandBuffer == VK_NULL_HANDLE);
    if (ownCommands) {
        commandBuffer = gVulkanContext->beginOneTimeCommands();
    }
    buildMatrices();
    finalOutputFb.beginRendering(commandBuffer, mistColor.r, mistColor.g, mistColor.b);
    drawSky(commandBuffer);
    drawFloor(commandBuffer);
    drawOrthoGrid(commandBuffer);
    if (shadowMode > 0 && getShadowStrength() > 0.0001f) {
        for (auto track : trackList) {
            if (track->trackData->drawHeartline != 3)
                drawTrack(commandBuffer, track, RenderPass::PlanarShadow);
        }
        if (gloParent->mOptions->glbShadowsEnabled)
            drawGlbs(commandBuffer, RenderPass::PlanarShadow);
    }
    for (auto track : trackList) {
        if (track->trackData->drawHeartline != 3)
            drawTrack(commandBuffer, track, RenderPass::Main);
    }
    drawGlbs(commandBuffer, RenderPass::Main);
    drawMarkers(commandBuffer);
    finalOutputFb.endRendering(commandBuffer);
    if (ownCommands) {
        gVulkanContext->endOneTimeCommands(commandBuffer);
    } else {
        gVulkanContext->waitIdle();
    }

    std::vector<uint8_t> rgba = finalOutputFb.readPixels();
    std::vector<unsigned char> rgb((size_t)hiResWidth * hiResHeight * 3);
    for (size_t pixel = 0; pixel < (size_t)hiResWidth * hiResHeight; ++pixel) {
        rgb[pixel * 3 + 0] = rgba[pixel * 4 + 0];
        rgb[pixel * 3 + 1] = rgba[pixel * 4 + 1];
        rgb[pixel * 3 + 2] = rgba[pixel * 4 + 2];
    }
    stbi_flip_vertically_on_write(false);
    stbi_write_png(path.c_str(), hiResWidth, hiResHeight, 3, rgb.data(), hiResWidth * 3);

    resize(originalWidth, originalHeight);
    sceneDirty = true;
}

bool Viewport::addGlbMesh(const std::string& path) {
    GlbModelData modelData;
    if (!readGlb(path, modelData)) {
        return false;
    }

    GlbMesh gm;
    gm.path = path;
    gm.visible = true;
    gm.selected = false;
    gm.minAABB = modelData.minAABB;
    gm.maxAABB = modelData.maxAABB;

    for (auto& primData : modelData.primitives) {
        GlbPrimitive prim;
        prim.vertexCount = (int)primData.vertices.size();
        prim.indexCount = (int)primData.indices.size();
        prim.baseColorFactor = primData.baseColorFactor;
        prim.hasTexture = primData.hasTexture;

        // Create vertex buffer
        size_t vertexBufferSize = primData.vertices.size() * sizeof(GlbVertex);
        prim.mesh.vertexBuffer.create(*gVulkanContext, vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        prim.mesh.vertexBuffer.write(primData.vertices.data(), vertexBufferSize);

        // Create index buffer
        size_t indexBufferSize = primData.indices.size() * sizeof(uint32_t);
        prim.mesh.indexBuffer.create(*gVulkanContext, indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        prim.mesh.indexBuffer.write(primData.indices.data(), indexBufferSize);

        // Create texture if present
        if (prim.hasTexture) {
            prim.texture.create2d(*gVulkanContext, primData.textureData.width, primData.textureData.height, primData.textureData.rgba.data());
            prim.descriptorSets = glbPipeline.createDescriptorSets(prim.texture.view, prim.texture.sampler);
        } else {
            prim.descriptorSets = glbPipeline.createDescriptorSets(dummyWhiteTexture.view, dummyWhiteTexture.sampler);
        }

        gm.primitives.push_back(std::move(prim));
    }

    glbMeshes.push_back(std::move(gm));
    sceneDirty = true;
    return true;
}

void Viewport::removeGlbMesh(int index) {
    if (index < 0 || index >= (int)glbMeshes.size())
        return;

    gVulkanContext->waitIdle();
    for (auto& prim : glbMeshes[index].primitives) {
        prim.mesh.vertexBuffer.destroy();
        prim.mesh.indexBuffer.destroy();
        if (prim.hasTexture) {
            prim.texture.destroy();
        }
    }
    glbMeshes.erase(glbMeshes.begin() + index);
    sceneDirty = true;
}

bool Viewport::selectGlbAtRay(float ndcX, float ndcY) {
    glm::mat4 invPV = glm::inverse(ProjectionModelMatrix);

    glm::vec4 nearNdc(ndcX, -ndcY, -1.0f, 1.0f);
    glm::vec4 farNdc(ndcX, -ndcY, 1.0f, 1.0f);

    glm::vec4 nearWorld = invPV * nearNdc;
    nearWorld /= nearWorld.w;
    glm::vec4 farWorld = invPV * farNdc;
    farWorld /= farWorld.w;

    glm::vec3 rayOrigin = cameraPos;
    if (viewMode != ViewMode::Perspective) {
        rayOrigin = glm::vec3(nearWorld);
    }
    glm::vec3 rayDir = glm::normalize(glm::vec3(farWorld - nearWorld));

    int closestIdx = -1;
    float closestT = INFINITY;

    auto rayAABBIntersect = [](const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& minBox, const glm::vec3& maxBox, float& t) {
        float tMin = -INFINITY;
        float tMax = INFINITY;

        for (int i = 0; i < 3; ++i) {
            if (std::abs(rd[i]) < 1e-6f) {
                if (ro[i] < minBox[i] || ro[i] > maxBox[i]) {
                    return false;
                }
            } else {
                float invD = 1.0f / rd[i];
                float t0 = (minBox[i] - ro[i]) * invD;
                float t1 = (maxBox[i] - ro[i]) * invD;
                if (t0 > t1)
                    std::swap(t0, t1);
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
                if (tMin > tMax)
                    return false;
            }
        }
        t = tMin;
        return tMax >= 0.0f;
    };

    for (int i = 0; i < (int)glbMeshes.size(); ++i) {
        const auto& gm = glbMeshes[i];
        if (!gm.visible)
            continue;
        float t = 0.0f;
        if (rayAABBIntersect(rayOrigin, rayDir, gm.minAABB, gm.maxAABB, t)) {
            if (t < closestT) {
                closestT = t;
                closestIdx = i;
            }
        }
    }

    bool changed = false;
    for (int i = 0; i < (int)glbMeshes.size(); ++i) {
        bool shouldBeSelected = (i == closestIdx);
        if (glbMeshes[i].selected != shouldBeSelected) {
            glbMeshes[i].selected = shouldBeSelected;
            changed = true;
        }
    }

    if (changed) {
        sceneDirty = true;
    }
    return closestIdx != -1;
}

bool Viewport::hasSelectedGlb() const {
    for (const auto& gm : glbMeshes) {
        if (gm.selected) {
            return true;
        }
    }
    return false;
}

void Viewport::deleteSelectedGlb() {
    for (int i = 0; i < (int)glbMeshes.size(); ++i) {
        if (glbMeshes[i].selected) {
            removeGlbMesh(i);
            break;
        }
    }
}

void Viewport::buildMatrices(float offset) {
    (void)offset;
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
    ProjectionMatrix[1][1] *= -1.0f;

    if (activeTrack && activeTrack->trackData && activeTrack->trackData->lSections.size() > 0) {
        track* curTrack = activeTrack->trackData;

        if (povPos < 0)
            povPos += curTrack->getNumPoints();
        if (povPos > curTrack->getNumPoints())
            povPos = curTrack->getNumPoints();

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

            if (gloParent->mOptions->lookAheadPovSmoothing) {
                while (nextPos < curTrack->getNumPoints()) {
                    nextNode = curTrack->getPoint(nextPos);
                    if (nextNode) {
                        nextPD = nextNode->vRelPos(curTrack->povPos.y, curTrack->povPos.x);
                        diff = nextPD - posD;
                        if (glm::length(diff) > 0.05) {
                            break;
                        }
                    }
                    nextPos++;
                }
            }

            glm::vec3 direction;
            if (gloParent->mOptions->lookAheadPovSmoothing && glm::length(diff) > 0.05) {
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

    glm::vec3 L = glm::normalize(lightDir);
    glm::vec3 N = glm::vec3(0.0f, 1.0f, 0.0f);
    float dotNL = glm::dot(N, L);

    float H = gloParent->projectGrdHeight + 0.01f;

    shadowMatrix = glm::mat4(1.0f);
    if (std::abs(dotNL) > 0.0001f) {
        shadowMatrix[0][0] = 1.0f;
        shadowMatrix[1][0] = -L.x / L.y;
        shadowMatrix[2][0] = 0.0f;
        shadowMatrix[3][0] = H * (L.x / L.y);

        shadowMatrix[0][1] = 0.0f;
        shadowMatrix[1][1] = 0.0f;
        shadowMatrix[2][1] = 0.0f;
        shadowMatrix[3][1] = H;

        shadowMatrix[0][2] = 0.0f;
        shadowMatrix[1][2] = -L.z / L.y;
        shadowMatrix[2][2] = 1.0f;
        shadowMatrix[3][2] = H * (L.z / L.y);

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

void Viewport::drawSky(VkCommandBuffer commandBuffer) {
    glm::mat4 invV = glm::inverse(glm::mat4(glm::mat3(ModelMatrix)));
    glm::mat4 invP = glm::inverse(ProjectionMatrix);
    glm::mat4 invPV = invV * invP;
    glm::mat4 skyRotation = glm::rotate(glm::mat4(1.0f),
                                        glm::radians(gloParent->mOptions->skyboxRotation),
                                        glm::vec3(0.0f, 1.0f, 0.0f));

    auto cornerRay = [&](float x, float y) {
        glm::vec4 ray = invPV * glm::vec4(x, y, 1.0f, 1.0f);
        return glm::vec4(glm::normalize(glm::vec3(skyRotation * ray)), 0.0f);
    };

    SkyUniforms uniforms = {
        .topLeft = cornerRay(-1.0f, -1.0f),
        .topRight = cornerRay(1.0f, -1.0f),
        .bottomLeft = cornerRay(-1.0f, 1.0f),
        .bottomRight = cornerRay(1.0f, 1.0f),
        .fallbackColor = glm::vec4(gloParent->mOptions->backgroundColor, 1.0f),
        .hasTexture = skyTexture ? 1 : 0,
    };
    skyPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
    VkDeviceSize zeroOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &skyMesh.vertexBuffer.buffer, &zeroOffset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Viewport::drawFloor(VkCommandBuffer commandBuffer) {
    FloorUniforms uniforms = {
        .projectionMatrix = ProjectionMatrix,
        .modelMatrix = ModelMatrix,
        .eyePos = glm::vec4(cameraPos, 1.0f),
        .floorColor = glm::vec4(gloParent->mOptions->floorColor, 1.0f),
        .mistColor = glm::vec4(gloParent->mOptions->mistColor, 1.0f),
        .lightDir = glm::vec4(lightDir, 0.0f),
        .ambientColor = glm::vec4(gloParent->mOptions->ambientLightColor, 1.0f),
        .sunColor = glm::vec4(gloParent->mOptions->sunLightColor, 1.0f),
        .floorHeight = grdHeight,
        .grdTexSize = grdTexSize,
        .opacity = 1.0f,
        .border = 1,
        .grid = gloParent->mOptions->drawGrid ? 1 : 0,
        .mistEnabled = gloParent->mOptions->mistEnabled ? 1 : 0,
        .mistNear = gloParent->mOptions->mistNear,
        .mistFar = gloParent->mOptions->mistFar,
        .ambientStrength = gloParent->mOptions->ambientLightStrength,
        .sunStrength = gloParent->mOptions->sunLightStrength,
    };
    floorPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
    VkDeviceSize zeroOffset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &floorMesh.vertexBuffer.buffer, &zeroOffset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Viewport::drawMarkers(VkCommandBuffer commandBuffer) {
    if (!activeTrack || !activeTrack->trackData)
        return;
    if (activeTrack->trackData->drawHeartline == 3 || povMode)
        return;

    track* myTrack = activeTrack->trackData;
    glm::mat4 baseAnchorBase = glm::translate(glm::mat4(1.0f), glm::vec3(myTrack->startPos)) *
                               glm::rotate(glm::mat4(1.0f), (float)TO_RAD(myTrack->startYaw - 90.f), glm::vec3(0.f, 1.f, 0.f));

    StlUniforms uniforms = {
        .projectionMatrix = ProjectionMatrix,
        .modelMatrix = ModelMatrix,
        .anchorBase = glm::mat4(1.0f),
        .eyePos = glm::vec4(cameraPos, 1.0f),
        .lightDir = glm::vec4(lightDir, 0.0f),
        .solidColor = glm::vec4(1.0f),
        .mistColor = glm::vec4(gloParent->mOptions->mistColor, 1.0f),
        .ambientColor = glm::vec4(gloParent->mOptions->ambientLightColor, 1.0f),
        .sunColor = glm::vec4(gloParent->mOptions->sunLightColor, 1.0f),
        .wire = 0,
        .edgeWidth = 0.0f,
        .mistEnabled = gloParent->mOptions->mistEnabled ? 1 : 0,
        .mistNear = gloParent->mOptions->mistNear,
        .mistFar = gloParent->mOptions->mistFar,
        .ambientStrength = gloParent->mOptions->ambientLightStrength,
        .sunStrength = gloParent->mOptions->sunLightStrength,
    };

    VkBuffer buffers[2] = {markerMesh.vertexBuffer.buffer, zeroAttributeBuffer.buffer};
    VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, markerMesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    for (size_t i = 0; i < myTrack->trainOffsets.size(); ++i) {
        const auto& offset = myTrack->trainOffsets[i];
        int totalPoints = myTrack->getNumPoints();
        if (totalPoints == 0)
            continue;

        mnode* povMarkerNode = getPOVNode();
        double povDist = 0.0;
        if (povMarkerNode) {
            povDist = povMarkerNode->fTotalLength;
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

        uniforms.anchorBase = baseAnchorBase * glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(markerScale));
        uniforms.solidColor = glm::vec4(markerColor, 1.0f);
        markerPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
        vkCmdDrawIndexed(commandBuffer, markerMesh.count, 1, 0, 0, 0);
    }

    if (showPOVMarker3D && !povMode) {
        mnode* povMarkerNode = getPOVNode();
        if (povMarkerNode) {
            glm::vec3 pos = povMarkerNode->vPos;
            uniforms.anchorBase = baseAnchorBase * glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.7f));
            uniforms.solidColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            markerPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
            vkCmdDrawIndexed(commandBuffer, markerMesh.count, 1, 0, 0, 0);
        }
    }
}

void Viewport::drawGlbs(VkCommandBuffer commandBuffer, RenderPass pass) {
    if (glbMeshes.empty())
        return;

    VkDeviceSize zeroOffset = 0;
    if (pass == RenderPass::PlanarShadow) {
        SimpleShadowUniforms uniforms = {
            .projectionMatrix = ProjectionMatrix,
            .modelMatrix = ModelMatrix,
            .anchorBase = glm::mat4(1.0f),
            .shadowMatrix = shadowMatrix,
            .uTrackLength = 0.0f,
            .heartline = 0.0f,
            .isInstanced = 0,
            .isAsset = 0,
            .shadowStrength = getShadowStrength(),
        };
        shadowGlbPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
        shadowGlbPipeline.bindStorageSet(commandBuffer, gVulkanContext->dummyStorageSet());
        for (const auto& sm : glbMeshes) {
            if (!sm.visible)
                continue;
            for (const auto& prim : sm.primitives) {
                VkBuffer buffers[2] = {prim.mesh.vertexBuffer.buffer, zeroAttributeBuffer.buffer};
                VkDeviceSize offsets[2] = {0, 0};
                vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, prim.mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, 0, 0, 0);
            }
        }
        return;
    }

    StlUniforms uniforms = {
        .projectionMatrix = ProjectionMatrix,
        .modelMatrix = ModelMatrix,
        .anchorBase = glm::mat4(1.0f),
        .eyePos = glm::vec4(cameraPos, 1.0f),
        .lightDir = glm::vec4(lightDir, 0.0f),
        .solidColor = glm::vec4(1.0f),
        .mistColor = glm::vec4(gloParent->mOptions->mistColor, 1.0f),
        .ambientColor = glm::vec4(gloParent->mOptions->ambientLightColor, 1.0f),
        .sunColor = glm::vec4(gloParent->mOptions->sunLightColor, 1.0f),
        .wire = 0,
        .edgeWidth = 0.006f,
        .mistEnabled = gloParent->mOptions->mistEnabled ? 1 : 0,
        .mistNear = gloParent->mOptions->mistNear,
        .mistFar = gloParent->mOptions->mistFar,
        .ambientStrength = gloParent->mOptions->ambientLightStrength,
        .sunStrength = gloParent->mOptions->sunLightStrength,
    };
    for (const auto& sm : glbMeshes) {
        if (!sm.visible)
            continue;
        for (const auto& prim : sm.primitives) {
            if (sm.selected) {
                uniforms.solidColor = prim.baseColorFactor * glm::vec4(1.3f, 1.3f, 0.7f, 1.0f);
            } else {
                uniforms.solidColor = prim.baseColorFactor;
            }

            VkDescriptorSet descriptorSet = prim.descriptorSets[gVulkanContext->currentFrameIndex()];
            glbPipeline.bindWithUniformsAndSet(commandBuffer, descriptorSet, &uniforms, sizeof(uniforms));

            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &prim.mesh.vertexBuffer.buffer, &zeroOffset);
            vkCmdBindIndexBuffer(commandBuffer, prim.mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, prim.indexCount, 1, 0, 0, 0);
        }
    }
}

void Viewport::drawTrack(VkCommandBuffer commandBuffer, trackHandler* hTrack, RenderPass pass) {
    if (hTrack->trackData->drawHeartline == 3)
        return;

    if (!hTrack->mMesh->isInit)
        hTrack->mMesh->init();

    trackMesh* mesh = hTrack->mMesh;
    track* myTrack = hTrack->trackData;

    glm::mat4 anchorBase = glm::translate(glm::mat4(1.0f), glm::vec3(myTrack->startPos)) *
                           glm::rotate(glm::mat4(1.0f), (float)TO_RAD(myTrack->startYaw - 90.f), glm::vec3(0.f, 1.f, 0.f));

    if (myTrack->drawHeartline != 2 && !myTrack->lSections.empty() &&
        mesh->splineStorageSet != VK_NULL_HANDLE &&
        (!mesh->instancedAssets.empty() || !mesh->instancedExtrusions.empty())) {

        auto drawInstanced = [&](track_asset_mesh_t& am, int isAsset, int smoothAlongSpline) {
            if (am.instances.empty() || am.sourceModel == nullptr || !am.baseUploaded)
                return;
            if (pass == RenderPass::PlanarShadow) {
                SimpleShadowUniforms uniforms = {
                    .projectionMatrix = ProjectionMatrix,
                    .modelMatrix = ModelMatrix,
                    .anchorBase = anchorBase,
                    .shadowMatrix = shadowMatrix,
                    .uTrackLength = (float)myTrack->getTotalLength(),
                    .heartline = (float)myTrack->fHeart,
                    .isInstanced = 1,
                    .isAsset = isAsset,
                    .shadowStrength = getShadowStrength(),
                };
                shadowInstancedPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
                shadowInstancedPipeline.bindStorageSet(commandBuffer, mesh->splineStorageSet);
            } else {
                TrackInstancedUniforms uniforms = {
                    .projectionMatrix = ProjectionMatrix,
                    .modelMatrix = ModelMatrix,
                    .anchorBase = anchorBase,
                    .eyePos = glm::vec4(cameraPos, 1.0f),
                    .lightDir = glm::vec4(lightDir, 0.0f),
                    .defaultColor = glm::vec4(hTrack->trackColors[0], 1.0f),
                    .sectionColor = glm::vec4(hTrack->trackColors[1], 1.0f),
                    .transitionColor = glm::vec4(hTrack->trackColors[2], 1.0f),
                    .mistColor = glm::vec4(gloParent->mOptions->mistColor, 1.0f),
                    .ambientColor = glm::vec4(gloParent->mOptions->ambientLightColor, 1.0f),
                    .sunColor = glm::vec4(gloParent->mOptions->sunLightColor, 1.0f),
                    .colorMode = curTrackShader,
                    .mistEnabled = gloParent->mOptions->mistEnabled ? 1 : 0,
                    .mistNear = gloParent->mOptions->mistNear,
                    .mistFar = gloParent->mOptions->mistFar,
                    .trackLength = (float)myTrack->getTotalLength(),
                    .heartline = (float)myTrack->fHeart,
                    .isAsset = isAsset,
                    .smoothAlongSpline = smoothAlongSpline,
                    .ambientStrength = gloParent->mOptions->ambientLightStrength,
                    .sunStrength = gloParent->mOptions->sunLightStrength,
                    .materialEnabled = gloParent->mOptions->trackTextureEnabled ? 1 : 0,
                };
                trackInstancedPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
                trackInstancedPipeline.bindStorageSet(commandBuffer, mesh->splineStorageSet);
            }
            VkBuffer buffers[2] = {am.baseVertexBuffer.buffer, am.instanceBuffer.buffer};
            VkDeviceSize offsets[2] = {0, 0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, am.baseIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, am.indexCount, (uint32_t)am.instances.size(), 0, 0, 0);
        };

        for (size_t assetIdx = 0; assetIdx < mesh->instancedAssets.size(); ++assetIdx) {
            if (assetIdx < myTrack->customAssets.size() && !myTrack->customAssets[assetIdx].visible)
                continue;
            auto& am = mesh->instancedAssets[assetIdx];

            drawInstanced(am, 1, am.smoothNormalsAlongSpline);
        }
        for (auto& am : mesh->instancedExtrusions)
            drawInstanced(am, 0, 1);
    }

    if (pass == RenderPass::Main && myTrack->drawHeartline != 1 &&
        !mesh->heartline.empty() && mesh->heartlineBuffer.buffer != VK_NULL_HANDLE) {
        TrackUniforms uniforms = {
            .projectionMatrix = ProjectionMatrix,
            .modelMatrix = ModelMatrix,
            .anchorBase = anchorBase,
            .eyePos = glm::vec4(cameraPos, 1.0f),
            .lightDir = glm::vec4(lightDir, 0.0f),
            .defaultColor = glm::vec4(hTrack->trackColors[3], 1.0f),
            .sectionColor = glm::vec4(hTrack->trackColors[1], 1.0f),
            .transitionColor = glm::vec4(hTrack->trackColors[2], 1.0f),
            .mistColor = glm::vec4(gloParent->mOptions->mistColor, 1.0f),
            .ambientColor = glm::vec4(gloParent->mOptions->ambientLightColor, 1.0f),
            .sunColor = glm::vec4(gloParent->mOptions->sunLightColor, 1.0f),
            .colorMode = 0,
            .mistEnabled = gloParent->mOptions->mistEnabled ? 1 : 0,
            .mistNear = gloParent->mOptions->mistNear,
            .mistFar = gloParent->mOptions->mistFar,
            .ambientStrength = gloParent->mOptions->ambientLightStrength,
            .sunStrength = gloParent->mOptions->sunLightStrength,
        };
        heartlinePipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
        VkBuffer buffers[2] = {mesh->heartlineBuffer.buffer, zeroAttributeBuffer.buffer};
        VkDeviceSize offsets[2] = {0, 0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
        vkCmdDraw(commandBuffer, (uint32_t)mesh->heartline.size(), 1, 0, 0);
    }
}

void Viewport::drawOrthoGrid(VkCommandBuffer commandBuffer) {
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

    float spacing = 10.0f;
    float subSpacing = 1.0f;

    bool drawMinor = (orthoScale <= 250.0f);

    if (viewMode == ViewMode::Side) {
        float startY = std::floor((cameraPos.y - halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endY = std::ceil((cameraPos.y + halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float startZ = std::floor((cameraPos.z - halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endZ = std::ceil((cameraPos.z + halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);

        for (float y = startY; y <= endY; y += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(y) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(y / spacing) * spacing - y) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                zAxisVertices.insert(zAxisVertices.end(), {0.0f, y, startZ, 0.0f, y, endZ});
            } else if (isMajor) {
                majorVertices.insert(majorVertices.end(), {0.0f, y, startZ, 0.0f, y, endZ});
            } else if (drawMinor) {
                minorVertices.insert(minorVertices.end(), {0.0f, y, startZ, 0.0f, y, endZ});
            }
        }
        for (float z = startZ; z <= endZ; z += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(z) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(z / spacing) * spacing - z) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                yAxisVertices.insert(yAxisVertices.end(), {0.0f, startY, z, 0.0f, endY, z});
            } else if (isMajor) {
                majorVertices.insert(majorVertices.end(), {0.0f, startY, z, 0.0f, endY, z});
            } else if (drawMinor) {
                minorVertices.insert(minorVertices.end(), {0.0f, startY, z, 0.0f, endY, z});
            }
        }
    } else if (viewMode == ViewMode::Front) {
        float startY = std::floor((cameraPos.y - halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endY = std::ceil((cameraPos.y + halfHeight) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float startX = std::floor((cameraPos.x - halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);
        float endX = std::ceil((cameraPos.x + halfWidth) / (drawMinor ? subSpacing : spacing)) * (drawMinor ? subSpacing : spacing);

        for (float y = startY; y <= endY; y += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(y) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(y / spacing) * spacing - y) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                xAxisVertices.insert(xAxisVertices.end(), {startX, y, 0.0f, endX, y, 0.0f});
            } else if (isMajor) {
                majorVertices.insert(majorVertices.end(), {startX, y, 0.0f, endX, y, 0.0f});
            } else if (drawMinor) {
                minorVertices.insert(minorVertices.end(), {startX, y, 0.0f, endX, y, 0.0f});
            }
        }
        for (float x = startX; x <= endX; x += (drawMinor ? subSpacing : spacing)) {
            bool isZero = std::abs(x) < 0.01f * (drawMinor ? subSpacing : spacing);
            bool isMajor = std::abs(std::round(x / spacing) * spacing - x) < 0.01f * (drawMinor ? subSpacing : spacing);
            if (isZero) {
                yAxisVertices.insert(yAxisVertices.end(), {x, startY, 0.0f, x, endY, 0.0f});
            } else if (isMajor) {
                majorVertices.insert(majorVertices.end(), {x, startY, 0.0f, x, endY, 0.0f});
            } else if (drawMinor) {
                minorVertices.insert(minorVertices.end(), {x, startY, 0.0f, x, endY, 0.0f});
            }
        }
    } else {
        return;
    }

    if (majorVertices.empty() && minorVertices.empty() && xAxisVertices.empty() && yAxisVertices.empty() && zAxisVertices.empty())
        return;

    glm::vec3 bg = gloParent->mOptions->backgroundColor;
    float bgBrightness = (bg.r + bg.g + bg.b) / 3.0f;
    glm::vec3 oppositeColor = (bgBrightness > 0.5f) ? glm::vec3(0.0f) : glm::vec3(1.0f);

    glm::vec3 minorColor = glm::mix(bg, oppositeColor, 0.15f);
    glm::vec3 majorColor = glm::mix(bg, oppositeColor, 0.55f);

    StlUniforms uniforms = {
        .projectionMatrix = ProjectionMatrix,
        .modelMatrix = ModelMatrix,
        .anchorBase = glm::mat4(1.0f),
        .eyePos = glm::vec4(cameraPos, 1.0f),
        .lightDir = glm::vec4(lightDir, 0.0f),
        .solidColor = glm::vec4(1.0f),
        .mistColor = glm::vec4(gloParent->mOptions->mistColor, 1.0f),
        .ambientColor = glm::vec4(1.0f),
        .sunColor = glm::vec4(1.0f),
        .wire = 0,
        .edgeWidth = 0.0f,
        .mistEnabled = 0,
        .mistNear = 0.0f,
        .mistFar = 1.0f,
        .ambientStrength = 1.2f,
        .sunStrength = 0.0f,
    };

    auto drawLineBatch = [&](const std::vector<float>& vertices, glm::vec3 color) {
        if (vertices.empty())
            return;
        uniforms.solidColor = glm::vec4(color, 1.0f);
        orthoGridPipeline.bindWithUniforms(commandBuffer, &uniforms, sizeof(uniforms));
        VkDeviceSize streamOffset = gVulkanContext->pushStreamData(vertices.data(), vertices.size() * sizeof(float));
        VkBuffer buffers[2] = {gVulkanContext->streamRingBuffer(), zeroAttributeBuffer.buffer};
        VkDeviceSize offsets[2] = {streamOffset, 0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 2, buffers, offsets);
        vkCmdDraw(commandBuffer, (uint32_t)(vertices.size() / 3), 1, 0, 0);
    };

    drawLineBatch(minorVertices, minorColor);
    drawLineBatch(majorVertices, majorColor);
    drawLineBatch(xAxisVertices, glm::vec3(0.70f, 0.20f, 0.20f));
    drawLineBatch(yAxisVertices, glm::vec3(0.20f, 0.70f, 0.20f));
    drawLineBatch(zAxisVertices, glm::vec3(0.20f, 0.20f, 0.70f));
}

void Viewport::setPOVMode(bool enabled) {
    if (enabled && activeTrack && activeTrack->trackData && activeTrack->trackData->isReferenceTrack()) {
        enabled = false;
    }
    if (povMode != enabled) {
        povMode = enabled;
        sceneDirty = true;
    }
}

void Viewport::setActiveTrack(trackHandler* track) {
    if (activeTrack != track) {
        activeTrack = track;
        if (activeTrack && activeTrack->trackData && activeTrack->trackData->isReferenceTrack()) {
            povMode = false;
            povNode = nullptr;
        }
        sceneDirty = true;
    }
}
