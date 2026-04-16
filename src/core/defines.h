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

#ifndef DEFINES_H
#define DEFINES_H


#include <stdint.h>

#ifdef NDEBUG
    #define VLAYERS_ENABLED 0
#else
    #define VLAYERS_ENABLED 1
#endif

#define PIGMENT_SUCCESS 0
#define PIGMENT_ERROR 1

#define PIGMENT_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t) (major)) << 22U) | (((uint32_t) (minor)) << 12U) | ((uint32_t) (patch)))

#define PIGMENT_DEFAULT_MAX_IMAGES 1024
#define PIGMENT_DEFAULT_MAX_SAMPLERS 16
#define PIGMENT_DEFAULT_MAX_FRAMES_IN_FLIGHT 2

typedef struct PAppInfo {
    const char* app_name;
    uint32_t app_version;
} PAppInfo;

typedef struct PWindowInfo {
    int width;
    int height;
    char* title;
} PWindowInfo;

typedef struct Pigment Pigment;

typedef struct PWindow PWindow;

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

typedef struct PCommands PCommands;

typedef struct PSync PSync;

typedef struct PDrawPushConstants PDrawPushConstants;

typedef struct PUniformBuffers PUniformBuffers;

typedef struct PDescriptor PDescriptor;

typedef struct PImage PImage;

typedef struct PImageList PImageList;

typedef struct PSampler PSampler;

typedef struct PSamplerList PSamplerList;

typedef struct PDepthResources PDepthResources;

typedef struct PCamera PCamera;

typedef struct PMeshBuffers PMeshBuffers;

typedef enum FilteringMode {
    NEAREST = 0,
    LINEAR  = 1
} FilteringMode;

typedef enum PAddressMode{
    P_ADDRESS_MODE_REPEAT          = 0,
    P_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    P_ADDRESS_MODE_CLAMP_TO_EDGE   = 2,
    P_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
} PAddressMode;

typedef struct PSamplerDesc {
    FilteringMode mag_filter;     // NEAREST or LINEAR
    FilteringMode min_filter;     // NEAREST or LINEAR
    FilteringMode mipmap_mode;    // NEAREST or LINEAR
    PAddressMode address_mode;    // 0 = REPEAT
} PSamplerDesc;

typedef enum {
    P_PRESENT_MODE_IMMEDIATE    = 0,    // no vsync, uncapped, may tear
    P_PRESENT_MODE_MAILBOX      = 1,    // triple buffering, no tearing
    P_PRESENT_MODE_FIFO         = 2,    // vsync (guaranteed available)
    P_PRESENT_MODE_FIFO_RELAXED = 3,    // vsync, tears if frame is late
} PPresentMode;

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef struct PDrawCall {
    PMeshBuffers* mesh;
    mat4 transform;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t image_index;
    uint32_t sampler_index;
    uint32_t pipeline_id;
    void* extra_push_data;
    uint32_t extra_push_size;
} PDrawCall;

typedef struct PVertex {
    vec3 pos;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
} __attribute__((aligned(16))) PVertex;

typedef struct UniformBufferObject {
    alignas(16) mat4 view;
    alignas(16) mat4 projection;
} UniformBufferObject;

typedef struct PigmentConfig {
    uint32_t max_images;
    uint32_t max_samplers;
    uint32_t max_frames_in_flight;
} PigmentConfig;

#endif
