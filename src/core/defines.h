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

#ifndef PIGMENT_DEFINES_H
#define PIGMENT_DEFINES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "attributes.h"

#include <stdint.h>

typedef unsigned char PBool;
#define P_TRUE 1
#define P_FALSE 0

typedef enum PResult {
    PIGMENT_SUCCESS             = 0,
    PIGMENT_ERROR               = 1,
    PIGMENT_ERROR_OUT_OF_MEMORY = 2,
    PIGMENT_ERROR_VULKAN        = 3,
} PResult;

#define PIGMENT_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t) (major)) << 22U) | (((uint32_t) (minor)) << 12U) | ((uint32_t) (patch)))

#define PIGMENT_DEFAULT_MAX_FRAMES_IN_FLIGHT 2

typedef struct PAppInfo {
    const char* app_name;
    uint32_t app_version;
} PAppInfo;

typedef struct PDeviceQueue PDeviceQueue;

typedef struct PSubmitHandle {
    PDeviceQueue* queue;
    uint64_t value;
} PSubmitHandle;

typedef enum PigmentLogSeverity {
    PIGMENT_LOG_TRACE_BIT = 1 << 0,
    PIGMENT_LOG_DEBUG_BIT = 1 << 1,
    PIGMENT_LOG_INFO_BIT  = 1 << 2,
    PIGMENT_LOG_WARN_BIT  = 1 << 3,
    PIGMENT_LOG_ERROR_BIT = 1 << 4,
} PigmentLogSeverity;

typedef enum PigmentLogType {
    PIGMENT_LOG_TYPE_GENERAL_BIT     = 1 << 0,
    PIGMENT_LOG_TYPE_VALIDATION_BIT  = 1 << 1,
    PIGMENT_LOG_TYPE_PERFORMANCE_BIT = 1 << 2,
} PigmentLogType;

typedef enum PFeature {
    P_FEATURE_DEPTH_BOUNDS_TEST            = 0,
    P_FEATURE_WIREFRAME_RASTERIZATION      = 1,
    P_FEATURE_MULTI_DRAW_INDIRECT          = 2,
    P_FEATURE_DRAW_INDIRECT_FIRST_INSTANCE = 3,
    P_FEATURE_DRAW_INDIRECT_COUNT          = 4,
    P_FEATURE_HOST_IMAGE_COPY              = 5,
    P_FEATURE_MEMORY_BUDGET                = 6,
    P_FEATURE_TEXTURE_COMPRESSION_BC       = 7,
    P_FEATURE_TEXTURE_COMPRESSION_ETC2     = 8,
    P_FEATURE_TEXTURE_COMPRESSION_ASTC_LDR = 9,
    P_FEATURE_COUNT    // size of the device feature array
} PFeature;

typedef enum {
    P_PRESENT_MODE_IMMEDIATE    = 0,    // no vsync, uncapped, may tear
    P_PRESENT_MODE_MAILBOX      = 1,    // triple buffering, no tearing
    P_PRESENT_MODE_FIFO         = 2,    // vsync (guaranteed available)
    P_PRESENT_MODE_FIFO_RELAXED = 3,    // vsync, tears if frame is late
} PPresentMode;

typedef enum PColorSpace {
    P_COLOR_SPACE_SRGB_NONLINEAR       = 0,    // SDR sRGB nonlinear (default)
    P_COLOR_SPACE_DISPLAY_P3_NONLINEAR = 1,    // wide gamut, SDR
    P_COLOR_SPACE_EXTENDED_SRGB_LINEAR = 2,    // scRGB, 16-bit float, linear, HDR-capable
    P_COLOR_SPACE_BT2020_LINEAR        = 3,    // BT.2020 linear
    P_COLOR_SPACE_HDR10_ST2084         = 4,    // HDR10 / PQ (10-bit, perceptual quantizer)
    P_COLOR_SPACE_HDR10_HLG            = 5,    // HDR10 / HLG (broadcast)
} PColorSpace;

#define P_PRESENT_MODE_DEFAULT P_PRESENT_MODE_MAILBOX

