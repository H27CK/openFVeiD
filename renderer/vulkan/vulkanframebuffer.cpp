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

#include "vulkanframebuffer.h"
#include "vulkancontext.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

void VulkanFramebuffer::create(VulkanContext& contextRef, uint32_t targetWidth, uint32_t targetHeight,
                               VkFormat format, VkSampleCountFlagBits samples) {
    context = &contextRef;
    colorFormat = format;
    sampleCount = samples;
    width = targetWidth;
    height = targetHeight;
    createImages();

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    vkCreateSampler(context->device, &samplerInfo, nullptr, &sampler);
}

void VulkanFramebuffer::createImages() {
    VkDevice device = context->device;

    VkImageCreateInfo colorInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = colorFormat,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &colorInfo, nullptr, &colorImage) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] framebuffer color image creation failed (%ux%u)\n", width, height);
        abort();
    }
    VkMemoryRequirements colorRequirements;
    vkGetImageMemoryRequirements(device, colorImage, &colorRequirements);
    VkMemoryAllocateInfo colorAllocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = colorRequirements.size,
        .memoryTypeIndex = context->findMemoryType(colorRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkAllocateMemory(device, &colorAllocate, nullptr, &colorMemory);
    vkBindImageMemory(device, colorImage, colorMemory, 0);

    VkImageViewCreateInfo colorViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = colorImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = colorFormat,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCreateImageView(device, &colorViewInfo, nullptr, &colorView);

    if (sampleCount != VK_SAMPLE_COUNT_1_BIT) {
        VkImageCreateInfo msaaInfo = colorInfo;
        msaaInfo.samples = sampleCount;
        msaaInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        vkCreateImage(device, &msaaInfo, nullptr, &msaaColorImage);
        VkMemoryRequirements msaaRequirements;
        vkGetImageMemoryRequirements(device, msaaColorImage, &msaaRequirements);
        VkMemoryAllocateInfo msaaAllocate = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = msaaRequirements.size,
            .memoryTypeIndex = context->findMemoryType(msaaRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        vkAllocateMemory(device, &msaaAllocate, nullptr, &msaaColorMemory);
        vkBindImageMemory(device, msaaColorImage, msaaColorMemory, 0);
        VkImageViewCreateInfo msaaViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = msaaColorImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = colorFormat,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCreateImageView(device, &msaaViewInfo, nullptr, &msaaColorView);
    }

    VkImageCreateInfo depthInfo = colorInfo;
    depthInfo.format = VulkanContext::depthFormat;
    depthInfo.samples = sampleCount;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    vkCreateImage(device, &depthInfo, nullptr, &depthImage);
    VkMemoryRequirements depthRequirements;
    vkGetImageMemoryRequirements(device, depthImage, &depthRequirements);
    VkMemoryAllocateInfo depthAllocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = depthRequirements.size,
        .memoryTypeIndex = context->findMemoryType(depthRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkAllocateMemory(device, &depthAllocate, nullptr, &depthMemory);
    vkBindImageMemory(device, depthImage, depthMemory, 0);

    VkImageViewCreateInfo depthViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VulkanContext::depthFormat,
        .subresourceRange = {VulkanContext::depthAspect, 0, 1, 0, 1},
    };
    vkCreateImageView(device, &depthViewInfo, nullptr, &depthView);

    everRendered = false;
}

void VulkanFramebuffer::destroyImages() {
    VkDevice device = context->device;
    if (colorView)
        vkDestroyImageView(device, colorView, nullptr);
    if (colorImage)
        vkDestroyImage(device, colorImage, nullptr);
    if (colorMemory)
        vkFreeMemory(device, colorMemory, nullptr);
    if (msaaColorView)
        vkDestroyImageView(device, msaaColorView, nullptr);
    if (msaaColorImage)
        vkDestroyImage(device, msaaColorImage, nullptr);
    if (msaaColorMemory)
        vkFreeMemory(device, msaaColorMemory, nullptr);
    msaaColorView = VK_NULL_HANDLE;
    msaaColorImage = VK_NULL_HANDLE;
    msaaColorMemory = VK_NULL_HANDLE;
    if (depthView)
        vkDestroyImageView(device, depthView, nullptr);
    if (depthImage)
        vkDestroyImage(device, depthImage, nullptr);
    if (depthMemory)
        vkFreeMemory(device, depthMemory, nullptr);
    colorView = VK_NULL_HANDLE;
    colorImage = VK_NULL_HANDLE;
    colorMemory = VK_NULL_HANDLE;
    depthView = VK_NULL_HANDLE;
    depthImage = VK_NULL_HANDLE;
    depthMemory = VK_NULL_HANDLE;
}

void VulkanFramebuffer::resize(uint32_t targetWidth, uint32_t targetHeight) {
    if (targetWidth == width && targetHeight == height)
        return;
    context->waitIdle();
    destroyImages();
    width = targetWidth;
    height = targetHeight;
    createImages();
}

void VulkanFramebuffer::beginRendering(VkCommandBuffer commandBuffer, float clearRed,
                                       float clearGreen, float clearBlue, float clearAlpha) {
    VkImageMemoryBarrier barriers[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = everRendered ? VK_ACCESS_SHADER_READ_BIT : (VkAccessFlags)0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = colorImage,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        },
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depthImage,
            .subresourceRange = {VulkanContext::depthAspect, 0, 1, 0, 1},
        },
    };
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);

    if (msaaColorImage != VK_NULL_HANDLE) {
        VkImageMemoryBarrier msaaBarrier = barriers[0];
        msaaBarrier.srcAccessMask = 0;
        msaaBarrier.image = msaaColorImage;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &msaaBarrier);
    }

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = colorView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{clearRed, clearGreen, clearBlue, clearAlpha}}},
    };
    if (msaaColorImage != VK_NULL_HANDLE) {
        colorAttachment.imageView = msaaColorView;
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = colorView;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}},
    };
    VkRenderingAttachmentInfo stencilAttachment = depthAttachment;
    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, {width, height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
        .pStencilAttachment = &stencilAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport = {0, 0, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void VulkanFramebuffer::endRendering(VkCommandBuffer commandBuffer) {
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier toShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = colorImage,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toShaderRead);
    everRendered = true;
}

std::vector<uint8_t> VulkanFramebuffer::readPixels() {
    VkDevice device = context->device;
    VkDeviceSize size = (VkDeviceSize)width * height * 4;

    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    vkCreateBuffer(device, &bufferInfo, nullptr, &staging);
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, staging, &requirements);
    VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = context->findMemoryType(requirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    vkAllocateMemory(device, &allocateInfo, nullptr, &stagingMemory);
    vkBindBufferMemory(device, staging, stagingMemory, 0);

    VkCommandBuffer commandBuffer = context->beginOneTimeCommands();
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = colorImage,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    VkBufferImageCopy copy = {
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyImageToBuffer(commandBuffer, colorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
    VkImageMemoryBarrier backToShaderRead = toTransfer;
    backToShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    backToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    backToShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    backToShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &backToShaderRead);
    context->endOneTimeCommands(commandBuffer);

    std::vector<uint8_t> pixels(size);
    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, size, 0, &mapped);
    memcpy(pixels.data(), mapped, size);
    vkUnmapMemory(device, stagingMemory);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    return pixels;
}

void VulkanFramebuffer::destroy() {
    if (!context)
        return;
    if (sampler)
        vkDestroySampler(context->device, sampler, nullptr);
    sampler = VK_NULL_HANDLE;
    destroyImages();
}
