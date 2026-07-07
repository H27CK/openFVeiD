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

#include "vulkancontext.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

VulkanContext* gVulkanContext = nullptr;

static void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] %s failed: %d\n", what, result);
        abort();
    }
}

void VulkanContext::createInstance() {
    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "FVD++",
        .apiVersion = VK_API_VERSION_1_3,
    };

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    VkInstanceCreateFlags flags = 0;
#ifdef VK_KHR_portability_enumeration
    uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    for (const auto& extension : available) {
        if (strcmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    }
#endif

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = flags,
        .pApplicationInfo = &applicationInfo,
        .enabledExtensionCount = (uint32_t)extensions.size(),
        .ppEnabledExtensionNames = extensions.data(),
    };
    vkCheck(vkCreateInstance(&createInfo, nullptr, &instance), "vkCreateInstance");
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        fprintf(stderr, "[vulkan] no devices found\n");
        abort();
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (VkPhysicalDevice candidate : devices) {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &presentSupported);
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
                physicalDevice = candidate;
                graphicsQueueFamily = family;
                VkPhysicalDeviceProperties properties;
                vkGetPhysicalDeviceProperties(candidate, &properties);
                fprintf(stderr, "[vulkan] using %s\n", properties.deviceName);
                return;
            }
        }
    }
    fprintf(stderr, "[vulkan] no suitable device/queue found\n");
    abort();
}

void VulkanContext::createDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    std::vector<const char*> extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    uint32_t availableCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &availableCount, available.data());
    auto enableIfAvailable = [&](const char* name) {
        for (const auto& extension : available) {
            if (strcmp(extension.extensionName, name) == 0) {
                extensions.push_back(name);
                return;
            }
        }
    };
    enableIfAvailable("VK_KHR_portability_subset");
    enableIfAvailable(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dynamicRenderingFeatures,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = (uint32_t)extensions.size(),
        .ppEnabledExtensionNames = extensions.data(),
    };
    vkCheck(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
}

void VulkanContext::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    int framebufferWidth = 0, framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    swapchainExtent = capabilities.currentExtent;
    if (swapchainExtent.width == UINT32_MAX) {
        swapchainExtent.width = std::clamp((uint32_t)framebufferWidth,
                                           capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        swapchainExtent.height = std::clamp((uint32_t)framebufferHeight,
                                            capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = format;
        }
    }
    swapchainFormat = chosenFormat.format;

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsyncEnabled) {
        for (VkPresentModeKHR available : presentModes) {
            if (available == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                presentMode = available;
                break;
            }
            if (available == VK_PRESENT_MODE_MAILBOX_KHR)
                presentMode = available;
        }
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0)
        imageCount = std::min(imageCount, capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = chosenFormat.format,
        .imageColorSpace = chosenFormat.colorSpace,
        .imageExtent = swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
    };
    vkCheck(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain), "vkCreateSwapchainKHR");

    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr);
    swapchainImages.resize(swapchainImageCount);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data());

    swapchainImageViews.resize(swapchainImageCount);
    renderFinishedSemaphores.resize(swapchainImageCount);
    for (uint32_t i = 0; i < swapchainImageCount; ++i) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchainFormat,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[i]), "vkCreateImageView");

        VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]), "vkCreateSemaphore");
    }

    VkImageCreateInfo depthInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat,
        .extent = {swapchainExtent.width, swapchainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    vkCheck(vkCreateImage(device, &depthInfo, nullptr, &depthImage), "depth image");
    VkMemoryRequirements depthRequirements;
    vkGetImageMemoryRequirements(device, depthImage, &depthRequirements);
    VkMemoryAllocateInfo depthAllocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = depthRequirements.size,
        .memoryTypeIndex = findMemoryType(depthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    vkCheck(vkAllocateMemory(device, &depthAllocate, nullptr, &depthMemory), "depth memory");
    vkBindImageMemory(device, depthImage, depthMemory, 0);
    VkImageViewCreateInfo depthViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depthFormat,
        .subresourceRange = {VulkanContext::depthAspect, 0, 1, 0, 1},
    };
    vkCheck(vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView), "depth view");
}

uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    for (uint32_t type = 0; type < memoryProperties.memoryTypeCount; ++type) {
        if ((typeBits & (1u << type)) &&
            (memoryProperties.memoryTypes[type].propertyFlags & properties) == properties) {
            return type;
        }
    }
    fprintf(stderr, "[vulkan] no suitable memory type\n");
    abort();
}

VkSampleCountFlagBits VulkanContext::clampSampleCount(int requestedSamples) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    VkSampleCountFlags supported = properties.limits.framebufferColorSampleCounts &
                                   properties.limits.framebufferDepthSampleCounts &
                                   properties.limits.framebufferStencilSampleCounts;
    for (int candidate = std::min(requestedSamples, 8); candidate > 1; candidate /= 2) {
        if (supported & (VkSampleCountFlags)candidate)
            return (VkSampleCountFlagBits)candidate;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

VkCommandBuffer VulkanContext::beginOneTimeCommands() {
    VkCommandBufferAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = frames[0].commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    vkCheck(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer), "one time command alloc");
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanContext::endOneTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, frames[0].commandPool, 1, &commandBuffer);
}

void VulkanContext::destroySwapchain() {
    for (VkImageView view : swapchainImageViews)
        vkDestroyImageView(device, view, nullptr);
    swapchainImageViews.clear();
    for (VkSemaphore semaphore : renderFinishedSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);
    renderFinishedSemaphores.clear();
    if (depthImageView)
        vkDestroyImageView(device, depthImageView, nullptr);
    if (depthImage)
        vkDestroyImage(device, depthImage, nullptr);
    if (depthMemory)
        vkFreeMemory(device, depthMemory, nullptr);
    depthImageView = VK_NULL_HANDLE;
    depthImage = VK_NULL_HANDLE;
    depthMemory = VK_NULL_HANDLE;
    if (swapchain)
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void VulkanContext::recreateSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &width, &height);
    }
    vkDeviceWaitIdle(device);
    destroySwapchain();
    createSwapchain();
}

static constexpr VkDeviceSize uniformRingSize = 4 * 1024 * 1024;
static constexpr VkDeviceSize streamRingSize = 16 * 1024 * 1024;

uint32_t VulkanContext::pushUniformData(const void* data, size_t bytes) {
    FrameResources& frame = frames[currentFrame];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize offset = (frame.uniformRingOffset + alignment - 1) & ~(alignment - 1);
    if (offset + bytes > uniformRingSize) {
        fprintf(stderr, "[vulkan] uniform ring exhausted\n");
        abort();
    }
    memcpy(frame.uniformRingMapped + offset, data, bytes);
    frame.uniformRingOffset = offset + bytes;
    return (uint32_t)offset;
}

VkDeviceSize VulkanContext::pushStreamData(const void* data, size_t bytes) {
    FrameResources& frame = frames[currentFrame];
    VkDeviceSize offset = (frame.streamRingOffset + 3) & ~(VkDeviceSize)3;
    if (offset + bytes > streamRingSize) {
        fprintf(stderr, "[vulkan] stream ring exhausted\n");
        abort();
    }
    memcpy(frame.streamRingMapped + offset, data, bytes);
    frame.streamRingOffset = offset + bytes;
    return offset;
}

VkBuffer VulkanContext::uniformRingBuffer() {
    return frames[currentFrame].uniformRing;
}

VkBuffer VulkanContext::streamRingBuffer() {
    return frames[currentFrame].streamRing;
}

VkDescriptorSetLayout VulkanContext::storageSetLayout() {
    if (storageLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        };
        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &binding,
        };
        vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &storageLayout), "storage set layout");

        VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128};
        VkDescriptorPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = 128,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &storagePool), "storage set pool");
    }
    return storageLayout;
}