typedef enum PQueueFlags {
    P_QUEUE_GRAPHICS_BIT = 1 << 0,
    P_QUEUE_COMPUTE_BIT  = 1 << 1,
    P_QUEUE_TRANSFER_BIT = 1 << 2,
} PQueueFlags;

/**
 * Queues to create at device init.
 */
typedef struct PQueueRequest {
    PQueueFlags required;
    uint32_t count;    // 0 = 1. Distinct queues wanted, clamped to what the hardware exposes.
    float priority;
} PQueueRequest;

typedef enum PCommandPoolFlags {
    P_COMMAND_POOL_FLAG_NONE         = 0,
    P_COMMAND_POOL_FLAG_TRANSIENT    = 1 << 0,
    P_COMMAND_POOL_FLAG_RESET_BUFFER = 1 << 1,
} PCommandPoolFlags;

typedef enum PCommandBufferUsage {
    P_CMD_BUFFER_USAGE_DEFAULT                  = 0,
    P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT      = 1 << 0,
    P_CMD_BUFFER_USAGE_SIMULTANEOUS_USE_BIT     = 1 << 1,
    P_CMD_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT = 1 << 2,    // secondary buffer inside a render pass
} PCommandBufferUsage;

typedef struct PCommandPoolDesc {
    PQueueFlags queue_flags;
    PCommandPoolFlags flags;
    const char* name;
} PCommandPoolDesc;

typedef enum PWindowHandleType {
    P_WINDOW_HANDLE_WIN32   = 0,
    P_WINDOW_HANDLE_XLIB    = 1,
    P_WINDOW_HANDLE_XCB     = 2,
    P_WINDOW_HANDLE_WAYLAND = 3,
    P_WINDOW_HANDLE_METAL   = 4,
    P_WINDOW_HANDLE_ANDROID = 5,
} PWindowHandleType;

typedef struct PWindowHandles {
    PWindowHandleType type;

    union {
        struct {
            void* hwnd;
            void* hinstance;
        } win32;

        struct {
            void* display;
            unsigned long window;
        } xlib;

        struct {
            void* connection;
            uint32_t window;
        } xcb;

        struct {
            void* display;
            void* surface;
        } wayland;

        struct {
            void* ca_metal_layer;
        } metal;

        struct {
            void* a_native_window;
        } android;
    };
} PWindowHandles;

typedef struct Pigment Pigment;

typedef struct PWindowRenderer PWindowRenderer;

typedef struct PInstance PInstance;

typedef struct LayerList LayerList;

typedef struct ExtensionList ExtensionList;

typedef struct PDevice PDevice;

typedef struct PSurface PSurface;

typedef struct SwapChainSupportDetails SwapChainSupportDetails;

typedef struct PSwapchain PSwapchain;

typedef struct PPipeline PPipeline;

typedef struct PLayout PLayout;

typedef struct PPipelineCache PPipelineCache;

typedef struct PPipelineBuild PPipelineBuild;

typedef struct PCommandPool PCommandPool;

typedef struct PCommandBuffer PCommandBuffer;

typedef struct PSync PSync;

typedef struct PDescriptorSetLayout PDescriptorSetLayout;

typedef struct PDescriptorPool PDescriptorPool;

typedef struct PDescriptorSet PDescriptorSet;

typedef struct PImage PImage;

typedef struct PImageView PImageView;

typedef struct PSampler PSampler;

typedef struct PSwapchainCallbackList PSwapchainCallbackList;

typedef struct PDeletionQueue PDeletionQueue;

typedef struct PBuffer PBuffer;

typedef struct PSamplerDesc PSamplerDesc;

typedef struct PRenderPassDesc PRenderPassDesc;

typedef struct PigmentLoggerCreateInfo PigmentLoggerCreateInfo;

typedef struct PigmentLogger PigmentLogger;

typedef struct PResourceTracker PResourceTracker;

typedef struct PSwapchainRecreateEvent {
    PWindowRenderer* renderer;
    uint32_t width;
    uint32_t height;
} PSwapchainRecreateEvent;

typedef void (*PSwapchainRecreateFn)(Pigment* pigment, const PSwapchainRecreateEvent* event, void* user_data);

