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

#include "primitives.h"

#include "mesh.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdlib.h>
#include <string.h>

static PVertex make_vertex(float px, float py, float pz, float nx, float ny, float nz, float u, float v)
{
    PVertex out = {
        .pos[0]    = px,
        .pos[1]    = py,
        .pos[2]    = pz,
        .normal[0] = nx,
        .normal[1] = ny,
        .normal[2] = nz,
        .uv_x      = u,
        .uv_y      = v,
        .color[0]  = 1.0f,
        .color[1]  = 1.0f,
        .color[2]  = 1.0f,
        .color[3]  = 1.0f,
    };

    return out;
}

PMeshData pigment_cube_mesh(void)
{
    PMeshData mesh    = {0};
    mesh.vertex_count = 24;
    mesh.index_count  = 36;
    mesh.vertices     = malloc(mesh.vertex_count * sizeof(PVertex));
    mesh.indices      = malloc(mesh.index_count * sizeof(uint32_t));
    if(mesh.vertices == NULL || mesh.indices == NULL)
    {
        free(mesh.vertices);
        free(mesh.indices);
        return (PMeshData) {0};
    }

    PVertex* v = mesh.vertices;

    v[0] = make_vertex(-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    v[1] = make_vertex(0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    v[2] = make_vertex(0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    v[3] = make_vertex(-0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    v[4] = make_vertex(0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f);
    v[5] = make_vertex(-0.5f, -0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f);
    v[6] = make_vertex(-0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f);
    v[7] = make_vertex(0.5f, 0.5f, -0.5f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f);

    v[8]  = make_vertex(-0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    v[9]  = make_vertex(0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    v[10] = make_vertex(0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f);
    v[11] = make_vertex(-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);

    v[12] = make_vertex(-0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f);
    v[13] = make_vertex(0.5f, -0.5f, -0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);
    v[14] = make_vertex(0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f);
    v[15] = make_vertex(-0.5f, -0.5f, 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f);

    v[16] = make_vertex(0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[17] = make_vertex(0.5f, -0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[18] = make_vertex(0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    v[19] = make_vertex(0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    v[20] = make_vertex(-0.5f, -0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    v[21] = make_vertex(-0.5f, -0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    v[22] = make_vertex(-0.5f, 0.5f, 0.5f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    v[23] = make_vertex(-0.5f, 0.5f, -0.5f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    static const uint32_t face_indices[6] = {0, 1, 2, 0, 2, 3};
    for(uint32_t face = 0; face < 6; face++)
    {
        uint32_t base = face * 4;
        for(uint32_t i = 0; i < 6; i++)
        {
            mesh.indices[face * 6 + i] = base + face_indices[i];
        }
    }

    return mesh;
}

PMeshData pigment_quad_mesh(void)
{
    PMeshData mesh    = {0};
    mesh.vertex_count = 4;
    mesh.index_count  = 6;
    mesh.vertices     = malloc(mesh.vertex_count * sizeof(PVertex));
    mesh.indices      = malloc(mesh.index_count * sizeof(uint32_t));
    if(mesh.vertices == NULL || mesh.indices == NULL)
    {
        free(mesh.vertices);
        free(mesh.indices);
        return (PMeshData) {0};
    }

    mesh.vertices[0] = make_vertex(-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    mesh.vertices[1] = make_vertex(0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    mesh.vertices[2] = make_vertex(0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    mesh.vertices[3] = make_vertex(-0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    mesh.indices[0] = 0;
    mesh.indices[1] = 1;
    mesh.indices[2] = 2;
    mesh.indices[3] = 0;
    mesh.indices[4] = 2;
    mesh.indices[5] = 3;

    return mesh;
}

PMeshData pigment_plane_mesh(uint32_t segments)
{
    if(segments == 0)
    {
        segments = 1;
    }

    uint32_t side     = segments + 1;
    PMeshData mesh    = {0};
    mesh.vertex_count = side * side;
    mesh.index_count  = segments * segments * 6;
    mesh.vertices     = malloc(mesh.vertex_count * sizeof(PVertex));
    mesh.indices      = malloc(mesh.index_count * sizeof(uint32_t));
    if(mesh.vertices == NULL || mesh.indices == NULL)
    {
        free(mesh.vertices);
        free(mesh.indices);
        return (PMeshData) {0};
    }

    float step = 1.0f / (float) segments;
    for(uint32_t z = 0; z < side; z++)
    {
        for(uint32_t x = 0; x < side; x++)
        {
            float fx                    = (float) x * step - 0.5f;
            float fz                    = (float) z * step - 0.5f;
            float u                     = (float) x * step;
            float v                     = (float) z * step;
            mesh.vertices[z * side + x] = make_vertex(fx, 0.0f, fz, 0.0f, 1.0f, 0.0f, u, v);
        }
    }

    uint32_t* idx = mesh.indices;
    for(uint32_t z = 0; z < segments; z++)
    {
        for(uint32_t x = 0; x < segments; x++)
        {
            uint32_t i0 = z * side + x;
            uint32_t i1 = z * side + x + 1;
            uint32_t i2 = (z + 1) * side + x + 1;
            uint32_t i3 = (z + 1) * side + x;
            *idx++      = i0;
            *idx++      = i2;
            *idx++      = i1;
            *idx++      = i0;
            *idx++      = i3;
            *idx++      = i2;
        }
    }

    return mesh;
}

PMeshData pigment_sphere_mesh(uint32_t lat_segments, uint32_t lon_segments)
{
    if(lat_segments < 2)
    {
        lat_segments = 2;
    }
    if(lon_segments < 3)
    {
        lon_segments = 3;
    }

    PMeshData mesh    = {0};
    mesh.vertex_count = (lat_segments + 1) * (lon_segments + 1);
    mesh.index_count  = lat_segments * lon_segments * 6;
    mesh.vertices     = malloc(mesh.vertex_count * sizeof(PVertex));
    mesh.indices      = malloc(mesh.index_count * sizeof(uint32_t));
    if(mesh.vertices == NULL || mesh.indices == NULL)
    {
        free(mesh.vertices);
        free(mesh.indices);
        return (PMeshData) {0};
    }

    for(uint32_t lat = 0; lat <= lat_segments; lat++)
    {
        float theta     = (float) lat * M_PI / (float) lat_segments;
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);

        for(uint32_t lon = 0; lon <= lon_segments; lon++)
        {
            float phi     = (float) lon * 2.0f * M_PI / (float) lon_segments;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);

            float x = cos_phi * sin_theta;
            float y = cos_theta;
            float z = sin_phi * sin_theta;
            float u = (float) lon / (float) lon_segments;
            float v = (float) lat / (float) lat_segments;

            mesh.vertices[lat * (lon_segments + 1) + lon] = make_vertex(0.5f * x, 0.5f * y, 0.5f * z, x, y, z, u, v);
        }
    }

    uint32_t* idx = mesh.indices;
    for(uint32_t lat = 0; lat < lat_segments; lat++)
    {
        for(uint32_t lon = 0; lon < lon_segments; lon++)
        {
            uint32_t i0 = lat * (lon_segments + 1) + lon;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + (lon_segments + 1);
            uint32_t i3 = i2 + 1;
            *idx++      = i0;
            *idx++      = i1;
            *idx++      = i2;
            *idx++      = i1;
            *idx++      = i3;
            *idx++      = i2;
        }
    }

    return mesh;
}

void pigment_free_mesh_data(PMeshData* mesh)
{
    if(mesh == NULL)
    {
        return;
    }
    free(mesh->vertices);
    free(mesh->indices);
    mesh->vertices     = NULL;
    mesh->indices      = NULL;
    mesh->vertex_count = 0;
    mesh->index_count  = 0;
}

PMeshBuffers* pigment_upload_mesh_data(Pigment* pigment, PCommandPool* pool, const PMeshData* data)
{
    if(data == NULL || data->vertices == NULL || data->indices == NULL)
    {
        return NULL;
    }
    return pigment_std_upload_mesh(pigment, pool, data->vertices, data->vertex_count * sizeof(PVertex), data->indices, data->index_count);
}
