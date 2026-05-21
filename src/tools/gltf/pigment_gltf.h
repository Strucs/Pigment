/**
 * Copyright 2026 Angel-Leduc TA
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

#ifndef PIGMENT_GLTF_H
#define PIGMENT_GLTF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pigment/defines.h"

#include "pigment/std/bindless.h"
#include "pigment/std/file_io.h"
#include "pigment/std/material.h"
#include "pigment/std/types.h"
#include "pigment/std/vertex.h"

typedef struct PRawSurface {
    uint32_t start_index;
    uint32_t index_count;
    uint32_t material_id;
    uint32_t node_index;
} PRawSurface;

typedef struct PRawImage {
    unsigned char* pixels;
    uint32_t width;
    uint32_t height;
} PRawImage;

typedef struct PGltfMesh {
    PVertex* vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;

    uint32_t* indices;
    uint32_t index_count;
    uint32_t index_capacity;

    PRawSurface* surfaces;
    uint32_t surface_count;
    uint32_t surface_capacity;

    PMaterialDesc* surface_descs;

    PMat4* node_transforms;
    uint32_t node_count;
    uint32_t node_capacity;

    PRawImage* images;
    uint32_t image_count;

    PSamplerDesc* sampler_descs;
    uint32_t sampler_count;
} PGltfMesh;

PIGMENT_API PGltfMesh* pigment_gltf_load_mesh(Pigment* pigment, const IOCallbacks* io, const char* filepath);
PIGMENT_API void pigment_gltf_free_mesh(Pigment* pigment, PGltfMesh* mesh);

PIGMENT_API PResult pigment_gltf_upload_textures(Pigment* pigment, PStdBindless* bindless, PGltfMesh* asset, PMaterials* materials);

#ifdef __cplusplus
}
#endif

#endif