typedef enum PShaderStageFlags {
    P_SHADER_STAGE_VERTEX_BIT   = 1 << 0,
    P_SHADER_STAGE_FRAGMENT_BIT = 1 << 4,
    P_SHADER_STAGE_COMPUTE_BIT  = 1 << 5,
    P_SHADER_STAGE_ALL_GRAPHICS = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT,
    P_SHADER_STAGE_ALL          = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT | P_SHADER_STAGE_COMPUTE_BIT,
} PShaderStageFlags;

typedef enum PFilteringMode {
    P_FILTERING_MODE_NEAREST = 0,
    P_FILTERING_MODE_LINEAR  = 1,
} PFilteringMode;

typedef enum PSampleCount {
    P_SAMPLE_COUNT_1  = 1,
    P_SAMPLE_COUNT_2  = 2,
    P_SAMPLE_COUNT_4  = 4,
    P_SAMPLE_COUNT_8  = 8,
    P_SAMPLE_COUNT_16 = 16,
    P_SAMPLE_COUNT_32 = 32,
    P_SAMPLE_COUNT_64 = 64,
} PSampleCount;

typedef enum PResolveMode {
    P_RESOLVE_MODE_AUTO        = 0,    // AVERAGE for color, SAMPLE_ZERO for depth/stencil
    P_RESOLVE_MODE_SAMPLE_ZERO = 1,
    P_RESOLVE_MODE_AVERAGE     = 2,
    P_RESOLVE_MODE_MIN         = 4,
    P_RESOLVE_MODE_MAX         = 8,
} PResolveMode;

typedef enum PLoadOp {
    P_LOAD_OP_CLEAR     = 0,    // default, clear to clear_color / depth_clear_value
    P_LOAD_OP_LOAD      = 1,    // preserve previous content (accumulation, overlay, multi-pass)
    P_LOAD_OP_DONT_CARE = 2,    // contents undefined at pass start
} PLoadOp;

typedef enum PStoreOp {
    P_STORE_OP_AUTO      = 0,    // STORE if no resolve, DONT_CARE if resolve_image is set
    P_STORE_OP_STORE     = 1,    // keep written content
    P_STORE_OP_DONT_CARE = 2,    // discard written content
} PStoreOp;

