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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "hashmap.h"

#define HASHSET_SIZE ((1 << 16) + 1)

// Vertex to uint32 hashmap

typedef struct _vertex_hashmap_node {
    Vertex key;
    uint32_t value;
    struct _vertex_hashmap_node* next;
} VertexHashmapNode;

struct _vertex_hashmap {
    VertexHashmapNode* nodes[HASHSET_SIZE];
    Vertex** key_list;
    int key_list_size;
    int key_list_allocated;
};

static uint16_t _vertex_hashmap_calc_hash(const Vertex* key)
{
    uint16_t hash  = 0;
    uint16_t* _key = (uint16_t*) key;
    for(size_t i = 0; i < sizeof(Vertex) / sizeof(uint16_t); i++)
    {
        hash ^= *_key;
        _key++;
    }
    return hash;
}

VertexHashMap* vertex_hashmap_create(void)
{
    return calloc(1, sizeof(VertexHashMap));
}

bool _vertex_key_list_append(VertexHashMap* hashmap, Vertex* key)
{
    if(hashmap->key_list_size >= hashmap->key_list_allocated)
    {
        if(hashmap->key_list_allocated == 0)
        {
            hashmap->key_list_allocated = 64;
        }
        else
        {
            hashmap->key_list_allocated *= 2;
        }
        void* temp = realloc(hashmap->key_list, (size_t) hashmap->key_list_allocated * sizeof(Vertex*));
        if(temp == NULL)
        {
            return false;
        }
        hashmap->key_list = temp;
    }
    hashmap->key_list[hashmap->key_list_size] = key;
    hashmap->key_list_size++;

    return true;
}

static inline bool compare_vertices(const Vertex* v1, const Vertex* v2)
{
    return memcmp(v1, v2, sizeof(Vertex)) == 0;
}

bool vertex_hashmap_set_value(VertexHashMap* hashmap, const Vertex* key, uint32_t value)
{
    uint16_t hash                = _vertex_hashmap_calc_hash(key);
    VertexHashmapNode* last_node = hashmap->nodes[hash];
    VertexHashmapNode* node;
    if(hashmap->nodes[hash] != NULL)
    {
        while(last_node->next != NULL && !compare_vertices(&(last_node->key), key))
        {
            last_node = last_node->next;
        }
        if(compare_vertices(&(last_node->key), key))
        {
            last_node->value = value;
            return true;
        }
    }
    node = (VertexHashmapNode*) malloc(sizeof(VertexHashmapNode));
    if(node == NULL)
    {
        return false;
    }
    node->key = *key;
    _vertex_key_list_append(hashmap, &(node->key));
    node->value = value;
    node->next  = NULL;
    if(last_node != NULL)
    {
        last_node->next = node;
    }
    else
    {
        hashmap->nodes[hash] = node;
    }
    return true;
}

int vertex_hashmap_get_nb_keys(const VertexHashMap* hashmap)
{
    return hashmap->key_list_size;
}

Vertex** vertex_hashmap_get_keys_list(const VertexHashMap* hashmap)
{
    return hashmap->key_list;
}

int32_t vertex_hashmap_get_value(const VertexHashMap* hashmap, const Vertex* key)
{
    uint16_t hash           = _vertex_hashmap_calc_hash(key);
    VertexHashmapNode* node = hashmap->nodes[hash];
    while(node != NULL && !compare_vertices(&(node->key), key))
    {
        node = node->next;
    }
    if(node == NULL)
    {
        return -1;
    }
    return (int32_t) node->value;
}

static void _free_vertex_list(VertexHashmapNode* node)
{
    if(node == NULL)
    {
        return;
    }
    _free_vertex_list(node->next);
    free(node);
}

/*
Free the underlying hashmap behind the pointer HASHMAP and set the hashmap handler to NULL.
*/
void vertex_hashmap_free(VertexHashMap** hashmap)
{
    if(*hashmap == NULL)
    {
        return;
    }
    for(int i = 0; i < HASHSET_SIZE; i++)
    {
        _free_vertex_list((*hashmap)->nodes[i]);
    }
    free((*hashmap)->key_list);
    free(*hashmap);
    *hashmap = NULL;
}

