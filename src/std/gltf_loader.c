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

#include "gltf_loader.h"

#include "internal.h"

#include <cgltf.h>
#include <stb_image.h>
#include <string.h>

#define NO_MATERIAL UINT32_MAX
#define MISSING_TEXTURE UINT32_MAX
#define DEFAULT_CAPACITY 8

typedef struct PrimAttrs {
    cgltf_accessor* pos;
    cgltf_accessor* normal;
    cgltf_accessor* uv;
    cgltf_accessor* color;
} PrimAttrs;

static PSamplerDesc convert_gltf_sampler(cgltf_sampler* s);
static PrimAttrs find_primitive_attrs(cgltf_primitive* prim);
static PResult append_primitive_vertices(Pigment* pigment, MeshAsset* asset, const PrimAttrs* attrs);
static PMaterialDesc resolve_primitive_material(cgltf_material* mat, cgltf_data* data);
static PResult append_surface(Pigment* pigment, MeshAsset* asset, PRawSurface surface, const PMaterialDesc* desc);
static PResult process_primitive(Pigment* pigment, MeshAsset* asset, cgltf_data* data, cgltf_primitive* prim, uint32_t node_idx);
static PResult process_node(Pigment* pigment, MeshAsset* asset, cgltf_data* data, cgltf_node* node);
static cgltf_result io_cgltf_read(const cgltf_memory_options* memory_options, const cgltf_file_options* file, const char* path, cgltf_size* size, void** data);
static void io_cgltf_release(const cgltf_memory_options* memory_options, const cgltf_file_options* file, void* data);
static void* pigment_cgltf_alloc(void* user, cgltf_size size);
static void pigment_cgltf_free(void* user, void* ptr);

static PSamplerDesc convert_gltf_sampler(cgltf_sampler* s)
{
    PSamplerDesc desc = {0};

    desc.mag_filter = (s->mag_filter == cgltf_filter_type_nearest) ? P_FILTERING_MODE_NEAREST : P_FILTERING_MODE_LINEAR;

    switch(s->min_filter)
    {
        case cgltf_filter_type_nearest:
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_nearest_mipmap_linear:
            desc.min_filter = P_FILTERING_MODE_NEAREST;
            break;
        default:
            desc.min_filter = P_FILTERING_MODE_LINEAR;
            break;
    }

    switch(s->min_filter)
    {
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_linear_mipmap_nearest:
            desc.mipmap_mode = P_FILTERING_MODE_NEAREST;
            break;
        default:
            desc.mipmap_mode = P_FILTERING_MODE_LINEAR;
            break;
    }

    switch(s->wrap_s)
    {
        case cgltf_wrap_mode_clamp_to_edge:
            desc.address_mode = P_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
        case cgltf_wrap_mode_mirrored_repeat:
            desc.address_mode = P_ADDRESS_MODE_MIRRORED_REPEAT;
            break;
        default:
            desc.address_mode = P_ADDRESS_MODE_REPEAT;
            break;
    }

    return desc;
}

static PrimAttrs find_primitive_attrs(cgltf_primitive* prim)
{
    PrimAttrs attributes = {0};

    int wanted_uv_set = 0;
    if(prim->material && prim->material->has_pbr_metallic_roughness
       && prim->material->pbr_metallic_roughness.base_color_texture.texture)
    {
        wanted_uv_set = (int) prim->material->pbr_metallic_roughness.base_color_texture.texcoord;
    }

    cgltf_accessor* uv_fallback = NULL;
    for(size_t a = 0; a < prim->attributes_count; a++)
    {
        cgltf_attribute* attr = &prim->attributes[a];
        if(attr->type == cgltf_attribute_type_position)
        {
            attributes.pos = attr->data;
        }
        else if(attr->type == cgltf_attribute_type_normal)
        {
            attributes.normal = attr->data;
        }
        else if(attr->type == cgltf_attribute_type_texcoord)
        {
            if(attr->index == wanted_uv_set)
            {
                attributes.uv = attr->data;
            }
            else if(uv_fallback == NULL)
            {
                uv_fallback = attr->data;
            }
        }
        else if(attr->type == cgltf_attribute_type_color && attr->index == 0)
        {
            attributes.color = attr->data;
        }
    }

    if(attributes.uv == NULL)
    {
        attributes.uv = uv_fallback;
    }

    return attributes;
}

