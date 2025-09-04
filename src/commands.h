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

#ifndef COMMANDS_H
#define COMMANDS_H

#include "defines.h"

PCommands* create_commands(PDevice* device, PSurface* surface);
void update_commands(PCommands* commands, PDevice* device, const uint32_t command_buffers_numbers);
void destroy_commands(PCommands* commands, PDevice* device, const uint32_t command_buffers_numbers);

#endif
