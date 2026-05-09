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

#include "deletion.h"
#include "structs.h"
#include "internal.h"
#include "log_internal.h"

#include <stdlib.h>

#define PIGMENT_DELETION_QUEUE_INITIAL_CAPACITY 32

typedef enum PWaitKind {
    P_WAIT_TIMELINE = 0,
    P_WAIT_FENCE    = 1,
} PWaitKind;

typedef struct PWaitTarget {
    PWaitKind kind;

    union {
        struct {
            VkSemaphore semaphore;
            uint64_t value;
        } timeline;

        VkFence fence;
    };
} PWaitTarget;

typedef struct PDeletionEntry {
    PDestroyFn destroy_fn;
    void* resource;
    PWaitTarget* targets;
    uint32_t target_count;
} PDeletionEntry;

struct PDeletionQueue {
    pigment_rwlock_t lock;
    PDeletionEntry* entries;
    uint32_t count;
    uint32_t capacity;

    VkSemaphore submit_timeline;
    uint64_t submit_next_value;
};

static PResult deletion_queue_push(PDeletionQueue* queue, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count);
static PResult collect_active_targets(Pigment* pigment, PWaitTarget** out_targets, uint32_t* out_count);
static void enqueue_or_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count);
static PBool target_signaled(VkDevice device, const PWaitTarget* target);
static VkSemaphore create_submit_timeline(Pigment* pigment);

PDeletionQueue* create_deletion_queue(Pigment* pigment)
{
    PDeletionQueue* queue = calloc(1, sizeof(*queue));
    if(queue == NULL)
    {
        return NULL;
    }
    (void) pigment_rwlock_init(&queue->lock);

    queue->submit_timeline = create_submit_timeline(pigment);
    if(queue->submit_timeline == VK_NULL_HANDLE)
    {
        pigment_rwlock_destroy(&queue->lock);
        free(queue);
        return NULL;
    }
    return queue;
}

void destroy_deletion_queue(Pigment* pigment, PDeletionQueue* queue)
{
    if(queue == NULL)
    {
        return;
    }

    for(uint32_t i = 0; i < queue->count; i++)
    {
        PDeletionEntry* entry = &queue->entries[i];
        entry->destroy_fn(pigment, entry->resource);
        free(entry->targets);
    }

    if(queue->submit_timeline != VK_NULL_HANDLE && pigment != NULL && pigment->device != NULL)
    {
        vkDestroySemaphore(pigment->device->logical_device, queue->submit_timeline, NULL);
    }

    free(queue->entries);
    pigment_rwlock_destroy(&queue->lock);
    free(queue);
}

VkSemaphore deletion_queue_submit_timeline(Pigment* pigment)
{
    if(pigment == NULL || pigment->deletions == NULL)
    {
        return VK_NULL_HANDLE;
    }

    return pigment->deletions->submit_timeline;
}

uint64_t deletion_queue_acquire_submit_value(Pigment* pigment)
{
    if(pigment == NULL || pigment->deletions == NULL)
    {
        return 0;
    }

    PDeletionQueue* queue = pigment->deletions;
    pigment_rwlock_wrlock(&queue->lock);
    uint64_t value = ++queue->submit_next_value;
    pigment_rwlock_wrunlock(&queue->lock);
    return value;
}

void pigment_defer_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource)
{
    if(pigment == NULL || destroy_fn == NULL || resource == NULL)
    {
        return;
    }

    if(pigment->deletions == NULL)
    {
        destroy_fn(pigment, resource);
        return;
    }

    PWaitTarget* targets = NULL;
    uint32_t count       = 0;
    if(collect_active_targets(pigment, &targets, &count) != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to collect active wait targets for deferred destroy. Falling back to immediate destroy.");
        destroy_fn(pigment, resource);
        return;
    }

    enqueue_or_destroy(pigment, destroy_fn, resource, targets, count);
    free(targets);
}

void pigment_vk_fence_defer_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource, VkFence fence)
{
    if(pigment == NULL || destroy_fn == NULL || resource == NULL)
    {
        return;
    }

    if(pigment->deletions == NULL)
    {
        destroy_fn(pigment, resource);
        return;
    }

    if(fence != VK_NULL_HANDLE)
    {
        PWaitTarget target = {
            .kind  = P_WAIT_FENCE,
            .fence = fence,
        };
        enqueue_or_destroy(pigment, destroy_fn, resource, &target, 1);
    }
    else
    {
        enqueue_or_destroy(pigment, destroy_fn, resource, NULL, 0);
    }
}

void pigment_drain_pending(Pigment* pigment)
{
    drain_deletion_queue(pigment);
}