static PResult append_primitive_vertices(Pigment* pigment, MeshAsset* asset, const PrimAttrs* attrs)
{
    if(attrs->pos == NULL || attrs->pos->count == 0)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t add = (uint32_t) attrs->pos->count;
    PResult res  = P_ARRAY_RESERVE_CACHE(pigment, asset->vertices, asset->vertex_count, asset->vertex_capacity, add, DEFAULT_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }

    if(asset->vertices == NULL)
    {
        return PIGMENT_ERROR;
    }

    for(size_t i = 0; i < attrs->pos->count; i++)
    {
        PVertex v = {0};
        cgltf_accessor_read_float(attrs->pos, i, v.pos, 3);
        if(attrs->normal)
        {
            cgltf_accessor_read_float(attrs->normal, i, v.normal, 3);
        }
        if(attrs->uv)
        {
            float uv[2] = {0};
            cgltf_accessor_read_float(attrs->uv, i, uv, 2);
            v.uv_x = uv[0];
            v.uv_y = uv[1];
        }
        if(attrs->color)
        {
            v.color[3] = 1.0f;    // default alpha for vec3 COLOR_0
            cgltf_accessor_read_float(attrs->color, i, v.color, 4);
        }
        else
        {
            v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
        }

        asset->vertices[asset->vertex_count] = v;
        asset->vertex_count++;
    }
    return PIGMENT_SUCCESS;
}

static PResult append_primitive_indices(Pigment* pigment, MeshAsset* asset, cgltf_accessor* indices, uint32_t base_vertex)
{
    if(indices == NULL || indices->count == 0)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t add = (uint32_t) indices->count;
    PResult res  = P_ARRAY_RESERVE_CACHE(pigment, asset->indices, asset->index_count, asset->index_capacity, add, DEFAULT_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }

    if(asset->indices == NULL)
    {
        return PIGMENT_ERROR;
    }

    for(size_t i = 0; i < indices->count; i++)
    {
        uint32_t idx                       = (uint32_t) cgltf_accessor_read_index(indices, i);
        asset->indices[asset->index_count] = base_vertex + idx;
        asset->index_count++;
    }
    return PIGMENT_SUCCESS;
}

static void resolve_texture_view(cgltf_texture_view* tv, cgltf_data* data, int32_t* image, int32_t* sampler)
{
    *image   = (int32_t) NO_MATERIAL;
    *sampler = (int32_t) NO_MATERIAL;
    if(tv == NULL || tv->texture == NULL)
    {
        return;
    }

    if(tv->texture->image)
    {
        *image = (int32_t) (tv->texture->image - data->images);
    }

    if(tv->texture->sampler)
    {
        *sampler = (int32_t) (tv->texture->sampler - data->samplers);
    }
}

static PMaterialDesc resolve_primitive_material(cgltf_material* mat, cgltf_data* data)
{
    PMaterialDesc desc = {
        .albedo_image               = (int32_t) NO_MATERIAL,
        .albedo_sampler             = (int32_t) NO_MATERIAL,
        .metallic_roughness_image   = (int32_t) NO_MATERIAL,
        .metallic_roughness_sampler = (int32_t) NO_MATERIAL,
        .normal_image               = (int32_t) NO_MATERIAL,
        .normal_sampler             = (int32_t) NO_MATERIAL,
        .emissive_image             = (int32_t) NO_MATERIAL,
        .emissive_sampler           = (int32_t) NO_MATERIAL,
        .occlusion_image            = (int32_t) NO_MATERIAL,
        .occlusion_sampler          = (int32_t) NO_MATERIAL,
        .base_color_factor          = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissive_factor            = {0.0f, 0.0f, 0.0f, 0.0f},
        .metallic_factor            = 1.0f,
        .roughness_factor           = 1.0f,
        .normal_scale               = 1.0f,
        .occlusion_scale            = 1.0f,
    };

    if(mat == NULL)
    {
        return desc;
    }

    if(mat->has_pbr_metallic_roughness)
    {
        cgltf_pbr_metallic_roughness* mr = &mat->pbr_metallic_roughness;
        resolve_texture_view(&mr->base_color_texture, data, &desc.albedo_image, &desc.albedo_sampler);
        resolve_texture_view(&mr->metallic_roughness_texture, data, &desc.metallic_roughness_image, &desc.metallic_roughness_sampler);
        memcpy(desc.base_color_factor, mr->base_color_factor, sizeof(desc.base_color_factor));
        desc.metallic_factor  = mr->metallic_factor;
        desc.roughness_factor = mr->roughness_factor;
    }
    else if(mat->has_pbr_specular_glossiness)
    {
        cgltf_pbr_specular_glossiness* sg = &mat->pbr_specular_glossiness;
        resolve_texture_view(&sg->diffuse_texture, data, &desc.albedo_image, &desc.albedo_sampler);
        memcpy(desc.base_color_factor, sg->diffuse_factor, sizeof(desc.base_color_factor));
    }

    resolve_texture_view(&mat->normal_texture, data, &desc.normal_image, &desc.normal_sampler);
    desc.normal_scale = mat->normal_texture.scale;

    resolve_texture_view(&mat->occlusion_texture, data, &desc.occlusion_image, &desc.occlusion_sampler);
    desc.occlusion_scale = mat->occlusion_texture.scale;

    resolve_texture_view(&mat->emissive_texture, data, &desc.emissive_image, &desc.emissive_sampler);
    memcpy(desc.emissive_factor, mat->emissive_factor, 3 * sizeof(float));
    desc.emissive_factor[3] = 0.0f;

    return desc;
}