typedef enum PFormat {
    P_FORMAT_UNDEFINED           = 0,
    P_FORMAT_R8_UNORM            = 9,
    P_FORMAT_R8_UINT             = 13,
    P_FORMAT_R8G8_UNORM          = 16,
    P_FORMAT_R8G8B8A8_UNORM      = 37,
    P_FORMAT_R8G8B8A8_SRGB       = 43,
    P_FORMAT_B8G8R8A8_UNORM      = 44,
    P_FORMAT_B8G8R8A8_SRGB       = 50,
    P_FORMAT_R16_UINT            = 74,
    P_FORMAT_R16G16B16A16_SFLOAT = 97,
    P_FORMAT_R32_SFLOAT          = 100,
    P_FORMAT_R32G32_SFLOAT       = 103,
    P_FORMAT_R32G32B32_SFLOAT    = 106,
    P_FORMAT_R32G32B32A32_SFLOAT = 109,
    P_FORMAT_D16_UNORM           = 124,
    P_FORMAT_D32_SFLOAT          = 126,
    P_FORMAT_S8_UINT             = 127,
    P_FORMAT_D16_UNORM_S8_UINT   = 128,
    P_FORMAT_D24_UNORM_S8_UINT   = 129,
    P_FORMAT_D32_SFLOAT_S8_UINT  = 130,

    P_FORMAT_BC1_RGB_UNORM  = 131,
    P_FORMAT_BC1_RGB_SRGB   = 132,
    P_FORMAT_BC1_RGBA_UNORM = 133,
    P_FORMAT_BC1_RGBA_SRGB  = 134,
    P_FORMAT_BC2_UNORM      = 135,
    P_FORMAT_BC2_SRGB       = 136,
    P_FORMAT_BC3_UNORM      = 137,
    P_FORMAT_BC3_SRGB       = 138,
    P_FORMAT_BC4_UNORM      = 139,
    P_FORMAT_BC4_SNORM      = 140,
    P_FORMAT_BC5_UNORM      = 141,
    P_FORMAT_BC5_SNORM      = 142,
    P_FORMAT_BC6H_UFLOAT    = 143,
    P_FORMAT_BC6H_SFLOAT    = 144,
    P_FORMAT_BC7_UNORM      = 145,
    P_FORMAT_BC7_SRGB       = 146,

    P_FORMAT_ETC2_R8G8B8_UNORM   = 147,
    P_FORMAT_ETC2_R8G8B8_SRGB    = 148,
    P_FORMAT_ETC2_R8G8B8A1_UNORM = 149,
    P_FORMAT_ETC2_R8G8B8A1_SRGB  = 150,
    P_FORMAT_ETC2_R8G8B8A8_UNORM = 151,
    P_FORMAT_ETC2_R8G8B8A8_SRGB  = 152,
    P_FORMAT_EAC_R11_UNORM       = 153,
    P_FORMAT_EAC_R11_SNORM       = 154,
    P_FORMAT_EAC_R11G11_UNORM    = 155,
    P_FORMAT_EAC_R11G11_SNORM    = 156,

    P_FORMAT_ASTC_4x4_UNORM   = 157,
    P_FORMAT_ASTC_4x4_SRGB    = 158,
    P_FORMAT_ASTC_5x4_UNORM   = 159,
    P_FORMAT_ASTC_5x4_SRGB    = 160,
    P_FORMAT_ASTC_5x5_UNORM   = 161,
    P_FORMAT_ASTC_5x5_SRGB    = 162,
    P_FORMAT_ASTC_6x5_UNORM   = 163,
    P_FORMAT_ASTC_6x5_SRGB    = 164,
    P_FORMAT_ASTC_6x6_UNORM   = 165,
    P_FORMAT_ASTC_6x6_SRGB    = 166,
    P_FORMAT_ASTC_8x5_UNORM   = 167,
    P_FORMAT_ASTC_8x5_SRGB    = 168,
    P_FORMAT_ASTC_8x6_UNORM   = 169,
    P_FORMAT_ASTC_8x6_SRGB    = 170,
    P_FORMAT_ASTC_8x8_UNORM   = 171,
    P_FORMAT_ASTC_8x8_SRGB    = 172,
    P_FORMAT_ASTC_10x5_UNORM  = 173,
    P_FORMAT_ASTC_10x5_SRGB   = 174,
    P_FORMAT_ASTC_10x6_UNORM  = 175,
    P_FORMAT_ASTC_10x6_SRGB   = 176,
    P_FORMAT_ASTC_10x8_UNORM  = 177,
    P_FORMAT_ASTC_10x8_SRGB   = 178,
    P_FORMAT_ASTC_10x10_UNORM = 179,
    P_FORMAT_ASTC_10x10_SRGB  = 180,
    P_FORMAT_ASTC_12x10_UNORM = 181,
    P_FORMAT_ASTC_12x10_SRGB  = 182,
    P_FORMAT_ASTC_12x12_UNORM = 183,
    P_FORMAT_ASTC_12x12_SRGB  = 184,
} PFormat;

typedef struct PSwapchainDesc {
    uint32_t width;             // 0 = query from surface
    uint32_t height;            // 0 = query from surface
    PColorSpace color_space;    // 0 = SRGB. Fallback to compatible if HDR format requested but not supported.
    PPresentMode present_mode;
    PSampleCount samples;    // 0 or P_SAMPLE_COUNT_1 = no MSAA. Falls back to 1x if hardware does not support requested count.
    uint32_t image_count;
    PBool transparent;
} PSwapchainDesc;

typedef enum PCompareOp {
    P_COMPARE_OP_NEVER            = 0,
    P_COMPARE_OP_LESS             = 1,
    P_COMPARE_OP_EQUAL            = 2,
    P_COMPARE_OP_LESS_OR_EQUAL    = 3,
    P_COMPARE_OP_GREATER          = 4,
    P_COMPARE_OP_NOT_EQUAL        = 5,
    P_COMPARE_OP_GREATER_OR_EQUAL = 6,
    P_COMPARE_OP_ALWAYS           = 7,
} PCompareOp;

