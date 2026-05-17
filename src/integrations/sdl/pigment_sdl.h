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

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"
#include "log.h"

#include "pigment/std/file_io.h"

#include <SDL3/SDL.h>

PWindowHandles pigment_sdl_get_window_handles(SDL_Window* window);

void pigment_sdl_log_callback(
    PigmentLogSeverity severity,
    PigmentLogType type,
    const PigmentLogRecord* record,
    void* user_data
);

IOCallbacks pigment_sdl_default_file_io(void);


#ifdef __cplusplus
}
#endif
#endif