static PResult append_surface(Pigment* pigment, MeshAsset* asset, PRawSurface surface, const PMaterialDesc* desc)
{
    uint32_t cap_a = asset->surface_capacity;
    uint32_t cap_b = asset->surface_capacity;
    PResult res    = P_ARRAY_RESERVE_CACHE(pigment, asset->surfaces, asset->surface_count, cap_a, 1, DEFAULT_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }

    res = P_ARRAY_RESERVE_CACHE(pigment, asset->surface_descs, asset->surface_count, cap_b, 1, DEFAULT_CAPACITY);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }
    asset->surface_capacity = cap_a;

    asset->surfaces[asset->surface_count]      = surface;
    asset->surface_descs[asset->surface_count] = *desc;
    asset->surface_count++;

    return PIGMENT_SUCCESS;
}

static PResult process_primitive(Pigment* pigment, MeshAsset* asset, cgltf_data* data, cgltf_primitive* prim, uint32_t node_idx)
{
    PrimAttrs attrs = find_primitive_attrs(prim);

    uint32_t base_vertex = asset->vertex_count;
    uint32_t index_start = asset->index_count;

    PResult res = append_primitive_vertices(pigment, asset, &attrs);
    if(res != PIGMENT_SUCCESS)
    {
        return res;
    }

    if(prim->indices)
    {
        res = append_primitive_indices(pigment, asset, prim->indices, base_vertex);
        if(res != PIGMENT_SUCCESS)
        {
            return res;
        }
    }
    else if(attrs.pos)
    {
        uint32_t add = (uint32_t) attrs.pos->count;
        res          = P_ARRAY_RESERVE_CACHE(pigment, asset->indices, asset->index_count, asset->index_capacity, add, DEFAULT_CAPACITY);
        if(res != PIGMENT_SUCCESS)
        {
            return res;
        }
        for(uint32_t i = 0; i < add; i++)
        {
            asset->indices[asset->index_count] = base_vertex + i;
            asset->index_count++;
        }
    }

    PMaterialDesc desc = resolve_primitive_material(prim->material, data);

    PRawSurface surface = {
        .start_index = index_start,
        .index_count = asset->index_count - index_start,
        .material_id = NO_MATERIAL,
        .node_index  = node_idx,
    };

    return append_surface(pigment, asset, surface, &desc);
}

