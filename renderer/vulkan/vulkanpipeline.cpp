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

#include "vulkanpipeline.h"
#include "vulkancontext.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

static std::vector<char> readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        fprintf(stderr, "[vulkan] cannot open %s\n", path.c_str());
        return {};
    }
    size_t size = (size_t)file.tellg();
    std::vector<char> content(size);
    file.seekg(0);
    file.read(content.data(), size);
    return content;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& spirv) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size(),
        .pCode = (const uint32_t*)spirv.data(),
    };
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] shader module creation failed\n");
    }
    return module;
}

bool VulkanPipeline::create(VulkanContext& contextRef, const VulkanPipelineConfig& pipelineConfig) {
    context = &contextRef;
    config = pipelineConfig;
    VkDevice device = context->device;

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back({
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    });
    for (uint32_t image = 0; image < config.sampledImageCount; ++image) {
        bindings.push_back({
            .binding = 1 + image,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        });
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = (uint32_t)bindings.size(),
        .pBindings = bindings.data(),
    };
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
        return false;

    const uint32_t frameCount = VulkanContext::maxFramesInFlight;
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, frameCount},
    };
    if (config.sampledImageCount > 0)
        poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frameCount * config.sampledImageCount});
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = frameCount,
        .poolSizeCount = (uint32_t)poolSizes.size(),
        .pPoolSizes = poolSizes.data(),
    };
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        return false;

    std::vector<VkDescriptorSetLayout> setLayouts(frameCount, descriptorSetLayout);
    VkDescriptorSetAllocateInfo setAllocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = frameCount,
        .pSetLayouts = setLayouts.data(),
    };
    descriptorSets.resize(frameCount);
    if (vkAllocateDescriptorSets(device, &setAllocate, descriptorSets.data()) != VK_SUCCESS)
        return false;

    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        VkDescriptorBufferInfo descriptorBuffer = {
            .buffer = context->frameUniformRingBuffer(frame),
            .offset = 0,
            .range = config.uniformBufferSize,
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSets[frame],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .pBufferInfo = &descriptorBuffer,
        };
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    std::vector<char> vertexSpirv = readBinaryFile(config.vertexSpirvPath);
    std::vector<char> fragmentSpirv = readBinaryFile(config.fragmentSpirvPath);
    if (vertexSpirv.empty() || fragmentSpirv.empty())
        return false;
    VkShaderModule vertexModule = createShaderModule(device, vertexSpirv);
    VkShaderModule fragmentModule = createShaderModule(device, fragmentSpirv);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexModule,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentModule,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = (uint32_t)config.vertexBindings.size(),
        .pVertexBindingDescriptions = config.vertexBindings.data(),
        .vertexAttributeDescriptionCount = (uint32_t)config.vertexAttributes.size(),
        .pVertexAttributeDescriptions = config.vertexAttributes.data(),
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = config.topology,
    };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = config.sampleCount,
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    if (config.stencilMode == VulkanPipelineConfig::StencilMode::writeReference) {
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.front = {
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = VK_STENCIL_OP_REPLACE,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .compareMask = 0xFF,
            .writeMask = 0xFF,
            .reference = 1,
        };
        depthStencil.back = depthStencil.front;
    } else if (config.stencilMode == VulkanPipelineConfig::StencilMode::testReferenceIncrement) {
        depthStencil.stencilTestEnable = VK_TRUE;
        depthStencil.front = {
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = VK_COMPARE_OP_EQUAL,
            .compareMask = 0xFF,
            .writeMask = 0xFF,
            .reference = 1,
        };
        depthStencil.back = depthStencil.front;
    }
    VkPipelineColorBlendAttachmentState blendAttachment = {
        .blendEnable = config.alphaBlend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment,
    };
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates,
    };

    VkDescriptorSetLayout setLayoutsForPipeline[2] = {descriptorSetLayout, VK_NULL_HANDLE};
    uint32_t setLayoutCount = 1;
    if (config.usesStorageSet) {
        setLayoutsForPipeline[1] = context->storageSetLayout();
        setLayoutCount = 2;
    }
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = setLayoutCount,
        .pSetLayouts = setLayoutsForPipeline,
    };
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &layout) != VK_SUCCESS)
        return false;

    VkFormat colorFormat = context->swapchainFormat;
    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = VulkanContext::depthFormat,
        .stencilAttachmentFormat = VulkanContext::depthFormat,
    };
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = layout,
    };
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device, vertexModule, nullptr);
    vkDestroyShaderModule(device, fragmentModule, nullptr);

    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] pipeline creation failed for %s\n", config.vertexSpirvPath.c_str());
        return false;
    }
    return true;
}

void VulkanPipeline::bindWithUniforms(VkCommandBuffer commandBuffer, const void* data, size_t bytes) {
    uint32_t dynamicOffset = context->pushUniformData(data, bytes);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &descriptorSets[context->currentFrameIndex()], 1, &dynamicOffset);
}

void VulkanPipeline::bindTexture(uint32_t imageIndex, VkImageView imageView, VkSampler sampler) {
    for (size_t frame = 0; frame < descriptorSets.size(); ++frame) {
        VkDescriptorImageInfo imageInfo = {
            .sampler = sampler,
            .imageView = imageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSets[frame],
            .dstBinding = 1 + imageIndex,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo,
        };
        vkUpdateDescriptorSets(context->device, 1, &write, 0, nullptr);
    }
}

void VulkanPipeline::bindStorageSet(VkCommandBuffer commandBuffer, VkDescriptorSet storageSet) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1,
                            &storageSet, 0, nullptr);
}

void VulkanPipeline::destroy() {
    if (!context)
        return;
    VkDevice device = context->device;
    if (pipeline)
        vkDestroyPipeline(device, pipeline, nullptr);
    if (layout)
        vkDestroyPipelineLayout(device, layout, nullptr);
    if (descriptorPool)
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorSetLayout)
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    pipeline = VK_NULL_HANDLE;
    layout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorSetLayout = VK_NULL_HANDLE;
}
