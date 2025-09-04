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

#include "vertex.h"
#include "structs.h"

static VkVertexInputBindingDescription get_binding_description(void);
static VkVertexInputAttributeDescription* get_attribute_descriptions(void);

PVertexDescription* create_vertex_description(void)
{
    PVertexDescription* vertex_description = malloc(sizeof(*vertex_description));
    if(vertex_description == NULL)
    {
        perror("malloc");
        return NULL;
    }

    vertex_description->binding_description         = get_binding_description();
    vertex_description->attribute_descriptions_size = 5;
    vertex_description->attribute_descriptions      = get_attribute_descriptions();

    return vertex_description;
}

void destroy_vertex_description(PVertexDescription* vertex_description)
{
    if(vertex_description == NULL)
    {
        return;
    }
    free(vertex_description->attribute_descriptions);
    free(vertex_description);
}

static VkVertexInputBindingDescription get_binding_description(void)
{
    VkVertexInputBindingDescription binding_description = {
        .binding   = 0,
        .stride    = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    return binding_description;
}

static VkVertexInputAttributeDescription* get_attribute_descriptions(void)
{
    const uint32_t attr_count = 5;
    VkVertexInputAttributeDescription* attribute_descriptions = calloc(attr_count, sizeof(*attribute_descriptions));
    if(attribute_descriptions == NULL)
    {
        perror("get_attribute_descriptions");
        return NULL;
    }

    attribute_descriptions[0].binding  = 0;
    attribute_descriptions[0].location = 0;
    attribute_descriptions[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[0].offset   = offsetof(Vertex, pos);

    attribute_descriptions[1].binding  = 0;
    attribute_descriptions[1].location = 1;
    attribute_descriptions[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[1].offset   = offsetof(Vertex, color);

    attribute_descriptions[2].binding  = 0;
    attribute_descriptions[2].location = 2;
    attribute_descriptions[2].format   = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[2].offset   = offsetof(Vertex, texture_coord);

    attribute_descriptions[3].binding  = 0;
    attribute_descriptions[3].location = 3;
    attribute_descriptions[3].format   = VK_FORMAT_R32_SINT;
    attribute_descriptions[3].offset   = offsetof(Vertex, texture_index);

    attribute_descriptions[4].binding  = 0;
    attribute_descriptions[4].location = 4;
    attribute_descriptions[4].format   = VK_FORMAT_R32_SINT;
    attribute_descriptions[4].offset   = offsetof(Vertex, sampler_index);

    return attribute_descriptions;
}
