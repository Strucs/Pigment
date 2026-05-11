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

#ifndef PIGMENT_STD_UPLOAD_H
#define PIGMENT_STD_UPLOAD_H

#include "defines.h"

typedef struct PBufferUploadDesc {
    const void* data;
    uint64_t size;
    uint64_t offset;
} PBufferUploadDesc;

PResult pigment_std_buffer_upload(Pigment* pigment, PCommandPool* pool, PBuffer* dst, const PBufferUploadDesc* desc, PSubmitHandle* out_handle);

#endif
