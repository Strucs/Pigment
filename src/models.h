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

#ifndef MODELS_H
#define MODELS_H

#include "defines.h"
#include "lib/hashmap.h"

PModel* create_model(void);
void destroy_model(PModel* model);
void load_model_multi_textures(const char* filepath, float x_pos, float y_pos, float z_pos, float scale, TextureHashMap* textures_to_load, PModel* model);
void load_model(const char* filepath, float x_pos, float y_pos, float z_pos, float scale, uint16_t texture_index, PModel* model);
void load_cube(float size, float x_pos, float y_pos, float z_pos, uint16_t texture_index, PModel* model);

#endif
