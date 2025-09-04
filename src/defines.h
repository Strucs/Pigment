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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef NDEBUG
    #define VLAYERS_ENABLED 0
#else
    #define VLAYERS_ENABLED 1
#endif

#define PIGMENT_SUCCESS 0
#define PIGMENT_ERROR 1

#define PIGMENT_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

typedef struct {
    bool has_value;
    uint32_t value;
} optional_uint32;

typedef struct PAppInfo_T {
    const char* app_name;
    uint32_t    app_version;
} PAppInfo;

typedef struct PWindowInfo_T {
    int   width;
    int   height;
    char* title;
} PWindowInfo;

typedef struct Pigment_T Pigment;

typedef struct PWindow_T PWindow;

typedef struct PInstance_T PInstance;

typedef struct LayerList_T LayerList;

typedef struct ExtensionList_T ExtensionList;

typedef struct PDevice_T PDevice;

typedef struct QueueFamilyIndices_T QueueFamilyIndices;

typedef struct FamilySet_T FamilySet;

typedef struct PSurface_T PSurface;

typedef struct QueueFamilySet_T QueueFamilySet;

typedef struct SwapChainSupportDetails_T SwapChainSupportDetails;

typedef struct PSwapchain_T PSwapchain;

typedef struct PPipeline_T PPipeline;

typedef struct PRenderPass_T PRenderPass;

typedef struct PCommands_T PCommands;

typedef struct PSync_T PSync;

typedef struct PVertexDescription_T PVertexDescription;

typedef struct PBuffers_T PBuffers;

typedef struct PDescriptor_T PDescriptor;

typedef struct PTexture_T PTexture;

typedef struct PTextureList_T PTextureList;

typedef struct PSampler_T PSampler;

typedef struct PSamplerList_T PSamplerList;

typedef struct PModel_T PModel;

typedef struct PDepthResources_T PDepthResources;

typedef struct PCamera_T PCamera;

typedef enum {
    NEAREST = 0,
    LINEAR  = 1
} FilteringMode;

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef struct Vertex {
    vec3 pos;
    vec3 color;
    vec2 texture_coord;
    uint32_t texture_index;
    uint32_t sampler_index;
} Vertex;

typedef struct UniformBufferObject {
    alignas(16) mat4 model;
    alignas(16) mat4 view;
    alignas(16) mat4 projection;
} UniformBufferObject;

#endif
