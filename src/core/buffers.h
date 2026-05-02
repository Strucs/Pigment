/**
 * Copyright 2025-2026 Angel-Leduc TA
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

#ifndef BUFFERS_H
#define BUFFERS_H

#include "defines.h"

typedef enum PBufferUsage {
    P_BUFFER_USAGE_VERTEX         = 1 << 0,
    P_BUFFER_USAGE_INDEX          = 1 << 1,
    P_BUFFER_USAGE_STORAGE        = 1 << 2,
    P_BUFFER_USAGE_UNIFORM        = 1 << 3,
    P_BUFFER_USAGE_SHADER_ADDRESS = 1 << 4,
    P_BUFFER_USAGE_TRANSFER_SRC   = 1 << 5,
    P_BUFFER_USAGE_TRANSFER_DST   = 1 << 6,
} PBufferUsage;

typedef enum PMemoryType {
    P_MEMORY_GPU_ONLY     = 0,
    P_MEMORY_HOST_VISIBLE = 1,
} PMemoryType;

typedef enum PIndexType {
    P_INDEX_TYPE_UINT16 = 0,
    P_INDEX_TYPE_UINT32 = 1,
} PIndexType;

typedef struct PBufferDesc {
    uint64_t size;
    PBufferUsage usage;
    PMemoryType memory;
} PBufferDesc;

PBuffer* pigment_create_buffer(Pigment* pigment, const PBufferDesc* desc);
void pigment_destroy_buffer(Pigment* pigment, PBuffer* buffer);

void* pigment_buffer_mapped(PBuffer* buffer);
uint64_t pigment_buffer_address(PBuffer* buffer);

void pigment_buffer_upload(Pigment* pigment, PBuffer* dst, const void* data, uint64_t size, uint64_t offset);

#endif
