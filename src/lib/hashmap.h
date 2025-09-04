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

#ifndef HASHMAP_H
#define HASHMAP_H

#include "defines.h"

// Vertex to uint32 hashmap

typedef struct _vertex_hashmap VertexHashMap;

VertexHashMap* vertex_hashmap_create(void);

bool vertex_hashmap_set_value(VertexHashMap* hashmap, const Vertex* key, uint32_t value);
int32_t vertex_hashmap_get_value(const VertexHashMap* hashmap, const Vertex* key);

int vertex_hashmap_get_nb_keys(const VertexHashMap* hashmap);
Vertex** vertex_hashmap_get_keys_list(const VertexHashMap* hashmap);

void vertex_hashmap_free(VertexHashMap** hashmap);

// texture_name (char *) to uint16 hashmap

typedef struct _texture_hashmap TextureHashMap;

TextureHashMap* texture_hashmap_create(void);

bool texture_hashmap_set_value(TextureHashMap* hashmap, const char* key, uint16_t value);
int16_t texture_hashmap_get_value(const TextureHashMap* hashmap, const char* key);

int texture_hashmap_get_nb_keys(const TextureHashMap* hashmap);
char** texture_hashmap_get_keys_list(const TextureHashMap* hashmap);

void texture_hashmap_free(TextureHashMap** hashmap);

#endif
