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

#include "swapchain_event.h"

#include "internal.h"

#define PIGMENT_SWAPCHAIN_CALLBACK_INITIAL_CAPACITY 4

PSwapchainCallbackList* create_swapchain_callback_list(Pigment* pigment)
{
    PSwapchainCallbackList* list = P_NEW_FOR_INSTANCE(pigment, list);
    if(list == NULL)
    {
        return NULL;
    }

    (void) pigment_rwlock_init(&list->lock);
    atomic_store(&list->next_handle, 1);
    return list;
}

void destroy_swapchain_callback_list(Pigment* pigment, PSwapchainCallbackList* list)
{
    if(list == NULL)
    {
        return;
    }

    pigment_rwlock_destroy(&list->lock);
    P_FREE(pigment, list->callbacks);
    P_FREE(pigment, list);
}

uint32_t pigment_register_swapchain_recreate(Pigment* pigment, PWindowRenderer* renderer, PSwapchainRecreateFn func, void* user_data)
{
    if(pigment == NULL || pigment->swapchain_callbacks == NULL || func == NULL)
    {
        return UINT32_MAX;
    }

    PSwapchainCallbackList* list = pigment->swapchain_callbacks;
    uint32_t handle              = atomic_fetch_add(&list->next_handle, 1);

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
        if(P_ARRAY_RESERVE_INSTANCE(pigment, list->callbacks, list->count, list->capacity, 1, PIGMENT_SWAPCHAIN_CALLBACK_INITIAL_CAPACITY) != PIGMENT_SUCCESS)
        {
            pigment_rwlock_wrunlock(&list->lock);
            return UINT32_MAX;
        }
        reuse_index = list->count++;
    }

    list->callbacks[reuse_index] = (PSwapchainCallback) {
        .user_data = user_data,
        .func      = func,
        .renderer  = renderer,
        .handle    = handle,
        .alive     = P_TRUE,
    };

    pigment_rwlock_wrunlock(&list->lock);
    return handle;
}

void pigment_unregister_swapchain_recreate(Pigment* pigment, uint32_t handle)
{
    if(pigment == NULL || pigment->swapchain_callbacks == NULL || handle == 0 || handle == UINT32_MAX)
    {
        return;
    }

    PSwapchainCallbackList* list = pigment->swapchain_callbacks;

    pigment_rwlock_wrlock(&list->lock);
    for(uint32_t i = 0; i < list->count; i++)
    {
        if(list->callbacks[i].handle == handle)
        {
            list->callbacks[i].alive    = P_FALSE;
            list->callbacks[i].func     = NULL;
            list->callbacks[i].renderer = NULL;
            break;
        }
    }
    pigment_rwlock_wrunlock(&list->lock);
}

void dispatch_swapchain_recreate(Pigment* pigment, const PSwapchainRecreateEvent* event)
{
    if(pigment == NULL || pigment->swapchain_callbacks == NULL || event == NULL)
    {
        return;
    }

    PSwapchainCallbackList* list = pigment->swapchain_callbacks;

    pigment_rwlock_rdlock(&list->lock);

    P_STACK_OR_HEAP(PSwapchainCallback, callbacks_to_call, list->count);
    if(callbacks_to_call == NULL)
    {
        pigment_rwlock_rdunlock(&list->lock);
        return;
    }

    uint32_t count = 0;
    for(uint32_t i = 0; i < list->count; i++)
    {
        const PSwapchainCallback* callback = &list->callbacks[i];
        if(!callback->alive)
        {
            continue;
        }
        if(callback->renderer != NULL && callback->renderer != event->renderer)
        {
            continue;
        }
        callbacks_to_call[count++] = *callback;
    }
    pigment_rwlock_rdunlock(&list->lock);

    for(uint32_t i = 0; i < count; i++)
    {
        callbacks_to_call[i].func(pigment, event, callbacks_to_call[i].user_data);
    }

    P_STACK_OR_HEAP_FREE(pigment, callbacks_to_call);
}
