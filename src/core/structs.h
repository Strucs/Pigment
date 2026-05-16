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

#ifndef PIGMENT_STRUCTS_H
#define PIGMENT_STRUCTS_H

#include "defines.h"
#include "pigment_vk.h"

#include <volk.h>

#include <stdatomic.h>

#ifndef _WIN32
    #include <pthread.h>

typedef pthread_rwlock_t pigment_rwlock_t;
typedef pthread_key_t pigment_tsd_t;

    #define pigment_rwlock_init(l) pthread_rwlock_init((l), NULL)
    #define pigment_rwlock_destroy(l) pthread_rwlock_destroy(l)
    #define pigment_rwlock_rdlock(l) pthread_rwlock_rdlock(l)
    #define pigment_rwlock_rdunlock(l) pthread_rwlock_unlock(l)
    #define pigment_rwlock_wrlock(l) pthread_rwlock_wrlock(l)
    #define pigment_rwlock_wrunlock(l) pthread_rwlock_unlock(l)

    #define pigment_tsd_init(tsd, destructor) pthread_key_create((tsd), (destructor))
    #define pigment_tsd_destroy(tsd) pthread_key_delete(tsd)
    #define pigment_tsd_get(tsd) pthread_getspecific(tsd)
    #define pigment_tsd_set(tsd, value) pthread_setspecific((tsd), (value))

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
typedef DWORD pigment_tsd_t;

    #define pigment_rwlock_init(l) (InitializeSRWLock(l), 0)
    #define pigment_rwlock_destroy(l) ((void) (l))
    #define pigment_rwlock_rdlock(l) AcquireSRWLockShared(l)
    #define pigment_rwlock_rdunlock(l) ReleaseSRWLockShared(l)
    #define pigment_rwlock_wrlock(l) AcquireSRWLockExclusive(l)
    #define pigment_rwlock_wrunlock(l) ReleaseSRWLockExclusive(l)

    #define pigment_tsd_init(tsd, destructor) (((*(tsd)) = FlsAlloc((PFLS_CALLBACK_FUNCTION) (destructor))) == FLS_OUT_OF_INDEXES ? -1 : 0)
    #define pigment_tsd_destroy(tsd) (FlsFree(tsd) ? 0 : -1)
    #define pigment_tsd_get(tsd) FlsGetValue(tsd)
    #define pigment_tsd_set(tsd, value) (FlsSetValue((tsd), (value)) ? 0 : -1)

#endif

typedef struct {
    PBool has_value;
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
    PBool validation_enabled;
    PBool best_practices_enabled;
    float depth_clear_value;
    const PQueueRequest* queue_requests;
    uint32_t queue_request_count;
    const void* extra;
} PRuntimeConfig;

struct Pigment {
    PAllocator cpu_allocator;          // CPU allocator (user-provided or default malloc/free)
    VkAllocationCallbacks vk_alloc;    // built from cpu_allocator, passed to all vkCreate*/vkDestroy*

    PInstance* instance;
    PDevice* device;
    PVkAllocator* gpu_allocator;
    PBool owns_gpu_allocator;

    PSwapchainCallbackList* swapchain_callbacks;

    PDeletionQueue* deletions;

    PRuntimeConfig config;
    PLogState* log;
};

struct PWindowRenderer {
    PSurface* surface;
    PSwapchain* swapchain;
    PSync* sync;
    PCommandPool* command_pool;
    PCommandBuffer** command_buffers;
    uint32_t current_image_index;
    PBool needs_recreate;
    PSwapchainDesc desc;
    PResourceTracker tracker;
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

struct PDeviceQueue {
    VkQueue queue;
    uint32_t family_index;
    uint32_t slot;
    PQueueFlags flags;
    VkSemaphore timeline;
    _Atomic uint64_t next_value;
};

struct PDevice {
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    PDeviceQueue* queues;
    uint32_t queue_count;
    ExtensionList* extensions;
    PBool features[P_FEATURE_COUNT];
};

struct PSurface {
    VkSurfaceKHR surface;
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
    VkColorSpaceKHR color_space;
    VkExtent2D extent;
    uint32_t current_frame;
    PImage* depth;
    PImage* color_multisample;
    VkSampleCountFlagBits samples;

    VkQueue present_queue;
    uint32_t present_family_index;
};

struct PPipeline {
    VkPipeline pipeline;
    PLayout* layout;
    PPipelineBindPoint bind_point;
    PResourceTracker tracker;
};

struct PLayout {
    VkPipelineLayout layout;
    uint32_t push_size;
    VkShaderStageFlags push_stages;
};

struct PPipelineCache {
    VkPipelineCache cache;
};

struct PPipelineBuild {
    VkShaderModule vertex_module;
    VkShaderModule fragment_module;
    uint32_t shader_stage_count;
    VkPipelineShaderStageCreateInfo shader_stages[2];
    VkSpecializationMapEntry* vertex_spec_entries;
    VkSpecializationInfo vertex_spec_info;
    VkSpecializationMapEntry* fragment_spec_entries;
    VkSpecializationInfo fragment_spec_info;
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
    const char* name;
};

struct PCommandPool {
    VkCommandPool pool;
    PQueueFlags queue_flags;
    uint32_t queue_family_index;
    PCommandPoolFlags flags;
    PCommandBuffer** buffers;
    uint32_t buffer_count;
    uint32_t buffer_capacity;
};

struct PCommandBuffer {
    VkCommandBuffer buffer;
    PCommandPool* source_pool;
    PResourceTracker** uses;
    uint32_t use_count;
    uint32_t use_capacity;
};

struct PSync {
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    PSubmitHandle* per_slot_handle;
    VkFence* present_fences;
};

struct PDescriptorSetLayout {
    VkDescriptorSetLayout layout;
};

struct PDescriptorPool {
    VkDescriptorPool pool;
    PDescriptorSet** sets;
    uint32_t set_count;
    uint32_t set_capacity;
    PBool allow_free_set;
};

struct PDescriptorSet {
    VkDescriptorSet set;
    PDescriptorPool* source_pool;
    PResourceTracker tracker;
};

typedef struct PImageViewDesc {
    PFormat format;              // 0 (P_FORMAT_UNDEFINED) = inherit image->vk_format
    PImageAspect aspect;         // 0 (P_IMAGE_ASPECT_INHERIT) = inherit image->aspect
    PImageViewType view_type;    // 0 (P_IMAGE_VIEW_TYPE_AUTO) = derive from layer_count and image create flags
    uint32_t base_layer;
    uint32_t layer_count;    // 0 = remaining
    uint32_t base_mip;
    uint32_t mip_count;    // 0 = remaining
    const char* name;
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
    VkSharingMode vk_sharing_mode;
    VkImageAspectFlags aspect;

    const char* name;

    PBool host_mapped;
    void* mapped;
    uint64_t row_pitch;
    VkMemoryPropertyFlags memory_flags;

    PImageViewCache view_cache;
    PResourceTracker tracker;
};

struct PSampler {
    VkSampler sampler;
    PResourceTracker tracker;
};

typedef struct PSwapchainCallback {
    PSwapchainRecreateFn func;
    void* user_data;
    PWindowRenderer* renderer;
    uint32_t handle;
    PBool alive;
} PSwapchainCallback;

struct PSwapchainCallbackList {
    pigment_rwlock_t lock;
    PSwapchainCallback* callbacks;
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
    VkMemoryPropertyFlags memory_flags;
    PResourceTracker tracker;
};

#endif
