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
#include "texture.h"
#include "log_internal.h"

#include <stdlib.h>
#include <string.h>

#include <cgltf.h>
#include <stb_image.h>

#define NO_MATERIAL UINT32_MAX
#define MISSING_TEXTURE UINT32_MAX
#define DEFAULT_CAPACITY 8

typedef struct PrimAttrs {
    cgltf_accessor* pos;
    cgltf_accessor* normal;
    cgltf_accessor* uv;
    cgltf_accessor* color;
} PrimAttrs;

static int darray_reserve(void** ptr, uint32_t count, uint32_t* cap, uint32_t add, size_t elem_size);
static PSamplerDesc convert_gltf_sampler(cgltf_sampler* s);
static PrimAttrs find_primitive_attrs(cgltf_primitive* prim);
static int append_primitive_vertices(MeshAsset* asset, const PrimAttrs* attrs);
static PMaterialDesc resolve_primitive_material(cgltf_material* mat, cgltf_data* data);
static int append_surface(MeshAsset* asset, PRawSurface surface, const PMaterialDesc* desc);
static int process_primitive(MeshAsset* asset, cgltf_data* data, cgltf_primitive* prim, uint32_t node_idx);
static int process_node(MeshAsset* asset, cgltf_data* data, cgltf_node* node);

static int darray_reserve(void** ptr, uint32_t count, uint32_t* cap, uint32_t add, size_t elem_size)
{
    if((uint64_t) count + add <= *cap)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t new_cap = (*cap == 0) ? DEFAULT_CAPACITY : *cap;
    while(new_cap < count + add)
    {
        new_cap *= 2;
    }

    void* new_ptr = realloc(*ptr, elem_size * new_cap);
    if(new_ptr == NULL)
    {
        return PIGMENT_ERROR;
    }

    *ptr = new_ptr;
    *cap = new_cap;
    return PIGMENT_SUCCESS;
}