static PResult process_node(Pigment* pigment, MeshAsset* asset, cgltf_data* data, cgltf_node* node)
{
    if(node->mesh)
    {
        float world[16];
        uint32_t this_node_idx;
        cgltf_mesh* gltf_mesh = NULL;

        cgltf_node_transform_world(node, world);

        PResult res = P_ARRAY_RESERVE_CACHE(pigment, asset->node_transforms, asset->node_count, asset->node_capacity, 1, DEFAULT_CAPACITY);
        if(res != PIGMENT_SUCCESS)
        {
            return res;
        }

        this_node_idx = asset->node_count;
        memcpy(asset->node_transforms[asset->node_count], world, sizeof(PMat4));
        asset->node_count++;

        gltf_mesh = node->mesh;
        for(size_t p = 0; p < gltf_mesh->primitives_count; p++)
        {
            res = process_primitive(pigment, asset, data, &gltf_mesh->primitives[p], this_node_idx);
            if(res != PIGMENT_SUCCESS)
            {
                return res;
            }
        }
    }

    for(size_t c = 0; c < node->children_count; c++)
    {
        PResult res = process_node(pigment, asset, data, node->children[c]);
        if(res != PIGMENT_SUCCESS)
        {
            return res;
        }
    }
    return PIGMENT_SUCCESS;
}

static cgltf_result io_cgltf_read(const cgltf_memory_options* memory_options, const cgltf_file_options* file, const char* path, cgltf_size* size, void** data)
{
    (void) memory_options;
    const IOCallbacks* io = (const IOCallbacks*) file->user_data;

    uint64_t out_size  = 0;
    unsigned char* buf = io->read_file(io->user_data, path, &out_size);
    if(buf == NULL)
    {
        return cgltf_result_file_not_found;
    }

    *size = (cgltf_size) out_size;
    *data = buf;
    return cgltf_result_success;
}

static void io_cgltf_release(const cgltf_memory_options* memory_options, const cgltf_file_options* file, void* data)
{
    (void) memory_options;
    const IOCallbacks* io = (const IOCallbacks*) file->user_data;
    io->free_file(io->user_data, (unsigned char*) data);
}

static void* pigment_cgltf_alloc(void* user, cgltf_size size)
{
    return P_ALLOC_OBJECT((Pigment*) user, size, _Alignof(max_align_t));
}

static void pigment_cgltf_free(void* user, void* ptr)
{
    P_FREE((Pigment*) user, ptr);
}

