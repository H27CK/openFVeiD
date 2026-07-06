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

class VulkanContext;

class VulkanTexture {
public:
    void create2d(VulkanContext& context, uint32_t width, uint32_t height, const uint8_t* rgba,
                  bool repeat = true);
    void createCube(VulkanContext& context, uint32_t width, uint32_t height, const uint8_t* rgbaFaces);
    void destroy();

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

private:
    void createInternal(VulkanContext& context, uint32_t width, uint32_t height,
                        const uint8_t* rgba, uint32_t layers, bool cube, bool repeat);
    VkDevice device = VK_NULL_HANDLE;
};