VkDescriptorSet VulkanContext::allocateStorageBufferSet(VkBuffer buffer, VkDeviceSize range) {
    VkDescriptorSetLayout layout = storageSetLayout();
    VkDescriptorSetAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = storagePool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    vkCheck(vkAllocateDescriptorSets(device, &allocateInfo, &set), "storage set alloc");
    updateStorageBufferSet(set, buffer, range);
    return set;
}

void VulkanContext::updateStorageBufferSet(VkDescriptorSet set, VkBuffer buffer, VkDeviceSize range) {
    VkDescriptorBufferInfo bufferInfo = {
        .buffer = buffer,
        .offset = 0,
        .range = range,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &bufferInfo,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

VkDescriptorSet VulkanContext::dummyStorageSet() {
    if (dummyStorage == VK_NULL_HANDLE) {
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = 64,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCreateBuffer(device, &bufferInfo, nullptr, &dummyStorageBuffer);
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, dummyStorageBuffer, &requirements);
        VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkAllocateMemory(device, &allocateInfo, nullptr, &dummyStorageMemory);
        vkBindBufferMemory(device, dummyStorageBuffer, dummyStorageMemory, 0);
        void* mapped = nullptr;
        vkMapMemory(device, dummyStorageMemory, 0, 64, 0, &mapped);
        memset(mapped, 0, 64);
        vkUnmapMemory(device, dummyStorageMemory);
        dummyStorage = allocateStorageBufferSet(dummyStorageBuffer, 64);
    }
    return dummyStorage;
}

void VulkanContext::createFrameResources() {
    auto createRing = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer,
                          VkDeviceMemory& memory, char*& mapped) {
        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "ring buffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer, &requirements);
        VkMemoryAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = findMemoryType(requirements.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };
        vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "ring memory");
        vkBindBufferMemory(device, buffer, memory, 0);
        void* mappedRaw = nullptr;
        vkMapMemory(device, memory, 0, size, 0, &mappedRaw);
        mapped = (char*)mappedRaw;
    };

    for (auto& frame : frames) {
        createRing(uniformRingSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   frame.uniformRing, frame.uniformRingMemory, frame.uniformRingMapped);
        createRing(streamRingSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   frame.streamRing, frame.streamRingMemory, frame.streamRingMapped);
        VkCommandPoolCreateInfo poolInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = graphicsQueueFamily,
        };
        vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkCheck(vkAllocateCommandBuffers(device, &allocateInfo, &frame.commandBuffer), "vkAllocateCommandBuffers");

        VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailableSemaphore), "vkCreateSemaphore");

        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlightFence), "vkCreateFence");
    }
}

bool VulkanContext::initialize(GLFWwindow* targetWindow) {
    gVulkanContext = this;
    window = targetWindow;
    createInstance();
    vkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface), "glfwCreateWindowSurface");
    pickPhysicalDevice();
    createDevice();
    createSwapchain();
    createFrameResources();
    return true;
}

void VulkanContext::setVSync(bool enabled) {
    if (vsyncEnabled == enabled)
        return;
    vsyncEnabled = enabled;
    presentModeChangePending = (swapchain != VK_NULL_HANDLE);
}

