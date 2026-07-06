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

#include "vulkantexture.h"
#include "vulkancontext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void VulkanTexture::createInternal(VulkanContext& context, uint32_t width, uint32_t height,
                                   const uint8_t* rgba, uint32_t layers, bool cube, bool repeat) {
    device = context.device;
    VkDeviceSize totalSize = (VkDeviceSize)width * height * 4 * layers;
    uint32_t mipLevels = 1 + (uint32_t)std::floor(std::log2((double)std::max(width, height)));

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : (VkImageCreateFlags)0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] texture image creation failed\n");
        abort();
    }
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image, &requirements);
    VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkAllocateMemory(device, &allocateInfo, nullptr, &memory);
    vkBindImageMemory(device, image, memory, 0);

    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = totalSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(device, &stagingInfo, nullptr, &staging);
    VkMemoryRequirements stagingRequirements;
    vkGetBufferMemoryRequirements(device, staging, &stagingRequirements);
    VkMemoryAllocateInfo stagingAllocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = stagingRequirements.size,
        .memoryTypeIndex = context.findMemoryType(stagingRequirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(device, &stagingAllocate, nullptr, &stagingMemory);
    vkBindBufferMemory(device, staging, stagingMemory, 0);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, totalSize, 0, &mapped);
    memcpy(mapped, rgba, totalSize);
    vkUnmapMemory(device, stagingMemory);

    VkCommandBuffer commandBuffer = context.beginOneTimeCommands();
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layers},
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(commandBuffer, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    int32_t mipWidth = (int32_t)width;
    int32_t mipHeight = (int32_t)height;
    for (uint32_t level = 1; level < mipLevels; ++level) {
        VkImageMemoryBarrier previousToSource = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, layers},
        };
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &previousToSource);

        int32_t nextWidth = std::max(mipWidth / 2, 1);
        int32_t nextHeight = std::max(mipHeight / 2, 1);
        VkImageBlit blit = {
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, layers},
            .srcOffsets = {{0, 0, 0}, {mipWidth, mipHeight, 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layers},
            .dstOffsets = {{0, 0, 0}, {nextWidth, nextHeight, 1}},
        };
        vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    VkImageMemoryBarrier sourcesToShaderRead = toTransfer;
    sourcesToShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sourcesToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourcesToShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    sourcesToShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sourcesToShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels > 1 ? mipLevels - 1 : 1, 0, layers};
    if (mipLevels > 1) {
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &sourcesToShaderRead);
    }
    VkImageMemoryBarrier lastToShaderRead = toTransfer;
    lastToShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    lastToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    lastToShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    lastToShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lastToShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, layers};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &lastToShaderRead);
    context.endOneTimeCommands(commandBuffer);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, layers},
    };
    vkCreateImageView(device, &viewInfo, nullptr, &view);

    VkSamplerAddressMode addressMode = repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
}

void VulkanTexture::create2d(VulkanContext& context, uint32_t width, uint32_t height,
                             const uint8_t* rgba, bool repeat) {
    createInternal(context, width, height, rgba, 1, false, repeat);
}

void VulkanTexture::createCube(VulkanContext& context, uint32_t width, uint32_t height,
                               const uint8_t* rgbaFaces) {
    createInternal(context, width, height, rgbaFaces, 6, true, false);
}

void VulkanTexture::destroy() {
    if (!device)
        return;
    if (sampler)
        vkDestroySampler(device, sampler, nullptr);
    if (view)
        vkDestroyImageView(device, view, nullptr);
    if (image)
        vkDestroyImage(device, image, nullptr);
    if (memory)
        vkFreeMemory(device, memory, nullptr);
    sampler = VK_NULL_HANDLE;
    view = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
}
