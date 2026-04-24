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

// device.c
QueueFamilyIndices* find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface);

// surface.c
SwapChainSupportDetails* get_support_details(VkPhysicalDevice device, VkSurfaceKHR surface);
void destroy_support_details(SwapChainSupportDetails* details);

// commands.c
void cmd_begin_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index, bool transparent);
void cmd_end_rendering(VkCommandBuffer command_buffer, PSwapchain* swapchain, uint32_t image_index);
VkCommandBuffer start_single_usage_commands(VkCommandPool command_pool, PDevice* device);
void end_single_usage_commands(VkCommandBuffer* command_buffer, VkCommandPool command_pool, PDevice* device);
PCommandPool* pigment_default_pool(Pigment* pigment);

// buffers.c
int create_buffer(VkBuffer* buffer, VkDeviceMemory* buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);
int create_vertex_buffer(VkBuffer* buffer, VkDeviceMemory* memory, VkDeviceAddress* address, const void* data, VkDeviceSize size, PDevice* device, VkCommandPool command_pool);
int create_index_buffer(VkBuffer* buffer, VkDeviceMemory* memory, const uint32_t* indices, uint32_t index_count, PDevice* device, VkCommandPool command_pool);
uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties);

// texture.c
VkImageView create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, uint32_t mip_levels, VkDevice device);
int create_vk_image(VkImage* image, VkDeviceMemory* image_memory, uint32_t width, uint32_t height, uint32_t mip_levels, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, PDevice* device);

// depth.c
void destroy_depth_resources(PSwapchain* swapchain, PDevice* device);

// uniform.c
void update_uniform_buffer(PUniformBuffers* buffers, PSwapchain* swapchain, PCamera* camera);

// camera.c
void get_view_matrix(PCamera* camera, UniformBufferObject* ubo);

#endif