void drain_deletion_queue(Pigment* pigment)
{
    if(pigment == NULL || pigment->deletions == NULL)
    {
        return;
    }

    PDeletionQueue* queue = pigment->deletions;
    VkDevice device       = pigment->device->logical_device;

    pigment_rwlock_wrlock(&queue->lock);

    uint32_t write = 0;
    for(uint32_t read = 0; read < queue->count; read++)
    {
        PDeletionEntry* entry = &queue->entries[read];

        PBool ready = P_TRUE;

        for(uint32_t i = 0; i < entry->target_count; i++)
        {
            if(!target_signaled(device, &entry->targets[i]))
            {
                ready = P_FALSE;
                break;
            }
        }

        if(ready)
        {
            entry->destroy_fn(pigment, entry->resource);
            free(entry->targets);
        }
        else
        {
            queue->entries[write++] = *entry;
        }
    }
    queue->count = write;

    pigment_rwlock_wrunlock(&queue->lock);
}

static void enqueue_or_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count)
{
    pigment_rwlock_wrlock(&pigment->deletions->lock);
    PResult result = deletion_queue_push(pigment->deletions, destroy_fn, resource, targets, target_count);
    pigment_rwlock_wrunlock(&pigment->deletions->lock);

    if(result != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to enqueue deferred destroy. Falling back to immediate destroy.");
        destroy_fn(pigment, resource);
    }
}

static PResult deletion_queue_push(PDeletionQueue* queue, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count)
{
    PWaitTarget* targets_copy = NULL;
    if(target_count > 0)
    {
        targets_copy = malloc(target_count * sizeof(*targets_copy));
        if(targets_copy == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        memcpy(targets_copy, targets, target_count * sizeof(*targets_copy));
    }

    if(queue->count >= queue->capacity)
    {
        uint32_t new_capacity   = (queue->capacity == 0) ? PIGMENT_DELETION_QUEUE_INITIAL_CAPACITY : queue->capacity * 2;
        PDeletionEntry* new_ptr = realloc(queue->entries, new_capacity * sizeof(*new_ptr));
        if(new_ptr == NULL)
        {
            free(targets_copy);
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        queue->entries  = new_ptr;
        queue->capacity = new_capacity;
    }

    queue->entries[queue->count++] = (PDeletionEntry) {
        .destroy_fn   = destroy_fn,
        .resource     = resource,
        .targets      = targets_copy,
        .target_count = target_count,
    };
    return PIGMENT_SUCCESS;
}

static PResult collect_active_targets(Pigment* pigment, PWaitTarget** out_targets, uint32_t* out_count)
{
    *out_targets = NULL;
    *out_count   = 0;

    PRendererList* list       = pigment->renderers;
    PDeletionQueue* deletions = pigment->deletions;
    uint32_t renderer_count   = (list != NULL) ? list->count : 0;
    PBool has_submit      = (deletions != NULL && deletions->submit_timeline != VK_NULL_HANDLE && deletions->submit_next_value > 0);

    uint32_t target_count = has_submit ? 1 : 0;
    for(uint32_t i = 0; i < renderer_count; i++)
    {
        PSync* sync = list->renderers[i]->sync;
        if(sync != NULL && sync->timeline != VK_NULL_HANDLE && sync->active_target_value > 0)
        {
            target_count++;
        }
    }

    if(target_count == 0)
    {
        return PIGMENT_SUCCESS;
    }

    PWaitTarget* targets = malloc(target_count * sizeof(*targets));
    if(targets == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    uint32_t fill = 0;
    for(uint32_t i = 0; i < renderer_count; i++)
    {
        PSync* sync = list->renderers[i]->sync;
        if(sync != NULL && sync->timeline != VK_NULL_HANDLE && sync->active_target_value > 0)
        {
            targets[fill++] = (PWaitTarget) {
                .kind     = P_WAIT_TIMELINE,
                .timeline = {.semaphore = sync->timeline, .value = sync->active_target_value},
            };
        }
    }

    if(has_submit)
    {
        targets[fill++] = (PWaitTarget) {
            .kind     = P_WAIT_TIMELINE,
            .timeline = {.semaphore = deletions->submit_timeline, .value = deletions->submit_next_value},
        };
    }

    *out_targets = targets;
    *out_count   = target_count;
    return PIGMENT_SUCCESS;
}

static VkSemaphore create_submit_timeline(Pigment* pigment)
{
    VkSemaphoreTypeCreateInfo type_info = {
        .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue  = 0,
    };

    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &type_info,
    };

    VkSemaphore semaphore;
    VkResult result = vkCreateSemaphore(pigment->device->logical_device, &create_info, NULL, &semaphore);
    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to create submit timeline semaphore (result: %d)", result);
        return VK_NULL_HANDLE;
    }
    return semaphore;
}

static PBool target_signaled(VkDevice device, const PWaitTarget* target)
{
    if(target->kind == P_WAIT_FENCE)
    {
        return vkGetFenceStatus(device, target->fence) == VK_SUCCESS;
    }

    uint64_t current_value = 0;
    if(vkGetSemaphoreCounterValue(device, target->timeline.semaphore, &current_value) != VK_SUCCESS)
    {
        return P_FALSE;
    }
    return current_value >= target->timeline.value;
}