typedef enum PPipelineStage {
    P_PIPELINE_STAGE_NONE                        = 0,
    P_PIPELINE_STAGE_DRAW_INDIRECT_BIT           = 1 << 0,
    P_PIPELINE_STAGE_VERTEX_INPUT_BIT            = 1 << 1,
    P_PIPELINE_STAGE_INDEX_INPUT_BIT             = 1 << 2,
    P_PIPELINE_STAGE_VERTEX_SHADER_BIT           = 1 << 3,
    P_PIPELINE_STAGE_FRAGMENT_SHADER_BIT         = 1 << 4,
    P_PIPELINE_STAGE_COMPUTE_SHADER_BIT          = 1 << 5,
    P_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT    = 1 << 6,
    P_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT     = 1 << 7,
    P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 1 << 8,
    P_PIPELINE_STAGE_TRANSFER_BIT                = 1 << 9,
    P_PIPELINE_STAGE_HOST_BIT                    = 1 << 10,
    P_PIPELINE_STAGE_ALL_GRAPHICS_BIT            = 1 << 11,
    P_PIPELINE_STAGE_ALL_COMMANDS_BIT            = 1 << 12,
} PPipelineStage;

typedef enum PPipelineBindPoint {
    P_PIPELINE_BIND_POINT_GRAPHICS = 0,
    P_PIPELINE_BIND_POINT_COMPUTE  = 1,
} PPipelineBindPoint;

typedef enum PCommandBufferLevel {
    P_COMMAND_BUFFER_LEVEL_PRIMARY   = 0,
    P_COMMAND_BUFFER_LEVEL_SECONDARY = 1,
} PCommandBufferLevel;

typedef enum PMemoryFlags {
    P_MEMORY_DEVICE_LOCAL_BIT  = 1 << 0,
    P_MEMORY_HOST_VISIBLE_BIT  = 1 << 1,
    P_MEMORY_HOST_COHERENT_BIT = 1 << 2,
    P_MEMORY_HOST_CACHED_BIT   = 1 << 3,
} PMemoryFlags;

typedef enum PMemoryAccess {
    P_MEMORY_ACCESS_NONE                               = 0,
    P_MEMORY_ACCESS_INDIRECT_COMMAND_READ_BIT          = 1 << 0,
    P_MEMORY_ACCESS_INDEX_READ_BIT                     = 1 << 1,
    P_MEMORY_ACCESS_VERTEX_ATTRIBUTE_READ_BIT          = 1 << 2,
    P_MEMORY_ACCESS_UNIFORM_READ_BIT                   = 1 << 3,
    P_MEMORY_ACCESS_SHADER_SAMPLED_READ_BIT            = 1 << 4,
    P_MEMORY_ACCESS_SHADER_STORAGE_READ_BIT            = 1 << 5,
    P_MEMORY_ACCESS_SHADER_STORAGE_WRITE_BIT           = 1 << 6,
    P_MEMORY_ACCESS_COLOR_ATTACHMENT_READ_BIT          = 1 << 7,
    P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT         = 1 << 8,
    P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT  = 1 << 9,
    P_MEMORY_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT = 1 << 10,
    P_MEMORY_ACCESS_TRANSFER_READ_BIT                  = 1 << 11,
    P_MEMORY_ACCESS_TRANSFER_WRITE_BIT                 = 1 << 12,
    P_MEMORY_ACCESS_HOST_READ_BIT                      = 1 << 13,
    P_MEMORY_ACCESS_HOST_WRITE_BIT                     = 1 << 14,
    P_MEMORY_ACCESS_MEMORY_READ_BIT                    = 1 << 15,
    P_MEMORY_ACCESS_MEMORY_WRITE_BIT                   = 1 << 16,
} PMemoryAccess;

