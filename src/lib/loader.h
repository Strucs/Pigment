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

#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
#include "defines.h"

typedef struct _texture_hashmap TexturesToLoad;

typedef struct StringArray_T StringArray;

TexturesToLoad* init_textures_to_load(void);

void add_texture_to_load(TexturesToLoad* textures_to_load, const char* texture_name);
void add_textures_dir_to_load(TexturesToLoad* textures_to_load, const char* texture_dir);
int textures_to_load_number(const TexturesToLoad* textures_to_load);
char** get_textures_to_load(const TexturesToLoad* textures_to_load);
uint16_t get_texture_to_load_indice(const TexturesToLoad* textures_to_load, const char* texture_name);

void destroy_textures_to_load(TexturesToLoad* textures_to_load);

StringArray* create_string_array(void);
void destroy_string_array(StringArray* string_array);
void add_path(StringArray* path_array, const char* path);
void load_texture(PTextureList* texture_list, const char* texture_name, StringArray* paths, PCommands* commands, PDevice* device);
void load_all_textures(PTextureList* texture_list, TexturesToLoad* textures_to_load, StringArray* paths, PCommands* commands, PDevice* device);

#endif
