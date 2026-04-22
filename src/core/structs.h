/**
 * Copyright 2025-2026 Angel-Leduc TA
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

#ifndef STRUCT_H
#define STRUCT_H

#include "defines.h"

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    bool has_value;
    uint32_t value;
} optional_uint32;

struct Pigment {
    PWindow** windows;
    PWindowRenderer** renderers;
    uint32_t window_count;
    uint32_t window_capacity;
    PInstance* instance;
    PDevice* device;
    PDescriptor* descriptor;
    PCommandPools* command_pools;
    PImageList* images;
    PSamplerList* samplers;
    PUniformBuffers* buffers;
    uint32_t max_frames_in_flight;
    uint32_t max_images;
    uint32_t max_samplers;
};

struct PWindow {
    PWindowInfo* info;
    SDL_Window* window;
    bool should_close;
};

struct PWindowRenderer {
    PSurface* surface;
    PSwapchain* swapchain;
    PSync* sync;
    PCommandBuffers* command_buffers;
    uint32_t current_image_index;
    bool framebuffer_resized;
    uint32_t pending_width;
    uint32_t pending_height;
    PPresentMode requested_present_mode;
    bool transparent_framebuffer;
};

struct PInstance {
    VkInstance vulkan_instance;
    ExtensionList* extensions;
    LayerList* layers;
    VkDebugUtilsMessengerEXT debug_messenger;
};

struct LayerList {
    const char** names;
    uint32_t size;
};

struct ExtensionList {
    const char** names;
    uint32_t size;
};

struct PDevice {
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    ExtensionList* extensions;
};

struct QueueFamilyIndices {
    optional_uint32 graphics_family;
    optional_uint32 present_family;
};

struct PSurface {
    VkSurfaceKHR surface;
};

struct QueueFamilySet {
    uint32_t* set;
    uint32_t size;
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formats_count;
    uint32_t present_modes_count;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* present_modes;
};

struct PSwapchain {
    VkSwapchainKHR swapchain;
    VkImage* images;
    VkImageView* image_views;
    uint32_t image_count;
    VkFormat image_format;
    VkExtent2D extent;
    uint32_t current_frame;
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
    VkFormat depth_format;
};

struct PPipelines {
    VkPipeline* graphic_pipelines;
    VkPipelineLayout* pipeline_layouts;
    uint32_t count;
};

struct PPipelineBuild {
    VkShaderModule vertex_module;
    VkShaderModule fragment_module;
    uint32_t shader_stage_count;
    VkPipelineShaderStageCreateInfo shader_stages[2];
    VkPipelineVertexInputStateCreateInfo vertex_input;
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    VkPipelineColorBlendAttachmentState blend_attachment;
    VkPipelineColorBlendStateCreateInfo color_blend;
    VkDynamicState* dynamic_state_list;
    uint32_t dynamic_state_count;
    VkPipelineDynamicStateCreateInfo dynamic;
    VkFormat color_format;
    VkPipelineRenderingCreateInfoKHR rendering;
    VkPipelineLayout layout;
};

struct PCommandPools {
    VkCommandPool* pools;
    uint32_t pool_count;
    uint32_t pool_capacity;
};

struct PCommandBuffers {
    VkCommandBuffer* buffers;
    VkCommandPool source_pool;
};

struct PSync {
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;
};

struct PDrawPushConstants {
    mat4 world_matrix;
    VkDeviceAddress vertex_buffer;
    uint32_t image_index;
    uint32_t sampler_index;
};

struct PUniformBuffers {
    VkBuffer* uniform_buffers;
    VkDeviceMemory* uniform_buffers_memory;
    void** uniform_buffers_mapped;
};

struct PDescriptor {
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet* descriptor_sets;
};

struct PImage {
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory image_memory;
    uint32_t mip_levels;
};

struct PImageList {
    PImage* images;
    uint32_t image_number;
    uint32_t image_size;
};

struct PSampler {
    VkSampler sampler;
};

struct PSamplerList {
    PSampler* samplers;
    PSamplerDesc* descs;
    uint32_t sampler_number;
    uint32_t sampler_size;
};

struct PMeshBuffers {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkDeviceAddress vertex_buffer_address;
    VkBuffer index_buffer;
    VkDeviceMemory index_buffer_memory;
};

#endif
