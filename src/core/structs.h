/**
 * Copyright 2025 Angel-Leduc TA
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

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    bool has_value;
    uint32_t value;
} optional_uint32;

struct Pigment {
    PWindow* window;
    PInstance* instance;
    PSurface* surface;
    PDevice* device;
    PSwapchain* swapchain;
    PDescriptor* descriptor;
    PPipeline* pipeline;
    PCommands* commands;
    PSync* sync;
    PImageList* images;
    PSamplerList* samplers;
    PUniformBuffers* buffers;
    uint32_t max_frames_in_flight;
    uint32_t max_images;
    uint32_t max_samplers;
    uint32_t current_image_index;
};

struct PWindow {
    PWindowInfo* info;
    GLFWwindow* window;
    bool framebuffer_resized;
    PCamera* camera;
    float last_frame_time;
    float mouse_offset_x;
    float mouse_offset_y;
    float mouse_last_x;
    float mouse_last_y;
    bool first_time_mouse;
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
    PPresentMode preferred_present_mode;
};

struct PPipeline {
    VkPipeline graphic_pipeline;
    VkPipelineLayout pipeline_layout;
};

struct PCommands {
    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;
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

struct PCamera {
    vec3 position;
    vec3 front;
    vec3 up;
    float speed;
    float roll;
    float pitch;
    float yaw;
};

struct PMeshBuffers {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
    VkDeviceAddress vertex_buffer_address;
    VkBuffer index_buffer;
    VkDeviceMemory index_buffer_memory;
};

#endif
