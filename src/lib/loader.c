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

#include "loader.h"
#include "hashmap.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define PATH_ARRAY_MAX_SIZE 256
#define NAME_MAX_SIZE 256
#define PATH_MAX_SIZE 4096

struct StringArray_T {
    char** strings;
    uint8_t size;
};

extern int add_texture(PTextureList* texture_list, const char* texture_path, PCommands* commands, PDevice* device);

TexturesToLoad* init_textures_to_load(void)
{
    TexturesToLoad* texture_hashmap = texture_hashmap_create();
    if(texture_hashmap == NULL)
    {
        perror("init_textures_to_load");
        return NULL;
    }

    texture_hashmap_set_value(texture_hashmap, "default", 0);

    return texture_hashmap;
}

void add_texture_to_load(TexturesToLoad* textures_to_load, const char* texture_name)
{
    int textures_number   = textures_to_load_number(textures_to_load);
    char** textures_names = texture_hashmap_get_keys_list(textures_to_load);

    for(int i = 0; i < textures_number; i++)
    {
        if(strcmp(texture_name, textures_names[i]) == 0)
        {
            return;
        }
    }

    texture_hashmap_set_value(textures_to_load, texture_name, (uint16_t) textures_number);
}

void add_textures_dir_to_load(TexturesToLoad* textures_to_load, const char* texture_dir)
{
    char path[PATH_MAX_SIZE] = {0};
    size_t texture_dir_name_len;
    struct stat path_stat;

    DIR* dir = opendir(texture_dir);
    if(dir == NULL)
    {
        goto ERROR;
    }

    struct dirent* entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        path[0] = '\0';

        texture_dir_name_len = strnlen(texture_dir, PATH_MAX_SIZE);

        strncpy(path, texture_dir, texture_dir_name_len + 1);
        if(path[texture_dir_name_len - 1] != '/')
        {
            strncat(path, "/", 2);
        }
        strncat(path, entry->d_name, NAME_MAX_SIZE + 1);

        stat(path, &path_stat);
        if(S_ISDIR(path_stat.st_mode))
        {
            add_textures_dir_to_load(textures_to_load, path);
        }
        else if(S_ISREG(path_stat.st_mode))
        {
            add_texture_to_load(textures_to_load, entry->d_name);
        }
    }
    closedir(dir);
    return;

ERROR:
    perror("add_textures_dir_to_load");
    return;
}

int textures_to_load_number(const TexturesToLoad* texture_to_load)
{
    return texture_hashmap_get_nb_keys(texture_to_load);
}

char** get_textures_to_load(const TexturesToLoad* texture_to_load)
{
    return texture_hashmap_get_keys_list(texture_to_load);
}

uint16_t get_texture_to_load_indice(const TexturesToLoad* texture_to_load, const char* texture_name)
{
    int16_t indices = texture_hashmap_get_value(texture_to_load, texture_name);

    if(indices == -1)
    {
        return 0;
    }

    return (uint16_t) indices;
}

void destroy_textures_to_load(TexturesToLoad* texture_to_load)
{
    texture_hashmap_free(&texture_to_load);
}
struct stat path_stat;

StringArray* create_string_array(void)
{
    StringArray* string_array = calloc(1, PATH_ARRAY_MAX_SIZE);
    if(string_array == NULL)
    {
        goto ERROR;
    }

    string_array->strings = calloc(PATH_ARRAY_MAX_SIZE, sizeof(*(string_array->strings)));
    if(string_array->strings == NULL)
    {
        goto ERROR;
    }

    *string_array->strings = calloc(PATH_MAX_SIZE, sizeof(**(string_array->strings)));
    if(*string_array->strings == NULL)
    {
        goto ERROR;
    }

    return string_array;

ERROR:
    perror("create_string_array");
    if(string_array != NULL)
    {
        free(string_array->strings);
        free(string_array);
    }
    return NULL;
}

void destroy_string_array(StringArray* string_array)
{
    free(string_array);
}

void add_path(StringArray* path_array, const char* path)
{
    size_t path_size = strnlen(path, PATH_MAX_SIZE);
    if(path_size - 1 >= PATH_MAX_SIZE)
    {
        fprintf(stderr, "Path size exceeds PATH_MAX_SIZE (%i)!\n", PATH_MAX_SIZE);
        goto ERROR;
    }

    struct stat path_stat;
    stat(path, &path_stat);
    if(!S_ISDIR(path_stat.st_mode))
    {
        fprintf(stderr, "%s is not a directory!\n", path);
        goto ERROR;
    }

    memcpy(path_array->strings[path_array->size], path, path_size);
    if(path_array->strings[path_array->size][path_size - 1] != '/')
    {
        path_array->strings[path_array->size][path_size]     = '/';
        path_array->strings[path_array->size][path_size + 1] = '\0';
    }
    path_array->size++;

ERROR:
    return;
}

void load_texture(PTextureList* texture_list, const char* texture_name, StringArray* paths, PCommands* commands, PDevice* device)
{
    if(strcmp(texture_name, "default") == 0)
    {
        add_texture(texture_list, "default", commands, device);
        return;
    }

    struct stat path_stat;
    int stat_result;

    char* texture_path = calloc(PATH_MAX_SIZE, sizeof(*texture_path));
    if(texture_path == NULL)
    {
        goto ERROR;
    }

    for(size_t i = 0; i < paths->size; i++)
    {
        strncat(texture_path, paths->strings[i], PATH_MAX_SIZE - 1);
        strncat(texture_path, texture_name, NAME_MAX_SIZE + 1);
        stat_result = stat(texture_path, &path_stat);
        if(stat_result == 0 && S_ISREG(path_stat.st_mode))
        {
            break;
        }
        texture_path[0] = '\0';
    }

    add_texture(texture_list, texture_path, commands, device);

    free(texture_path);

    return;

ERROR:
    perror("load_texture");
    return;
}

void load_all_textures(PTextureList* texture_list, TexturesToLoad* textures_to_load, StringArray* paths, PCommands* commands, PDevice* device)
{
    char** textures = get_textures_to_load(textures_to_load);
    for(int i = 0; i < textures_to_load_number(textures_to_load); i++)
    {
        load_texture(texture_list, textures[i], paths, commands, device);
    }
}
