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
    PCommandPoolList* command_pools;
    PPipelineList* pipelines;
    PLayoutList* layouts;
    PResizeCallbackList* resize_callbacks;

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
    PCommandBuffer** command_buffers;
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
    PImage* depth;
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
    PDescriptorSetLayout** set_layouts;
    uint32_t set_layout_count;
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

struct PCommandBuffer {
    VkCommandBuffer buffer;
    VkCommandPool source_pool;
};

struct PSync {
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* in_flight_fences;
};

struct PDescriptorSetLayout {
    VkDescriptorSetLayout layout;
};

struct PDescriptorPool {
    VkDescriptorPool pool;
    PDescriptorSet** sets;
    uint32_t set_count;
    uint32_t set_capacity;
};

struct PDescriptorSet {
    VkDescriptorSet set;
};

typedef struct PImageViewDesc {
    PFormat format;             // 0 (P_FORMAT_UNDEFINED) = inherit image->vk_format
    PImageAspect aspect;        // 0 (P_IMAGE_ASPECT_INHERIT) = inherit image->aspect
    PImageViewType view_type;   // 0 (P_IMAGE_VIEW_TYPE_AUTO) = derive from layer_count and image create flags
    uint32_t base_layer;
    uint32_t layer_count;       // 0 = remaining
    uint32_t base_mip;
    uint32_t mip_count;         // 0 = remaining
} PImageViewDesc;

struct PImageView {
    PImageViewDesc desc;
    VkImageView view;
};

typedef struct PImageViewCache {
    PImageView** views;
    uint32_t count;
    uint32_t capacity;
} PImageViewCache;

struct PImage {
    VkImage image;
    PVkAllocation* image_allocation;

    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t mip_levels;
    uint32_t array_layers;
    VkFormat vk_format;
    VkImageUsageFlags vk_usage;
    VkImageType vk_image_type;
    VkImageCreateFlags vk_create_flags;
    VkSampleCountFlagBits vk_samples;
    VkImageAspectFlags aspect;

    PImageViewCache view_cache;
};

struct PSampler {
    VkSampler sampler;
};

typedef struct PResizeCallback {
    PSwapchainResizeFn func;
    void* user_data;
    uint32_t handle;
    bool alive;
} PResizeCallback;

struct PResizeCallbackList {
    pigment_rwlock_t lock;
    PResizeCallback* callbacks;
    uint32_t count;
    uint32_t capacity;
    _Atomic uint32_t next_handle;
};

struct PBuffer {
    VkBuffer buffer;
    PVkAllocation* allocation;
    VkDeviceAddress address;
    void* mapped;
    uint64_t size;
};

#endif
