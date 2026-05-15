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

#ifndef PIGMENT_STD_GLTF_LOADER_H
#define PIGMENT_STD_GLTF_LOADER_H

#include "bindless.h"
#include "file_io.h"
#include "material.h"
#include "types.h"
#include "vertex.h"

#include "pigment/defines.h"

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

typedef struct MeshAsset {
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
} MeshAsset;

MeshAsset* load_gltf_mesh(Pigment* pigment, const IOCallbacks* io, const char* filepath);
void free_mesh_asset(Pigment* pigment, MeshAsset* mesh);

PResult upload_mesh_textures(Pigment* pigment, PStdBindless* bindless, MeshAsset* asset, PMaterials* materials);

#endif