VkCommandBuffer VulkanContext::acquireFrame() {
    if (presentModeChangePending) {
        presentModeChangePending = false;
        recreateSwapchain();
    }
    FrameResources& frame = frames[currentFrame];
    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                   frame.imageAvailableSemaphore, VK_NULL_HANDLE, &currentImageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return VK_NULL_HANDLE;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquireResult, "vkAcquireNextImageKHR");
    }
    vkResetFences(device, 1, &frame.inFlightFence);

    vkResetCommandBuffer(frame.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkCheck(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");
    frame.uniformRingOffset = 0;
    frame.streamRingOffset = 0;
    frameActive = true;
    return frame.commandBuffer;
}

void VulkanContext::beginSwapchainRendering(float clearRed, float clearGreen, float clearBlue) {
    FrameResources& frame = frames[currentFrame];

    VkImageMemoryBarrier toColorAttachment = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImages[currentImageIndex],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toColorAttachment);

    VkImageMemoryBarrier depthBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depthImage,
        .subresourceRange = {VulkanContext::depthAspect, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &depthBarrier);

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews[currentImageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{clearRed, clearGreen, clearBlue, 1.0f}}},
    };
    VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}},
    };
    VkRenderingAttachmentInfo stencilAttachment = depthAttachment;
    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, swapchainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
        .pStencilAttachment = &stencilAttachment,
    };
    vkCmdBeginRendering(frame.commandBuffer, &renderingInfo);

    VkViewport viewport = {0, 0, (float)swapchainExtent.width, (float)swapchainExtent.height, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, swapchainExtent};
    vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
}

VkCommandBuffer VulkanContext::beginFrame(float clearRed, float clearGreen, float clearBlue) {
    VkCommandBuffer commandBuffer = acquireFrame();
    if (!commandBuffer)
        return VK_NULL_HANDLE;
    beginSwapchainRendering(clearRed, clearGreen, clearBlue);
    return commandBuffer;
}

void VulkanContext::endFrame() {
    if (!frameActive)
        return;
    frameActive = false;
    FrameResources& frame = frames[currentFrame];

    vkCmdEndRendering(frame.commandBuffer);

    VkImageMemoryBarrier toPresent = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchainImages[currentImageIndex],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(frame.commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toPresent);

    vkCheck(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.imageAvailableSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphores[currentImageIndex],
    };
    vkCheck(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.inFlightFence), "vkQueueSubmit");

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphores[currentImageIndex],
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &currentImageIndex,
    };
    VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else {
        vkCheck(presentResult, "vkQueuePresentKHR");
    }

    currentFrame = (currentFrame + 1) % framesInFlight;
}

void VulkanContext::waitIdle() {
    if (device)
        vkDeviceWaitIdle(device);
}

void VulkanContext::shutdown() {
    waitIdle();
    for (auto& frame : frames) {
        if (frame.inFlightFence)
            vkDestroyFence(device, frame.inFlightFence, nullptr);
        if (frame.imageAvailableSemaphore)
            vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr);
        if (frame.commandPool)
            vkDestroyCommandPool(device, frame.commandPool, nullptr);
        if (frame.uniformRing) {
            vkUnmapMemory(device, frame.uniformRingMemory);
            vkDestroyBuffer(device, frame.uniformRing, nullptr);
            vkFreeMemory(device, frame.uniformRingMemory, nullptr);
        }
        if (frame.streamRing) {
            vkUnmapMemory(device, frame.streamRingMemory);
            vkDestroyBuffer(device, frame.streamRing, nullptr);
            vkFreeMemory(device, frame.streamRingMemory, nullptr);
        }
    }
    if (dummyStorageBuffer)
        vkDestroyBuffer(device, dummyStorageBuffer, nullptr);
    if (dummyStorageMemory)
        vkFreeMemory(device, dummyStorageMemory, nullptr);
    dummyStorage = VK_NULL_HANDLE;
    dummyStorageBuffer = VK_NULL_HANDLE;
    dummyStorageMemory = VK_NULL_HANDLE;
    if (storagePool)
        vkDestroyDescriptorPool(device, storagePool, nullptr);
    if (storageLayout)
        vkDestroyDescriptorSetLayout(device, storageLayout, nullptr);
    storagePool = VK_NULL_HANDLE;
    storageLayout = VK_NULL_HANDLE;
    destroySwapchain();
    if (device)
        vkDestroyDevice(device, nullptr);
    if (surface)
        vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance)
        vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
    surface = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    if (gVulkanContext == this)
        gVulkanContext = nullptr;
}
