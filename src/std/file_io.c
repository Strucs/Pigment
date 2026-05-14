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

#include "file_io.h"
#include "internal.h"

#include <stdio.h>

static unsigned char* stdio_read_file(void* user_data, const char* path, uint64_t* out_size)
{
    Pigment* pigment = (Pigment*) user_data;

    if(path == NULL || out_size == NULL)
    {
        return NULL;
    }

    FILE* fd = fopen(path, "rb");
    if(fd == NULL)
    {
        return NULL;
    }

    fseek(fd, 0l, SEEK_END);
    long size = ftell(fd);
    rewind(fd);

    if(size < 0)
    {
        fclose(fd);
        return NULL;
    }

    unsigned char* data = (unsigned char*) P_ALLOC_OBJECT(pigment, (uint64_t) size, _Alignof(unsigned char));
    if(data == NULL)
    {
        fclose(fd);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t) size, fd);
    fclose(fd);

    if(read != (size_t) size)
    {
        P_FREE(pigment, data);
        return NULL;
    }

    *out_size = (uint64_t) size;
    return data;
}

static int stdio_write_file(void* user_data, const char* path, const void* data, uint64_t size)
{
    (void) user_data;

    if(path == NULL || data == NULL)
    {
        return -1;
    }

    FILE* fd = fopen(path, "wb");
    if(fd == NULL)
    {
        return -1;
    }

    size_t written = fwrite(data, 1, (size_t) size, fd);
    fclose(fd);

    return (written == (size_t) size) ? 0 : -1;
}

static void stdio_free_file(void* user_data, unsigned char* data)
{
    Pigment* pigment = (Pigment*) user_data;
    P_FREE(pigment, data);
}

IOCallbacks pigment_std_default_file_io(Pigment* pigment)
{
    return (IOCallbacks) {
        .read_file  = stdio_read_file,
        .write_file = stdio_write_file,
        .free_file  = stdio_free_file,
        .user_data  = pigment,
    };
}
