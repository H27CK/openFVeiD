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
#include <string>
#include <vector>

class VulkanContext;

struct VulkanPipelineConfig {
    std::string vertexSpirvPath;
    std::string fragmentSpirvPath;
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depthTest = true;
    bool depthWrite = true;
    bool alphaBlend = false;
    enum class StencilMode { none, writeReference, testReferenceIncrement };
    StencilMode stencilMode = StencilMode::none;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    uint32_t sampledImageCount = 0;
    uint32_t uniformBufferSize = 0;
    bool usesStorageSet = false;
};

class VulkanPipeline {
public:
    bool create(VulkanContext& context, const VulkanPipelineConfig& config);
    void destroy();

    void bindWithUniforms(VkCommandBuffer commandBuffer, const void* data, size_t bytes);
    void bindTexture(uint32_t imageIndex, VkImageView imageView, VkSampler sampler);
    void bindStorageSet(VkCommandBuffer commandBuffer, VkDescriptorSet storageSet);

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

private:
    VulkanContext* context = nullptr;
    VulkanPipelineConfig config;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
};
