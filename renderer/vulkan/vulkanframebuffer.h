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
#include <cstdint>
#include <vector>

class VulkanContext;

class VulkanFramebuffer {
public:
    void create(VulkanContext& context, uint32_t width, uint32_t height, VkFormat format,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void resize(uint32_t width, uint32_t height);
    void destroy();

    void beginRendering(VkCommandBuffer commandBuffer, float clearRed, float clearGreen,
                        float clearBlue, float clearAlpha = 1.0f);
    void endRendering(VkCommandBuffer commandBuffer);

    std::vector<uint8_t> readPixels();

    VkImage colorImage = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    uint32_t width = 0;
    uint32_t height = 0;

private:
    void createImages();
    void destroyImages();

    VulkanContext* context = nullptr;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImage msaaColorImage = VK_NULL_HANDLE;
    VkDeviceMemory msaaColorMemory = VK_NULL_HANDLE;
    VkImageView msaaColorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    bool everRendered = false;
};