// texture_name (char*) to uint16 hashmap

typedef struct _texture_hashmap_node {
    char* key;
    uint16_t value;
    struct _texture_hashmap_node* next;
} TextureHashmapNode;

struct _texture_hashmap {
    TextureHashmapNode* nodes[HASHSET_SIZE];
    char** key_list;
    int key_list_size;
    int key_list_allocated;
};

static uint16_t _texture_hashmap_calc_hash(const char* key)
{
    uint16_t hash = 0;
    while(*key != '\0' && *(key + 1) != '\0')
    {
        hash ^= *((uint16_t*) key);
        key += 2;
    }
    if(*key != '\0')
    {
        hash ^= *((uint16_t*) key);
    }
    return hash;
}

TextureHashMap* texture_hashmap_create(void)
{
    return calloc(1, sizeof(TextureHashMap));
}

bool _texture_key_list_append(TextureHashMap* hashmap, char* key)
{
    if(hashmap->key_list_size >= hashmap->key_list_allocated)
    {
        if(hashmap->key_list_allocated == 0)
        {
            hashmap->key_list_allocated = 64;
        }
        else
        {
            hashmap->key_list_allocated *= 2;
        }
        void* temp = realloc(hashmap->key_list, (size_t) hashmap->key_list_allocated * sizeof(char*));
        if(temp == NULL)
        {
            return false;
        }
        hashmap->key_list = temp;
    }
    hashmap->key_list[hashmap->key_list_size] = key;
    hashmap->key_list_size++;

    return true;
}

bool texture_hashmap_set_value(TextureHashMap* hashmap, const char* key, uint16_t value)
{
    uint16_t hash                 = _texture_hashmap_calc_hash(key);
    TextureHashmapNode* last_node = hashmap->nodes[hash];
    TextureHashmapNode* node;
    if(hashmap->nodes[hash] != NULL)
    {
        while(last_node->next != NULL && strcmp(last_node->key, key) != 0)
        {
            last_node = last_node->next;
        }
        if(strcmp(last_node->key, key) == 0)
        {
            last_node->value = value;
            return true;
        }
    }
    node = (TextureHashmapNode*) malloc(sizeof(TextureHashmapNode));
    if(node == NULL)
    {
        return false;
    }
    node->key = (char*) malloc((strlen(key) + 1) * sizeof(char));
    if(node->key == NULL)
    {
        free(node);
        return false;
    }
    strcpy(node->key, key);
    _texture_key_list_append(hashmap, node->key);
    node->value = value;
    node->next  = NULL;
    if(last_node != NULL)
    {
        last_node->next = node;
    }
    else
    {
        hashmap->nodes[hash] = node;
    }
    return true;
}

int texture_hashmap_get_nb_keys(const TextureHashMap* hashmap)
{
    return hashmap->key_list_size;
}

char** texture_hashmap_get_keys_list(const TextureHashMap* hashmap)
{
    return hashmap->key_list;
}

int16_t texture_hashmap_get_value(const TextureHashMap* hashmap, const char* key)
{
    uint16_t hash            = _texture_hashmap_calc_hash(key);
    TextureHashmapNode* node = hashmap->nodes[hash];
    while(node != NULL && strcmp(node->key, key) != 0)
    {
        node = node->next;
    }
    if(node == NULL)
    {
        return -1;
    }
    return (int16_t) node->value;
}

static void _free_texture_list(TextureHashmapNode* node)
{
    if(node == NULL)
    {
        return;
    }
    _free_texture_list(node->next);
    free(node->key);
    free(node);
}

/*
Free the underlying hashmap behind the pointer HASHMAP and set the hashmap handler to NULL.
*/
void texture_hashmap_free(TextureHashMap** hashmap)
{
    if(*hashmap == NULL)
    {
        return;
    }
    for(int i = 0; i < HASHSET_SIZE; i++)
    {
        _free_texture_list((*hashmap)->nodes[i]);
    }
    free((*hashmap)->key_list);
    free(*hashmap);
    *hashmap = NULL;
}
