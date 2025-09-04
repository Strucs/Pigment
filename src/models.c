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


#include "models.h"
#include "structs.h"

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "lib/tinyobj_loader_c.h"

#define INITIAL_SIZE 262144

void vertices_list_append(PModel* model, Vertex vertex);
void indices_list_append(PModel* model, uint32_t indice);
void load_file(void* ctx __attribute__((unused)), const char* filename, const int is_mtl __attribute__((unused)), const char* obj_filename __attribute__((unused)), char** buffer, size_t* len);

PModel* create_model(void)
{
    PModel* model = calloc(1, sizeof(*model));
    if(model == NULL)
    {
        perror("malloc");
        return NULL;
    }

    model->vertices = malloc(INITIAL_SIZE * sizeof(*model->vertices));
    if(model->vertices == NULL)
    {
        perror("malloc");
        free(model);
        return NULL;
    }

    model->vertices_size = INITIAL_SIZE;
    model->indices       = malloc(INITIAL_SIZE * sizeof(*model->indices));
    if(model->indices == NULL)
    {
        perror("malloc");
        free(model->vertices);
        free(model);
        return NULL;
    }

    model->indices_size = INITIAL_SIZE;

    return model;
}

void destroy_model(PModel* model)
{
    if(model == NULL)
    {
        return;
    }
    free(model->vertices);
    free(model->indices);
    free(model);
}

void load_model_multi_textures(const char* filepath, float x_pos, float y_pos, float z_pos, float scale, TextureHashMap* textures_to_load, PModel* model)
{
    tinyobj_attrib_t attrib;
    tinyobj_shape_t* shapes       = NULL;
    tinyobj_material_t* materials = NULL;

    size_t num_shapes;
    size_t num_materials;

    tinyobj_attrib_init(&attrib);

    if(tinyobj_parse_obj(&attrib, &shapes, &num_shapes, &materials, &num_materials, filepath, load_file, NULL, TINYOBJ_FLAG_TRIANGULATE) != TINYOBJ_SUCCESS)
    {
        fprintf(stderr, "Failed to load model!\n");
        return;
    }

    VertexHashMap* vertices_dictionnary = vertex_hashmap_create();

    for(size_t i = 0; i < num_shapes; i++)
    {
        for(size_t j = 0; j < attrib.num_faces; j++)
        {
            Vertex vertex = {
                .pos = {
                        attrib.vertices[3 * attrib.faces[j].v_idx + 0],
                        attrib.vertices[3 * attrib.faces[j].v_idx + 1],
                        attrib.vertices[3 * attrib.faces[j].v_idx + 2],
                        },

                .color = {1.0f, 1.0f, 1.0f},

                .texture_coord = {attrib.texcoords[2 * attrib.faces[j].vt_idx + 0], 1.0f - attrib.texcoords[2 * attrib.faces[j].vt_idx + 1]},

                .texture_index = 0,

                .sampler_index = NEAREST,
            };

            vec3 position = {x_pos, y_pos, z_pos};

            glm_vec3_add(vertex.pos, position, vertex.pos);
            glm_vec3_scale(vertex.pos, scale, vertex.pos);

            if(materials[attrib.material_ids[j / 3]].diffuse_texname != NULL)
            {
                vertex.texture_index = (uint16_t) texture_hashmap_get_value(textures_to_load, materials[attrib.material_ids[j / 3]].diffuse_texname);
            }

            uint32_t value;
            if((value = (uint32_t) vertex_hashmap_get_value(vertices_dictionnary, &vertex)) == (uint32_t) -1)
            {
                value = model->vertices_number;
                vertex_hashmap_set_value(vertices_dictionnary, &vertex, value);
                vertices_list_append(model, vertex);
            }
            indices_list_append(model, value);
        }
    }

    vertex_hashmap_free(&vertices_dictionnary);

    tinyobj_attrib_free(&attrib);
    tinyobj_shapes_free(shapes, num_shapes);
    tinyobj_materials_free(materials, num_materials);
}

void load_model(const char* filepath, float x_pos, float y_pos, float z_pos, float scale, uint16_t texture_index, PModel* model)
{
    tinyobj_attrib_t attrib;
    tinyobj_shape_t* shapes       = NULL;
    tinyobj_material_t* materials = NULL;

    size_t num_shapes;
    size_t num_materials;

    tinyobj_attrib_init(&attrib);

    if(tinyobj_parse_obj(&attrib, &shapes, &num_shapes, &materials, &num_materials, filepath, load_file, NULL, TINYOBJ_FLAG_TRIANGULATE) != TINYOBJ_SUCCESS)
    {
        fprintf(stderr, "Failed to load model!\n");
        return;
    }

    VertexHashMap* vertices_dictionnary = vertex_hashmap_create();

    for(size_t i = 0; i < num_shapes; i++)
    {
        for(size_t j = 0; j < attrib.num_faces; j++)
        {
            Vertex vertex = {
                .pos = {
                        attrib.vertices[3 * attrib.faces[j].v_idx + 0],
                        attrib.vertices[3 * attrib.faces[j].v_idx + 1],
                        attrib.vertices[3 * attrib.faces[j].v_idx + 2],
                        },

                .color = {1.0f, 1.0f, 1.0f},

                .texture_coord = {attrib.texcoords[2 * attrib.faces[j].vt_idx + 0], 1.0f - attrib.texcoords[2 * attrib.faces[j].vt_idx + 1]},

                .texture_index = texture_index,

                .sampler_index = NEAREST,
            };

            vec3 position = {x_pos, y_pos, z_pos};

            glm_vec3_add(vertex.pos, position, vertex.pos);
            glm_vec3_scale(vertex.pos, scale, vertex.pos);

            uint32_t value;
            if((value = (uint32_t) vertex_hashmap_get_value(vertices_dictionnary, &vertex)) == (uint32_t) -1)
            {
                value = model->vertices_number;
                vertex_hashmap_set_value(vertices_dictionnary, &vertex, value);
                vertices_list_append(model, vertex);
            }
            indices_list_append(model, value);
        }
    }

    vertex_hashmap_free(&vertices_dictionnary);

    tinyobj_attrib_free(&attrib);
    tinyobj_shapes_free(shapes, num_shapes);
    tinyobj_materials_free(materials, num_materials);
}