static PSamplerDesc convert_gltf_sampler(cgltf_sampler* s)
{
    PSamplerDesc desc = {0};

    desc.mag_filter = (s->mag_filter == cgltf_filter_type_nearest) ? NEAREST : LINEAR;

    switch(s->min_filter)
    {
        case cgltf_filter_type_nearest:
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_nearest_mipmap_linear:
            desc.min_filter = NEAREST;
            break;
        default:
            desc.min_filter = LINEAR;
            break;
    }

    switch(s->min_filter)
    {
        case cgltf_filter_type_nearest_mipmap_nearest:
        case cgltf_filter_type_linear_mipmap_nearest:
            desc.mipmap_mode = NEAREST;
            break;
        default:
            desc.mipmap_mode = LINEAR;
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

static int append_primitive_vertices(MeshAsset* asset, const PrimAttrs* attrs)
{
    if(attrs->pos == NULL || attrs->pos->count == 0)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t add = (uint32_t) attrs->pos->count;
    if(darray_reserve((void**) &asset->vertices, asset->vertex_count, &asset->vertex_capacity, add, sizeof(PVertex)))
    {
        return PIGMENT_ERROR;
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
            float uv[2];
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

static int append_primitive_indices(MeshAsset* asset, cgltf_accessor* indices, uint32_t base_vertex)
{
    if(indices == NULL || indices->count == 0)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t add = (uint32_t) indices->count;
    if(darray_reserve((void**) &asset->indices, asset->index_count, &asset->index_capacity, add, sizeof(uint32_t)) == PIGMENT_ERROR)
    {
        return PIGMENT_ERROR;
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
        .occlusion_scale         = 1.0f,
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

static int append_surface(MeshAsset* asset, PRawSurface surface, const PMaterialDesc* desc)
{
    if((uint64_t) asset->surface_count + 1 > asset->surface_capacity)
    {
        uint32_t new_cap = (asset->surface_capacity == 0) ? DEFAULT_CAPACITY : asset->surface_capacity * 2;

        PRawSurface* new_surfaces = realloc(asset->surfaces, sizeof(PRawSurface) * new_cap);
        PMaterialDesc* new_descs  = realloc(asset->surface_descs, sizeof(PMaterialDesc) * new_cap);

        if(new_surfaces == NULL || new_descs == NULL)
        {
            return PIGMENT_ERROR;
        }

        asset->surfaces         = new_surfaces;
        asset->surface_descs    = new_descs;
        asset->surface_capacity = new_cap;
    }

    asset->surfaces[asset->surface_count]      = surface;
    asset->surface_descs[asset->surface_count] = *desc;
    asset->surface_count++;

    return PIGMENT_SUCCESS;
}

static int process_primitive(MeshAsset* asset, cgltf_data* data, cgltf_primitive* prim, uint32_t node_idx)
{
    PrimAttrs attrs = find_primitive_attrs(prim);

    uint32_t base_vertex = asset->vertex_count;
    uint32_t index_start = asset->index_count;

    if(append_primitive_vertices(asset, &attrs) == PIGMENT_ERROR)
    {
        return PIGMENT_ERROR;
    }

    if(prim->indices)
    {
        if(append_primitive_indices(asset, prim->indices, base_vertex) == PIGMENT_ERROR)
        {
            return PIGMENT_ERROR;
        }
    }
    else if(attrs.pos)
    {
        uint32_t add = (uint32_t) attrs.pos->count;
        if(darray_reserve((void**) &asset->indices, asset->index_count, &asset->index_capacity, add, sizeof(uint32_t)) == PIGMENT_ERROR)
        {
            return PIGMENT_ERROR;
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

    return append_surface(asset, surface, &desc);
}

static int process_node(MeshAsset* asset, cgltf_data* data, cgltf_node* node)
{
    if(node->mesh)
    {
        float world[16];
        uint32_t this_node_idx;
        cgltf_mesh* gltf_mesh = NULL;

        cgltf_node_transform_world(node, world);

        if(darray_reserve((void**) &asset->node_transforms, asset->node_count, &asset->node_capacity, 1, sizeof(mat4)) == PIGMENT_ERROR)
        {
            return PIGMENT_ERROR;
        }

        this_node_idx = asset->node_count;
        memcpy(asset->node_transforms[asset->node_count], world, sizeof(mat4));
        asset->node_count++;

        gltf_mesh = node->mesh;
        for(size_t p = 0; p < gltf_mesh->primitives_count; p++)
        {
            if(process_primitive(asset, data, &gltf_mesh->primitives[p], this_node_idx) == PIGMENT_ERROR)
            {
                return PIGMENT_ERROR;
            }
        }
    }

    for(size_t c = 0; c < node->children_count; c++)
    {
        if(process_node(asset, data, node->children[c]) == PIGMENT_ERROR)
        {
            return PIGMENT_ERROR;
        }
    }
    return PIGMENT_SUCCESS;
}

MeshAsset* load_gltf_mesh(Pigment* pigment, const char* filepath)
{
    cgltf_options options = {0};
    cgltf_data* data      = NULL;
    MeshAsset* asset      = NULL;
    size_t base_len       = 0;

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

    asset = calloc(1, sizeof(MeshAsset));
    if(asset == NULL)
    {
        goto FREE;
    }

    if(data->images_count > 0)
    {
        asset->images = calloc(data->images_count, sizeof(PRawImage));
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
                    char* full_path = malloc(base_len + uri_len + 1);
                    if(full_path)
                    {
                        memcpy(full_path, filepath, base_len);
                        memcpy(full_path + base_len, img->uri, uri_len + 1);
                        pixels = stbi_load(full_path, &w, &h, &channels, STBI_rgb_alpha);
                        free(full_path);
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
                            free(decoded);
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
        asset->sampler_descs = calloc(data->samplers_count, sizeof(PSamplerDesc));
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
            if(process_node(asset, data, scene->nodes[n]) == PIGMENT_ERROR)
            {
                goto ERROR;
            }
        }
    }

    goto FREE;

ERROR:
    free_mesh_asset(asset);
    asset = NULL;

FREE:
    cgltf_free(data);
    return asset;
}

int upload_mesh_textures(Pigment* pigment, MeshAsset* asset, PMaterials* materials)
{
    if(pigment == NULL || asset == NULL)
    {
        return PIGMENT_ERROR;
    }

    int status                   = PIGMENT_SUCCESS;
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
        tex_map     = calloc(asset->image_count, sizeof(*tex_map));
        pixels      = malloc(asset->image_count * sizeof(*pixels));
        widths      = malloc(asset->image_count * sizeof(*widths));
        heights     = malloc(asset->image_count * sizeof(*heights));
        formats     = malloc(asset->image_count * sizeof(*formats));
        src_indices = malloc(asset->image_count * sizeof(*src_indices));

        if(tex_map == NULL || pixels == NULL || widths == NULL || heights == NULL || formats == NULL || src_indices == NULL)
        {
            status = PIGMENT_ERROR;
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
            uint32_t start_slot = pigment_upload_image_batch(pigment, pixels, widths, heights, formats, valid);
            for(uint32_t i = 0; i < valid; i++)
            {
                tex_map[src_indices[i]] = start_slot + i;
            }
        }
    }

    if(asset->sampler_count > 0)
    {
        samp_map = calloc(asset->sampler_count, sizeof(*samp_map));
        if(samp_map == NULL)
        {
            goto FREE;
        }
        for(uint32_t i = 0; i < asset->sampler_count; i++)
        {
            samp_map[i] = pigment_add_sampler(pigment, &asset->sampler_descs[i]);
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

    free(asset->surface_descs);
    asset->surface_descs = NULL;

FREE:
    free(pixels);
    free(tex_map);
    free(samp_map);
    free(widths);
    free(heights);
    free(formats);
    free(src_indices);
    return status;
}

void free_mesh_asset(MeshAsset* asset)
{
    if(!asset)
    {
        return;
    }

    for(uint32_t i = 0; i < asset->image_count; i++)
    {
        stbi_image_free(asset->images[i].pixels);
    }

    free(asset->images);
    free(asset->sampler_descs);

    free(asset->vertices);
    free(asset->indices);
    free(asset->surfaces);
    free(asset->surface_descs);
    free(asset->node_transforms);
    free(asset);
}
