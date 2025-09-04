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

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "defines.h"

struct Pigment_T {
    PWindow* window;
    PInstance* instance;
    PSurface* surface;
    PDevice* device;
    PSwapchain* swapchain;
    PRenderPass* render_pass;
    PDescriptor* descriptor;
    PPipeline* pipeline;
    PCommands* commands;
    PSync* sync;
    PTextureList* textures;
    PSamplerList* samplers;
    PBuffers* buffers;
    PCamera* camera;
    PModel* model;
    PVertexDescription* vertex_description;
    uint32_t max_frames_in_flight;
};

struct PWindow_T {
    PWindowInfo* info;
    GLFWwindow* window;
    bool framebuffer_resized;
    PCamera* camera;
    float last_frame_time;
    float mouse_last_x;
    float mouse_last_y;
    bool first_time_mouse;
};

struct PInstance_T {
    VkInstance vulkan_instance;
    ExtensionList* extensions;
    LayerList* layers;
    VkDebugUtilsMessengerEXT debug_messenger;
};

struct LayerList_T {
    const char** names;
    uint32_t size;
};

struct ExtensionList_T {
    const char** names;
    uint32_t size;
};

struct PDevice_T {
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    ExtensionList* extensions;
};

struct QueueFamilyIndices_T {
    optional_uint32 graphics_family;
    optional_uint32 present_family;
};

struct PSurface_T {
    VkSurfaceKHR surface;
};

struct QueueFamilySet_T {
    uint32_t* set;
    uint32_t size;
};

struct SwapChainSupportDetails_T {
    VkSurfaceCapabilitiesKHR capabilities;
    uint32_t formats_count;
    uint32_t present_modes_count;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* present_modes;
};

struct PSwapchain_T {
    VkSwapchainKHR swapchain;
    VkImage* images;
    VkImageView* image_views;
    uint32_t image_count;
    VkFormat image_format;
    VkExtent2D extent;
    VkFramebuffer* framebuffers;
    uint32_t current_frame;
    VkImage depth_image;
    VkDeviceMemory depth_image_memory;
    VkImageView depth_image_view;
};

struct PPipeline_T {
    VkPipeline graphic_pipeline;
    VkPipelineLayout pipeline_layout;
};

struct PRenderPass_T {
    VkRenderPass render_pass;
};

struct PCommands_T {
    VkCommandPool command_pool;
    VkCommandBuffer* command_buffers;
};

struct PSync_T {
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;
};

struct PVertexDescription_T {
    VkVertexInputBindingDescription binding_description;
    VkVertexInputAttributeDescription* attribute_descriptions;
    uint32_t attribute_descriptions_size;
};

struct PBuffers_T {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkBuffer* uniform_buffers;
    VkDeviceMemory vertex_buffer_memory;
    VkDeviceMemory index_buffer_memory;
    VkDeviceMemory* uniform_buffers_memory;
    uint32_t vertices_size;
    uint32_t indices_size;
    void** uniform_buffers_mapped;
};

struct PDescriptor_T {
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet* descriptor_sets;
};

struct PTexture_T {
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory image_memory;
    uint32_t mip_levels;
};

struct PTextureList_T {
    PTexture* textures;
    uint32_t texture_number;
    uint32_t texture_size;
};

struct PSampler_T {
    VkSampler sampler;
};

struct PSamplerList_T {
    PSampler* samplers;
    uint32_t sampler_number;
    uint32_t sampler_size;
};

struct PModel_T {
    Vertex* vertices;
    uint32_t vertices_number;
    uint32_t vertices_size;
    uint32_t* indices;
    uint32_t indices_number;
    uint32_t indices_size;
};

struct PCamera_T {
    vec3 position;
    vec3 front;
    vec3 up;
    float speed;
    float roll;
    float pitch;
    float yaw;
};

#endif
