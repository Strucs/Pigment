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

#ifndef DEFINES_H
#define DEFINES_H

#include <stdint.h>
#include <stdbool.h>

#define PIGMENT_SUCCESS 0
#define PIGMENT_ERROR 1

#define PIGMENT_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t) (major)) << 22U) | (((uint32_t) (minor)) << 12U) | ((uint32_t) (patch)))

#define PIGMENT_DEFAULT_MAX_FRAMES_IN_FLIGHT 2

typedef struct PAppInfo {
    const char* app_name;
    uint32_t app_version;
} PAppInfo;

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
    P_FEATURE_DEPTH_BOUNDS_TEST       = 0,
    P_FEATURE_WIREFRAME_RASTERIZATION = 1,
    P_FEATURE_COUNT    // size of the device feature array
} PFeature;

typedef enum {
    P_PRESENT_MODE_IMMEDIATE    = 0,    // no vsync, uncapped, may tear
    P_PRESENT_MODE_MAILBOX      = 1,    // triple buffering, no tearing
    P_PRESENT_MODE_FIFO         = 2,    // vsync (guaranteed available)
    P_PRESENT_MODE_FIFO_RELAXED = 3,    // vsync, tears if frame is late
} PPresentMode;

#define P_PRESENT_MODE_DEFAULT P_PRESENT_MODE_MAILBOX

typedef enum PQueueFamily {
    P_QUEUE_FAMILY_GRAPHICS = 0,
    // P_QUEUE_FAMILY_COMPUTE  = 1,
    // P_QUEUE_FAMILY_TRANSFER = 2,
    P_QUEUE_FAMILY_MAX_ENUM = 0x7FFFFFFF
} PQueueFamily;

typedef enum PCommandPoolFlags {
    P_COMMAND_POOL_FLAG_NONE         = 0,
    P_COMMAND_POOL_FLAG_TRANSIENT    = 1 << 0,
    P_COMMAND_POOL_FLAG_RESET_BUFFER = 1 << 1,
} PCommandPoolFlags;

typedef struct PCommandPoolDesc {
    PQueueFamily queue_family;
    PCommandPoolFlags flags;
} PCommandPoolDesc;

typedef enum {
    P_WINDOW_FLAG_NONE          = 0,
    P_WINDOW_FLAG_RESIZABLE     = 1 << 0,
    P_WINDOW_FLAG_BORDERLESS    = 1 << 1,
    P_WINDOW_FLAG_FULLSCREEN    = 1 << 2,
    P_WINDOW_FLAG_MAXIMIZED     = 1 << 3,
    P_WINDOW_FLAG_MINIMIZED     = 1 << 4,
    P_WINDOW_FLAG_ALWAYS_ON_TOP = 1 << 5,
    P_WINDOW_FLAG_HIGH_DPI      = 1 << 6,
    P_WINDOW_FLAG_TRANSPARENT   = 1 << 7,
    P_WINDOW_FLAG_NOT_FOCUSABLE = 1 << 8,
} PWindowFlags;

#define P_WINDOW_FLAGS_DEFAULT (P_WINDOW_FLAG_RESIZABLE | P_WINDOW_FLAG_HIGH_DPI)

typedef struct PWindowInfo {
    int width;
    int height;
    char* title;
    PPresentMode preferred_present_mode;
    PWindowFlags flags;
} PWindowInfo;

typedef struct Pigment Pigment;

typedef struct PWindow PWindow;

typedef struct PWindowRenderer PWindowRenderer;

typedef struct PInstance PInstance;

typedef struct LayerList LayerList;

typedef struct ExtensionList ExtensionList;

typedef struct PDevice PDevice;

typedef struct QueueFamilyIndices QueueFamilyIndices;

typedef struct FamilySet FamilySet;

typedef struct PSurface PSurface;

typedef struct QueueFamilySet QueueFamilySet;

typedef struct SwapChainSupportDetails SwapChainSupportDetails;

typedef struct PSwapchain PSwapchain;

typedef struct PPipeline PPipeline;

typedef struct PPipelineList PPipelineList;

typedef struct PLayout PLayout;

typedef struct PLayoutList PLayoutList;

typedef struct PPipelineBuild PPipelineBuild;

typedef struct PCommandPool PCommandPool;

typedef struct PCommandPoolList PCommandPoolList;

typedef struct PCommandBuffers PCommandBuffers;

typedef struct PSync PSync;

typedef struct PDescriptorSetLayout PDescriptorSetLayout;

typedef struct PDescriptorPool PDescriptorPool;

typedef struct PDescriptorSet PDescriptorSet;

typedef struct PImage PImage;

typedef struct PSampler PSampler;

typedef struct PTrackedImage PTrackedImage;

typedef struct PTrackedImageList PTrackedImageList;

typedef struct PBuffer PBuffer;

typedef struct PSamplerDesc PSamplerDesc;

typedef struct PRenderPassDesc PRenderPassDesc;

typedef struct PigmentLoggerCreateInfo PigmentLoggerCreateInfo;

typedef struct PigmentLogger PigmentLogger;

typedef enum PShaderStageFlags {
    P_SHADER_STAGE_VERTEX_BIT   = 1 << 0,
    P_SHADER_STAGE_FRAGMENT_BIT = 1 << 4,
    P_SHADER_STAGE_COMPUTE_BIT  = 1 << 5,
    P_SHADER_STAGE_ALL_GRAPHICS = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT,
    P_SHADER_STAGE_ALL          = P_SHADER_STAGE_VERTEX_BIT | P_SHADER_STAGE_FRAGMENT_BIT | P_SHADER_STAGE_COMPUTE_BIT,
} PShaderStageFlags;

typedef enum PSampleCount {
    P_SAMPLE_COUNT_1  = 1,
    P_SAMPLE_COUNT_2  = 2,
    P_SAMPLE_COUNT_4  = 4,
    P_SAMPLE_COUNT_8  = 8,
    P_SAMPLE_COUNT_16 = 16,
    P_SAMPLE_COUNT_32 = 32,
    P_SAMPLE_COUNT_64 = 64,
} PSampleCount;

typedef enum PFormat {
    P_FORMAT_UNDEFINED           = 0,
    P_FORMAT_R8_UNORM            = 9,
    P_FORMAT_R8G8_UNORM          = 16,
    P_FORMAT_R8G8B8A8_UNORM      = 37,
    P_FORMAT_R8G8B8A8_SRGB       = 43,
    P_FORMAT_B8G8R8A8_UNORM      = 44,
    P_FORMAT_B8G8R8A8_SRGB       = 50,
    P_FORMAT_R16G16B16A16_SFLOAT = 97,
} PFormat;

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

typedef struct PigmentConfig {
    uint32_t max_frames_in_flight;
    const PigmentLoggerCreateInfo* loggers;
    uint32_t logger_count;
    bool enable_validation;
    bool enable_best_practices;
    float depth_clear_value;    // 0.0 = reverse Z (default), 1.0 = standard Z. Convention shared across all pipelines.
    const void* extra;
} PigmentConfig;

#endif