typedef enum PImageLayout {
    P_IMAGE_LAYOUT_UNDEFINED                = 0,
    P_IMAGE_LAYOUT_GENERAL                  = 1,
    P_IMAGE_LAYOUT_COLOR_ATTACHMENT         = 2,
    P_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT = 3,
    P_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY  = 4,
    P_IMAGE_LAYOUT_SHADER_READ_ONLY         = 5,
    P_IMAGE_LAYOUT_TRANSFER_SRC             = 6,
    P_IMAGE_LAYOUT_TRANSFER_DST             = 7,
    P_IMAGE_LAYOUT_PRESENT                  = 8,
    P_IMAGE_LAYOUT_PREINITIALIZED           = 9,
} PImageLayout;

typedef enum PImageAspect {
    P_IMAGE_ASPECT_INHERIT       = 0,    // use image->aspect
    P_IMAGE_ASPECT_COLOR         = 1,
    P_IMAGE_ASPECT_DEPTH         = 2,
    P_IMAGE_ASPECT_STENCIL       = 3,
    P_IMAGE_ASPECT_DEPTH_STENCIL = 4,
} PImageAspect;

typedef enum PImageViewType {
    P_IMAGE_VIEW_TYPE_AUTO       = 0,    // derive from layer_count and create flags
    P_IMAGE_VIEW_TYPE_2D         = 1,
    P_IMAGE_VIEW_TYPE_2D_ARRAY   = 2,
    P_IMAGE_VIEW_TYPE_CUBE       = 3,
    P_IMAGE_VIEW_TYPE_CUBE_ARRAY = 4,
    P_IMAGE_VIEW_TYPE_3D         = 5,
} PImageViewType;

typedef struct PImageViewDesc {
    PFormat format;              // 0 (P_FORMAT_UNDEFINED) = inherit the image format
    PImageAspect aspect;         // 0 (P_IMAGE_ASPECT_INHERIT) = inherit the image aspect
    PImageViewType view_type;    // 0 (P_IMAGE_VIEW_TYPE_AUTO) = derive from layer_count and image type
    uint32_t base_layer;
    uint32_t layer_count;    // 0 = remaining
    uint32_t base_mip;
    uint32_t mip_count;    // 0 = remaining
    const char* name;
} PImageViewDesc;

typedef enum PAllocScope {
    P_ALLOC_SCOPE_COMMAND  = 0,    // temporary within a function
    P_ALLOC_SCOPE_OBJECT   = 1,    // lifetime of a single RHI object
    P_ALLOC_SCOPE_CACHE    = 2,    // pools / caches
    P_ALLOC_SCOPE_DEVICE   = 3,    // lifetime of the device
    P_ALLOC_SCOPE_INSTANCE = 4,    // lifetime of the Pigment instance
} PAllocScope;

typedef enum PInternalAllocationType {
    P_INTERNAL_ALLOCATION_TYPE_EXECUTABLE = 0,
} PInternalAllocationType;

typedef struct PAllocator {
    void* user_data;

    void* (*alloc)(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope);
    void* (*calloc)(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope);
    void* (*realloc)(void* user_data, void* ptr, uint64_t old_size, uint64_t new_size, uint64_t alignment, PAllocScope scope);
    void (*free)(void* user_data, void* ptr);

    // Optional callbacks for driver-internal allocations. NULL = not tracked.
    void (*internal_alloc_notify)(void* user_data, uint64_t size, PInternalAllocationType type, PAllocScope scope);
    void (*internal_free_notify)(void* user_data, uint64_t size, PInternalAllocationType type, PAllocScope scope);
} PAllocator;

PIGMENT_API extern const PAllocator pigment_default_allocator;

typedef struct PigmentConfig {
    uint32_t max_frames_in_flight;
    const PigmentLoggerCreateInfo* loggers;
    uint32_t logger_count;
    PBool enable_validation;
    PBool enable_best_practices;
    float depth_clear_value;                // 0.0 = reverse Z (default), 1.0 = standard Z. Convention shared across all pipelines.
    const PAllocator* allocator;            // NULL = uses pigment_default_allocator (malloc/free)
    const PQueueRequest* queue_requests;    // explicit queue layout. NULL or count=0 = default policy (1 graphics + 1 compute + 1 transfer, most specialized the hardware exposes).
    uint32_t queue_request_count;
    const void* extra;
} PigmentConfig;

#ifdef __cplusplus
}
#endif

#endif
