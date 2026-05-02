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
#include "pigment_vk.h"

#include <volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifndef _WIN32
    #include <pthread.h>

typedef pthread_rwlock_t pigment_rwlock_t;

    #define pigment_rwlock_init(l) pthread_rwlock_init((l), NULL)
    #define pigment_rwlock_destroy(l) pthread_rwlock_destroy(l)
    #define pigment_rwlock_rdlock(l) pthread_rwlock_rdlock(l)
    #define pigment_rwlock_rdunlock(l) pthread_rwlock_unlock(l)
    #define pigment_rwlock_wrlock(l) pthread_rwlock_wrlock(l)
    #define pigment_rwlock_wrunlock(l) pthread_rwlock_unlock(l)

#else
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif

    #include <windows.h>

    #ifdef near
        #undef near
    #endif
    #ifdef far
        #undef far
    #endif

typedef SRWLOCK pigment_rwlock_t;

    #define pigment_rwlock_init(l) (InitializeSRWLock(l), 0)
    #define pigment_rwlock_destroy(l) ((void) (l))
    #define pigment_rwlock_rdlock(l) AcquireSRWLockShared(l)
    #define pigment_rwlock_rdunlock(l) ReleaseSRWLockShared(l)
    #define pigment_rwlock_wrlock(l) AcquireSRWLockExclusive(l)
    #define pigment_rwlock_wrunlock(l) ReleaseSRWLockExclusive(l)

#endif

typedef struct {
    bool has_value;
    uint32_t value;
} optional_uint32;

typedef struct PLogState {
    pigment_rwlock_t lock;
    PigmentLogger* loggers;
    uint32_t logger_count;
    _Atomic uint32_t active_severities;
    _Atomic uint32_t active_types;
} PLogState;

typedef struct PRuntimeConfig {
    uint32_t max_frames_in_flight;
    uint32_t max_images;
    uint32_t max_samplers;
    bool validation_enabled;
    bool best_practices_enabled;
    float depth_clear_value;
    const void* extra;
} PRuntimeConfig;

struct Pigment {
    PWindow** windows;
    PWindowRenderer** renderers;
    uint32_t window_count;
    uint32_t window_capacity;

    PInstance* instance;
    PDevice* device;
    PVkAllocator* allocator;
    bool owns_allocator;
    PDescriptor* descriptor;
    PCommandPoolList* command_pools;
    PImageList* images;
    PSamplerList* samplers;
    PPipelineList* pipelines;
    PLayoutList* layouts;

    PRuntimeConfig config;
    PLogState* log;
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
    uint32_t graphics_family_index;
    uint32_t present_family_index;
    ExtensionList* extensions;
    bool features[P_FEATURE_COUNT];
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
    PVkAllocation* depth_image_allocation;
    VkImageView depth_image_view;
    VkFormat depth_format;
};

struct PPipeline {
    VkPipeline pipeline;
    PLayout* layout;
};

struct PPipelineList {
    PPipeline** pipelines;
    uint32_t count;
    uint32_t capacity;
};

struct PLayout {
    VkPipelineLayout layout;
    uint32_t push_size;
    VkShaderStageFlags push_stages;
};

struct PLayoutList {
    PLayout** layouts;
    uint32_t count;
    uint32_t capacity;
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
    VkPipelineColorBlendAttachmentState* blend_attachments;
    uint32_t blend_attachment_count;
    VkPipelineColorBlendStateCreateInfo color_blend;
    VkDynamicState* dynamic_state_list;
    uint32_t dynamic_state_count;
    VkPipelineDynamicStateCreateInfo dynamic;
    VkFormat* color_formats;
    uint32_t color_format_count;
    VkPipelineRenderingCreateInfoKHR rendering;
    PLayout* layout;
};

struct PCommandPool {
    VkCommandPool pool;
    PQueueFamily queue_family;
    uint32_t queue_family_index;
    PCommandPoolFlags flags;
};

struct PCommandPoolList {
    PCommandPool** pools;
    uint32_t count;
    uint32_t capacity;
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

struct PDescriptor {
    VkDescriptorSetLayout descriptor_set_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet* descriptor_sets;
};

struct PImage {
    VkImage image;
    VkImageView image_view;
    PVkAllocation* image_allocation;
    uint32_t mip_levels;
};

struct PImageList {
    PImage* images;
    uint32_t count;
    uint32_t capacity;
};

struct PSampler {
    VkSampler sampler;
};

struct PSamplerList {
    PSampler* samplers;
    PSamplerDesc* descs;
    uint32_t count;
    uint32_t capacity;
};

struct PBuffer {
    VkBuffer buffer;
    PVkAllocation* allocation;
    VkDeviceAddress address;
    void* mapped;
    uint64_t size;
};

#endif