void vertices_list_append(PModel* model, Vertex vertex)
{
    if(model->vertices_number >= model->vertices_size)
    {
        model->vertices_size *= 2;
        model->vertices = realloc(model->vertices, model->vertices_size * sizeof(*(model->vertices)));
        if(model->vertices == NULL)
        {
            perror("realloc");
            return;
        }
    }
    model->vertices[model->vertices_number] = vertex;
    model->vertices_number++;
}

void indices_list_append(PModel* model, uint32_t indice)
{
    if(model->indices_number >= model->indices_size)
    {
        model->indices_size *= 2;
        void* temp = realloc(model->indices, model->indices_size * sizeof(*(model->indices)));
        if(temp == NULL)
        {
            perror("realloc");
            return;
        }
        model->indices = temp;
    }
    model->indices[model->indices_number] = indice;
    model->indices_number++;
}

void load_file(void* ctx __attribute__((unused)), const char* filename, const int is_mtl __attribute__((unused)), const char* obj_filename __attribute__((unused)), char** buffer, size_t* len)
{
    FILE* fd           = fopen(filename, "rb+");
    size_t string_size = 0;
    size_t read_size   = 0;

    if(fd)
    {
        fseek(fd, 0, SEEK_END);
        string_size = (size_t) ftell(fd);
        rewind(fd);
        *buffer   = malloc(sizeof(**buffer) * (string_size + 1));
        if(*buffer != NULL)
        {
            read_size = fread(*buffer, sizeof(char), (size_t) string_size, fd);
            if(string_size != read_size)
            {
                free(*buffer);
                *buffer = NULL;
            }
        }
        fclose(fd);
    }

    *len = read_size;
}

void load_cube(float size, float x_pos, float y_pos, float z_pos, uint16_t texture_index, PModel* model)
{
    float half_cube_size = size / 2.0f;
    vec3 cube_center     = {x_pos, y_pos, z_pos};
    FilteringMode filtering_mode = LINEAR;

    Vertex vertices[] = {
        // +X face
        {{cube_center[0] + half_cube_size, cube_center[1] - half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] - half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] + half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] + half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, texture_index, filtering_mode},

        // -X face
        {{cube_center[0] - half_cube_size, cube_center[1] + half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] + half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] - half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] - half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, texture_index, filtering_mode},

        // +Y face
        {{cube_center[0] + half_cube_size, cube_center[1] + half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] + half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] + half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] + half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, texture_index, filtering_mode},

        // -Y face
        {{cube_center[0] - half_cube_size, cube_center[1] - half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] - half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] - half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] - half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, texture_index, filtering_mode},

        // +Z face
        {{cube_center[0] - half_cube_size, cube_center[1] - half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] - half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] + half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] + half_cube_size, cube_center[2] + half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, texture_index, filtering_mode},

        // -Z face
        {{cube_center[0] + half_cube_size, cube_center[1] - half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] - half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] - half_cube_size, cube_center[1] + half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}, texture_index, filtering_mode},
        {{cube_center[0] + half_cube_size, cube_center[1] + half_cube_size, cube_center[2] - half_cube_size}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}, texture_index, filtering_mode},
    };

    uint32_t vertices_size = sizeof(vertices) / sizeof(vertices[0]);

    uint32_t offset = model->vertices_number;

    for(size_t i = 0; i < vertices_size; i++)
    {
        vertices_list_append(model, vertices[i]);
    }

    // +X face
    indices_list_append(model, offset + 0);
    indices_list_append(model, offset + 1);
    indices_list_append(model, offset + 2);
    indices_list_append(model, offset + 2);
    indices_list_append(model, offset + 3);
    indices_list_append(model, offset + 0);

    // -X face
    indices_list_append(model, offset + 4);
    indices_list_append(model, offset + 5);
    indices_list_append(model, offset + 6);
    indices_list_append(model, offset + 6);
    indices_list_append(model, offset + 7);
    indices_list_append(model, offset + 4);

    // +Y face
    indices_list_append(model, offset + 8);
    indices_list_append(model, offset + 9);
    indices_list_append(model, offset + 10);
    indices_list_append(model, offset + 10);
    indices_list_append(model, offset + 11);
    indices_list_append(model, offset + 8);

    // -X face
    indices_list_append(model, offset + 12);
    indices_list_append(model, offset + 13);
    indices_list_append(model, offset + 14);
    indices_list_append(model, offset + 14);
    indices_list_append(model, offset + 15);
    indices_list_append(model, offset + 12);

    // +Z face
    indices_list_append(model, offset + 16);
    indices_list_append(model, offset + 17);
    indices_list_append(model, offset + 18);
    indices_list_append(model, offset + 18);
    indices_list_append(model, offset + 19);
    indices_list_append(model, offset + 16);

    // -X face
    indices_list_append(model, offset + 20);
    indices_list_append(model, offset + 21);
    indices_list_append(model, offset + 22);
    indices_list_append(model, offset + 22);
    indices_list_append(model, offset + 23);
    indices_list_append(model, offset + 20);
}
