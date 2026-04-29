/**
 * Copyright 2026 Angel-Leduc TA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Private declarations for internal use. It must not be used in public headers.

#ifndef PIGMENT_INTERNAL_H
#define PIGMENT_INTERNAL_H

#include "structs.h"

static inline bool name_in_list(const char* const* list, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(list[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

static inline bool extension_available(const VkExtensionProperties* available, uint32_t count, const char* name)
{
    for(uint32_t i = 0; i < count; i++)
    {
        if(strcmp(available[i].extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

// window.c
const char* const* window_get_vk_instance_extensions(Pigment* pigment, uint32_t* out_count);
bool window_create_vk_surface(Pigment* pigment, PWindow* window, VkSurfaceKHR* out_surface);

// device.c
QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

// surface.c
SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface);
void destroy_support_details(SwapChainSupportDetails* details);

// commands.c
void cmd_begin_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index, bool transparent);
void cmd_end_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);
VkCommandBuffer start_single_usage_commands(Pigment* pigment, VkCommandPool command_pool);
void end_single_usage_commands(Pigment* pigment, VkCommandBuffer* command_buffer, VkCommandPool command_pool);
PCommandPool* pigment_default_pool(Pigment* pigment);

// buffers.c
int create_buffer(Pigment* pigment, VkBuffer* buffer, PVkAllocation** allocation, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
int create_vertex_buffer(Pigment* pigment, VkBuffer* buffer, PVkAllocation** allocation, VkDeviceAddress* address, const void* data, VkDeviceSize size, VkCommandPool command_pool);
int create_index_buffer(Pigment* pigment, VkBuffer* buffer, PVkAllocation** allocation, const uint32_t* indices, uint32_t index_count, VkCommandPool command_pool);

// texture.c
VkImageView create_image_view(Pigment* pigment, VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels);
int create_vk_image(Pigment* pigment, VkImage* image, PVkAllocation** allocation, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties);

// depth.c
void destroy_depth_resources(Pigment* pigment, PSwapchain* swapchain);

// uniform.c
void update_uniform_buffer(PUniformBuffers* buffers, PSwapchain* swapchain, PCamera* camera);

#endif
