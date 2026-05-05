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

#include "resize.h"
#include "internal.h"

#include <stdlib.h>

#define PIGMENT_RESIZE_INITIAL_CAPACITY 4

PResizeCallbackList* create_resize_callback_list(void)
{
    PResizeCallbackList* list = calloc(1, sizeof(*list));
    if(list == NULL)
    {
        return NULL;
    }

    (void) pigment_rwlock_init(&list->lock);
    atomic_store(&list->next_handle, 1);
    return list;
}

void destroy_resize_callback_list(PResizeCallbackList* list)
{
    if(list == NULL)
    {
        return;
    }

    pigment_rwlock_destroy(&list->lock);
    free(list->callbacks);
    free(list);
}

uint32_t pigment_register_swapchain_resize(Pigment* pigment, PSwapchainResizeFn func, void* user_data)
{
    if(pigment == NULL || pigment->resize_callbacks == NULL || func == NULL)
    {
        return UINT32_MAX;
    }

    PResizeCallbackList* list = pigment->resize_callbacks;
    uint32_t handle           = atomic_fetch_add(&list->next_handle, 1);

    pigment_rwlock_wrlock(&list->lock);

    uint32_t reuse_index = UINT32_MAX;
    for(uint32_t i = 0; i < list->count; i++)
    {
        if(!list->callbacks[i].alive)
        {
            reuse_index = i;
            break;
        }
    }

    if(reuse_index == UINT32_MAX)
    {
        if(list->count >= list->capacity)
        {
            uint32_t new_capacity      = (list->capacity == 0) ? PIGMENT_RESIZE_INITIAL_CAPACITY : list->capacity * 2;
            PResizeCallback* new_array = realloc(list->callbacks, new_capacity * sizeof(*new_array));
            if(new_array == NULL)
            {
                pigment_rwlock_wrunlock(&list->lock);
                return UINT32_MAX;
            }
            list->callbacks = new_array;
            list->capacity  = new_capacity;
        }
        reuse_index = list->count++;
    }

    list->callbacks[reuse_index] = (PResizeCallback) {
        .func      = func,
        .user_data = user_data,
        .handle    = handle,
        .alive     = true,
    };

    pigment_rwlock_wrunlock(&list->lock);
    return handle;
}

void pigment_unregister_swapchain_resize(Pigment* pigment, uint32_t handle)
{
    if(pigment == NULL || pigment->resize_callbacks == NULL || handle == 0 || handle == UINT32_MAX)
    {
        return;
    }

    PResizeCallbackList* list = pigment->resize_callbacks;

    pigment_rwlock_wrlock(&list->lock);
    for(uint32_t i = 0; i < list->count; i++)
    {
        if(list->callbacks[i].handle == handle)
        {
            list->callbacks[i].alive = false;
            list->callbacks[i].func  = NULL;
            break;
        }
    }
    pigment_rwlock_wrunlock(&list->lock);
}

void dispatch_swapchain_resize(Pigment* pigment, const PSwapchainResizeEvent* event)
{
    if(pigment == NULL || pigment->resize_callbacks == NULL || event == NULL)
    {
        return;
    }

    PResizeCallbackList* list = pigment->resize_callbacks;

    pigment_rwlock_rdlock(&list->lock);
    for(uint32_t i = 0; i < list->count; i++)
    {
        PResizeCallback callback = list->callbacks[i];
        if(callback.alive && callback.func != NULL)
        {
            callback.func(pigment, event, callback.user_data);
        }
    }
    pigment_rwlock_rdunlock(&list->lock);
}
