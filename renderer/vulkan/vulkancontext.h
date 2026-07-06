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

#include <vulkan/vulkan.h>
#include <vector>

struct GLFWwindow;

class VulkanContext {
public:
    bool initialize(GLFWwindow* window);
    void shutdown();

    VkCommandBuffer acquireFrame();
    void beginSwapchainRendering(float clearRed, float clearGreen, float clearBlue);
    VkCommandBuffer beginFrame(float clearRed, float clearGreen, float clearBlue);
    void endFrame();

    void waitIdle();

    void setVSync(bool enabled);

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);
    VkSampleCountFlagBits clampSampleCount(int requestedSamples);
    VkCommandBuffer beginOneTimeCommands();
    void endOneTimeCommands(VkCommandBuffer commandBuffer);
    int currentFrameIndex() const { return currentFrame; }
    VkCommandBuffer currentCommandBuffer() { return frameActive ? frames[currentFrame].commandBuffer : VK_NULL_HANDLE; }

    uint32_t pushUniformData(const void* data, size_t bytes);
    VkDeviceSize pushStreamData(const void* data, size_t bytes);
    VkBuffer uniformRingBuffer();
    VkBuffer streamRingBuffer();
    VkBuffer frameUniformRingBuffer(uint32_t frame) { return frames[frame].uniformRing; }

    VkDescriptorSetLayout storageSetLayout();
    VkDescriptorSet allocateStorageBufferSet(VkBuffer buffer, VkDeviceSize range);
    void updateStorageBufferSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize range);
    VkDescriptorSet dummyStorageSet();

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    static constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
    static constexpr VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    VkExtent2D swapchainExtent = {};
    uint32_t swapchainImageCount = 0;
    static constexpr int maxFramesInFlight = 2;

private:
    void createInstance();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void destroySwapchain();
    void recreateSwapchain();
    void createFrameResources();

    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    static constexpr int framesInFlight = maxFramesInFlight;
    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
        VkBuffer uniformRing = VK_NULL_HANDLE;
        VkDeviceMemory uniformRingMemory = VK_NULL_HANDLE;
        char* uniformRingMapped = nullptr;
        VkDeviceSize uniformRingOffset = 0;
        VkBuffer streamRing = VK_NULL_HANDLE;
        VkDeviceMemory streamRingMemory = VK_NULL_HANDLE;
        char* streamRingMapped = nullptr;
        VkDeviceSize streamRingOffset = 0;
    };
    FrameResources frames[framesInFlight];
    VkDescriptorSetLayout storageLayout = VK_NULL_HANDLE;
    VkDescriptorPool storagePool = VK_NULL_HANDLE;
    VkDescriptorSet dummyStorage = VK_NULL_HANDLE;
    VkBuffer dummyStorageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory dummyStorageMemory = VK_NULL_HANDLE;
    int currentFrame = 0;
    uint32_t currentImageIndex = 0;
    bool frameActive = false;
    bool vsyncEnabled = true;
    bool presentModeChangePending = false;
};

extern VulkanContext* gVulkanContext;
