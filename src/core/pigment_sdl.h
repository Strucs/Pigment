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

#ifndef PIGMENT_SDL_H
#define PIGMENT_SDL_H

#include "defines.h"
#include <SDL3/SDL.h>

void pigment_handle_sdl_event(Pigment* pigment, const SDL_Event* event);
SDL_Window* pigment_get_sdl_window(Pigment* pigment, uint32_t window_index);

#endif