MeshAsset* load_gltf_mesh(Pigment* pigment, const IOCallbacks* io, const char* filepath)
{
    IOCallbacks default_io = pigment_std_default_file_io(pigment);
    if(io == NULL)
    {
        io = &default_io;
    }

    cgltf_options options = {
        .memory = {.alloc_func = pigment_cgltf_alloc, .free_func = pigment_cgltf_free,    .user_data = pigment},
        .file   = {            .read = io_cgltf_read,     .release = io_cgltf_release, .user_data = (void*) io},
    };
    cgltf_data* data = NULL;
    MeshAsset* asset = NULL;
    size_t base_len  = 0;

    if(cgltf_parse_file(&options, filepath, &data) != cgltf_result_success)
    {
        PLOG_ERROR(pigment, "Failed to parse glTF file: %s", filepath);
        return NULL;
    }

    if(cgltf_load_buffers(&options, data, filepath) != cgltf_result_success)
    {
        PLOG_ERROR(pigment, "Failed to load glTF buffers: %s", filepath);
        goto FREE;
    }

    for(size_t i = 0; filepath[i]; i++)
    {
        if(filepath[i] == '/' || filepath[i] == '\\')
        {
            base_len = i + 1;
        }
    }

    asset = P_NEW_FOR_OBJECT(pigment, asset);
    if(asset == NULL)
    {
        goto FREE;
    }

    if(data->images_count > 0)
    {
        asset->images = P_NEW_ARRAY_FOR_OBJECT(pigment, asset->images, data->images_count);
        if(asset->images == NULL)
        {
            goto ERROR;
        }
        asset->image_count = (uint32_t) data->images_count;

        for(size_t i = 0; i < data->images_count; i++)
        {
            cgltf_image* img = &data->images[i];
            int w, h, channels;
            unsigned char* pixels = NULL;

            if(img->buffer_view)
            {
                const unsigned char* buf = (const unsigned char*) img->buffer_view->buffer->data + img->buffer_view->offset;
                pixels                   = stbi_load_from_memory(buf, (int) img->buffer_view->size, &w, &h, &channels, STBI_rgb_alpha);
            }
            else if(img->uri)
            {
                if(strncmp(img->uri, "data:", 5) != 0)
                {
                    size_t uri_len  = strlen(img->uri);
                    char* full_path = P_ALLOC_COMMAND(pigment, base_len + uri_len + 1, _Alignof(char));
                    if(full_path)
                    {
                        memcpy(full_path, filepath, base_len);
                        memcpy(full_path + base_len, img->uri, uri_len + 1);

                        uint64_t encoded_size  = 0;
                        unsigned char* encoded = io->read_file(io->user_data, full_path, &encoded_size);
                        if(encoded != NULL)
                        {
                            pixels = stbi_load_from_memory(encoded, (int) encoded_size, &w, &h, &channels, STBI_rgb_alpha);
                            io->free_file(io->user_data, encoded);
                        }
                        P_FREE(pigment, full_path);
                    }
                }
                else
                {
                    const char* comma = strchr(img->uri, ',');
                    if(comma && comma - img->uri >= 7 && strncmp(comma - 7, ";base64", 7) == 0)
                    {
                        const char* b64 = comma + 1;
                        size_t b64_len  = strlen(b64);
                        size_t pad      = 0;
                        if(b64_len >= 1 && b64[b64_len - 1] == '=')
                        {
                            pad++;
                        }
                        if(b64_len >= 2 && b64[b64_len - 2] == '=')
                        {
                            pad++;
                        }
                        size_t decoded_size = (b64_len / 4) * 3 - pad;

                        void* decoded = NULL;
                        if(cgltf_load_buffer_base64(&options, decoded_size, b64, &decoded) == cgltf_result_success)
                        {
                            pixels = stbi_load_from_memory((const unsigned char*) decoded, (int) decoded_size, &w, &h, &channels, STBI_rgb_alpha);
                            P_FREE(pigment, decoded);
                        }
                    }
                }
            }

            if(pixels == NULL)
            {
                PLOG_WARN(pigment, "Failed to load glTF image %u (uri=%s)", (unsigned int) i, img->uri ? img->uri : "(embedded)");
                continue;
            }

            asset->images[i].pixels = pixels;
            asset->images[i].width  = (uint32_t) w;
            asset->images[i].height = (uint32_t) h;
        }
    }

    if(data->samplers_count > 0)
    {
        asset->sampler_descs = P_NEW_ARRAY_FOR_OBJECT(pigment, asset->sampler_descs, data->samplers_count);
        if(asset->sampler_descs == NULL)
        {
            goto ERROR;
        }

        for(size_t i = 0; i < data->samplers_count; i++)
        {
            asset->sampler_descs[i] = convert_gltf_sampler(&data->samplers[i]);
        }
        asset->sampler_count = (uint32_t) data->samplers_count;
    }

    // Traverse scene nodes to get world transforms
    for(size_t s = 0; s < data->scenes_count; s++)
    {
        cgltf_scene* scene = &data->scenes[s];
        for(size_t n = 0; n < scene->nodes_count; n++)
        {
            if(process_node(pigment, asset, data, scene->nodes[n]) != PIGMENT_SUCCESS)
            {
                goto ERROR;
            }
        }
    }

    goto FREE;

ERROR:
    free_mesh_asset(pigment, asset);
    asset = NULL;

FREE:
    cgltf_free(data);
    return asset;
}

