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

#include "vulkanbuffer.h"
#include "vulkancontext.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

void VulkanBuffer::create(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage) {
    device = context.device;

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] buffer creation failed (%llu bytes)\n", (unsigned long long)size);
        abort();
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] buffer memory allocation failed\n");
        abort();
    }
    vkBindBufferMemory(device, buffer, memory, 0);
    vkMapMemory(device, memory, 0, size, 0, &mapped);
    capacity = size;
}

void VulkanBuffer::destroy() {
    if (!device)
        return;
    if (memory && mapped)
        vkUnmapMemory(device, memory);
    if (buffer)
        vkDestroyBuffer(device, buffer, nullptr);
    if (memory)
        vkFreeMemory(device, memory, nullptr);
    buffer = VK_NULL_HANDLE;
    memory = VK_NULL_HANDLE;
    mapped = nullptr;
    capacity = 0;
}

void VulkanBuffer::write(const void* data, size_t bytes, size_t offset) {
    memcpy((char*)mapped + offset, data, bytes);
}

void VulkanBuffer::ensureCapacity(VulkanContext& context, VkDeviceSize requiredSize, VkBufferUsageFlags usage) {
    if (requiredSize <= capacity)
        return;
    context.waitIdle();
    destroy();
    create(context, requiredSize, usage);
}