PResult upload_mesh_textures(Pigment* pigment, PStdBindless* bindless, MeshAsset* asset, PMaterials* materials)
{
    if(pigment == NULL || asset == NULL)
    {
        return PIGMENT_ERROR;
    }

    PResult status               = PIGMENT_SUCCESS;
    const unsigned char** pixels = NULL;
    uint32_t* tex_map            = NULL;
    uint32_t* samp_map           = NULL;
    uint32_t* widths             = NULL;
    uint32_t* heights            = NULL;
    PFormat* formats             = NULL;
    uint32_t* src_indices        = NULL;

    // Upload one batch of valid images contiguously and store indices in an array
    if(asset->image_count > 0)
    {
        tex_map     = P_NEW_ARRAY_FOR_OBJECT(pigment, tex_map, asset->image_count);
        pixels      = P_NEW_ARRAY_FOR_OBJECT(pigment, pixels, asset->image_count);
        widths      = P_NEW_ARRAY_FOR_OBJECT(pigment, widths, asset->image_count);
        heights     = P_NEW_ARRAY_FOR_OBJECT(pigment, heights, asset->image_count);
        formats     = P_NEW_ARRAY_FOR_OBJECT(pigment, formats, asset->image_count);
        src_indices = P_NEW_ARRAY_FOR_OBJECT(pigment, src_indices, asset->image_count);

        if(tex_map == NULL || pixels == NULL || widths == NULL || heights == NULL || formats == NULL || src_indices == NULL)
        {
            status = PIGMENT_ERROR_OUT_OF_MEMORY;
            goto FREE;
        }

        uint32_t valid = 0;
        for(uint32_t i = 0; i < asset->image_count; i++)
        {
            // If image could not load, use missing texture (inImageIndex == -1 in fragment shader)
            if(asset->images[i].pixels == NULL)
            {
                tex_map[i] = MISSING_TEXTURE;
                continue;
            }

            pixels[valid]      = asset->images[i].pixels;
            widths[valid]      = asset->images[i].width;
            heights[valid]     = asset->images[i].height;
            formats[valid]     = P_FORMAT_R8G8B8A8_SRGB;
            src_indices[valid] = i;
            valid++;
        }

        if(valid > 0)
        {
            uint32_t start_slot = pigment_std_add_image_batch(pigment, bindless, pixels, widths, heights, formats, valid);
            for(uint32_t i = 0; i < valid; i++)
            {
                tex_map[src_indices[i]] = start_slot + i;
            }
        }
    }

    if(asset->sampler_count > 0)
    {
        samp_map = P_NEW_ARRAY_FOR_OBJECT(pigment, samp_map, asset->sampler_count);
        if(samp_map == NULL)
        {
            goto FREE;
        }
        for(uint32_t i = 0; i < asset->sampler_count; i++)
        {
            samp_map[i] = pigment_std_add_sampler(pigment, bindless, &asset->sampler_descs[i]);
        }
    }

    for(uint32_t i = 0; i < asset->surface_count; i++)
    {
        PMaterialDesc desc = asset->surface_descs[i];

        int32_t* image_fields[]   = {&desc.albedo_image, &desc.metallic_roughness_image, &desc.normal_image, &desc.emissive_image, &desc.occlusion_image};
        int32_t* sampler_fields[] = {&desc.albedo_sampler, &desc.metallic_roughness_sampler, &desc.normal_sampler, &desc.emissive_sampler, &desc.occlusion_sampler};

        for(uint32_t k = 0; k < sizeof(image_fields) / sizeof(image_fields[0]); k++)
        {
            int32_t img = *image_fields[k];
            if(img == (int32_t) NO_MATERIAL)
            {
                *image_fields[k] = 0;
            }
            else if(tex_map && (uint32_t) img < asset->image_count)
            {
                *image_fields[k] = (int32_t) tex_map[img];
            }

            int32_t samp = *sampler_fields[k];
            if(samp == (int32_t) NO_MATERIAL)
            {
                *sampler_fields[k] = 0;
            }
            else if(samp_map && (uint32_t) samp < asset->sampler_count)
            {
                *sampler_fields[k] = (int32_t) samp_map[samp];
            }
        }

        if(materials != NULL)
        {
            uint32_t mid = pigment_std_material_create(pigment, materials, &desc);
            if(mid == UINT32_MAX)
            {
                status = PIGMENT_ERROR;
                goto FREE;
            }
            asset->surfaces[i].material_id = mid;
        }
    }

    P_FREE(pigment, asset->surface_descs);
    asset->surface_descs = NULL;

FREE:
    P_FREE(pigment, pixels);
    P_FREE(pigment, tex_map);
    P_FREE(pigment, samp_map);
    P_FREE(pigment, widths);
    P_FREE(pigment, heights);
    P_FREE(pigment, formats);
    P_FREE(pigment, src_indices);
    return status;
}

void free_mesh_asset(Pigment* pigment, MeshAsset* asset)
{
    if(!asset)
    {
        return;
    }

    for(uint32_t i = 0; i < asset->image_count; i++)
    {
        stbi_image_free(asset->images[i].pixels);
    }

    P_FREE(pigment, asset->images);
    P_FREE(pigment, asset->sampler_descs);

    P_FREE(pigment, asset->vertices);
    P_FREE(pigment, asset->indices);
    P_FREE(pigment, asset->surfaces);
    P_FREE(pigment, asset->surface_descs);
    P_FREE(pigment, asset->node_transforms);
    P_FREE(pigment, asset);
}
