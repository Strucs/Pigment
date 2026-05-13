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

#define PIGMENT_DELETION_CHUNK_NODES 64

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

typedef struct PDeletionNode {
    PDeletionEntry entry;
    struct PDeletionNode* next;
} PDeletionNode;

typedef struct PDeletionChunk {
    PDeletionNode nodes[PIGMENT_DELETION_CHUNK_NODES];
    struct PDeletionChunk* next;
} PDeletionChunk;

typedef struct PDeletionTsd {
    Pigment* pigment;
    PDeletionNode* private_free;
} PDeletionTsd;

struct PDeletionQueue {
    _Atomic(PDeletionNode*) head;
    _Atomic(PDeletionNode*) shared_free;
    _Atomic(PDeletionChunk*) chunks;

    pigment_tsd_t tsd;
};

static PResult collect_active_targets(Pigment* pigment, PWaitTarget** out_targets, uint32_t* out_count);
static void enqueue_or_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count);
static PResult push_node(Pigment* pigment, PDeletionQueue* queue, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count);
static void prepend_chain(PDeletionQueue* queue, PDeletionNode* chain_head, PDeletionNode* chain_tail);
static void prepend_shared_free(PDeletionQueue* queue, PDeletionNode* chain_head, PDeletionNode* chain_tail);
static PDeletionNode* node_acquire(Pigment* pigment, PDeletionQueue* queue);
static void tsd_destructor(void* ptr);
static PBool target_signaled(VkDevice device, const PWaitTarget* target);

PDeletionQueue* create_deletion_queue(Pigment* pigment)
{
    PDeletionQueue* queue = P_NEW_FOR_INSTANCE(pigment, queue);
    if(queue == NULL)
    {
        return NULL;
    }

    if(pigment_tsd_init(&queue->tsd, tsd_destructor) != 0)
    {
        P_FREE(pigment, queue);
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

    PDeletionNode* node = atomic_exchange_explicit(&queue->head, NULL, memory_order_acquire);

    PDeletionNode* fifo = NULL;
    while(node != NULL)
    {
        PDeletionNode* next = node->next;
        node->next          = fifo;
        fifo                = node;
        node                = next;
    }
    node = fifo;

    while(node != NULL)
    {
        PDeletionNode* next = node->next;
        node->entry.destroy_fn(pigment, node->entry.resource);
        P_FREE(pigment, node->entry.targets);
        node = next;
    }

    PDeletionChunk* chunk = atomic_load_explicit(&queue->chunks, memory_order_relaxed);
    while(chunk != NULL)
    {
        PDeletionChunk* next = chunk->next;
        P_FREE(pigment, chunk);
        chunk = next;
    }

    pigment_tsd_destroy(queue->tsd);
    P_FREE(pigment, queue);
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
    P_FREE(pigment, targets);
}

void pigment_defer_destroy_tracked(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PResourceTracker* tracker)
{
    if(pigment == NULL || destroy_fn == NULL || resource == NULL)
    {
        return;
    }

    if(pigment->deletions == NULL || tracker == NULL || tracker->last_used == NULL || pigment->device == NULL || pigment->device->queues == NULL)
    {
        pigment_defer_destroy(pigment, destroy_fn, resource);
        return;
    }

    PDevice* device      = pigment->device;
    uint32_t queue_count = device->queue_count;
    PWaitTarget* targets = P_NEW_ARRAY_FOR_COMMAND(pigment, targets, queue_count);
    if(targets == NULL)
    {
        pigment_defer_destroy(pigment, destroy_fn, resource);
        return;
    }

    uint32_t target_count = 0;
    for(uint32_t i = 0; i < queue_count; i++)
    {
        PDeviceQueue* queue = &device->queues[i];
        if(queue->timeline == VK_NULL_HANDLE)
        {
            continue;
        }
        uint64_t value = atomic_load_explicit(&tracker->last_used[queue->slot], memory_order_relaxed);
        if(value == 0)
        {
            continue;
        }
        targets[target_count++] = (PWaitTarget) {
            .kind     = P_WAIT_TIMELINE,
            .timeline = {.semaphore = queue->timeline, .value = value},
        };
    }

    if(target_count == 0)
    {
        destroy_fn(pigment, resource);
    }
    else
    {
        enqueue_or_destroy(pigment, destroy_fn, resource, targets, target_count);
    }
    P_FREE(pigment, targets);
}

void defer_destroy_renderer(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PResourceTracker* tracker, VkFence present_fence)
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

    uint32_t queue_count = (pigment->device != NULL) ? pigment->device->queue_count : 0;
    uint32_t cap         = queue_count + 1;
    PWaitTarget* targets = P_NEW_ARRAY_FOR_COMMAND(pigment, targets, cap);
    if(targets == NULL)
    {
        destroy_fn(pigment, resource);
        return;
    }

    uint32_t target_count = 0;

    if(tracker != NULL && tracker->last_used != NULL && pigment->device != NULL && pigment->device->queues != NULL)
    {
        PDevice* device = pigment->device;
        for(uint32_t i = 0; i < device->queue_count; i++)
        {
            PDeviceQueue* queue = &device->queues[i];
            if(queue->timeline == VK_NULL_HANDLE)
            {
                continue;
            }

            uint64_t value = atomic_load_explicit(&tracker->last_used[queue->slot], memory_order_relaxed);
            if(value == 0)
            {
                continue;
            }

            targets[target_count++] = (PWaitTarget) {
                .kind     = P_WAIT_TIMELINE,
                .timeline = {.semaphore = queue->timeline, .value = value},
            };
        }
    }

    if(present_fence != VK_NULL_HANDLE)
    {
        targets[target_count++] = (PWaitTarget) {
            .kind  = P_WAIT_FENCE,
            .fence = present_fence,
        };
    }

    if(target_count == 0)
    {
        destroy_fn(pigment, resource);
    }
    else
    {
        enqueue_or_destroy(pigment, destroy_fn, resource, targets, target_count);
    }

    P_FREE(pigment, targets);
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

    PDeletionNode* node = atomic_exchange_explicit(&queue->head, NULL, memory_order_acquire);

    PDeletionNode* fifo = NULL;
    while(node != NULL)
    {
        PDeletionNode* next = node->next;
        node->next          = fifo;
        fifo                = node;
        node                = next;
    }
    node = fifo;

    PDeletionNode* not_ready_head = NULL;
    PDeletionNode* not_ready_tail = NULL;
    PDeletionNode* ready_head     = NULL;
    PDeletionNode* ready_tail     = NULL;

    // Iterate through all pending deletions and check if their targets are signaled.
    // If so, we can destroy them immediately. Otherwise, we need to keep them in the queue.
    while(node != NULL)
    {
        PDeletionNode* next = node->next;

        // Check if all targets for this node are signaled. If so, we can destroy it immediately. Otherwise, we need to keep it in the queue.
        PBool ready = P_TRUE;
        for(uint32_t i = 0; i < node->entry.target_count; i++)
        {
            if(!target_signaled(device, &node->entry.targets[i]))
            {
                ready = P_FALSE;
                break;
            }
        }

        if(ready)
        {
            node->entry.destroy_fn(pigment, node->entry.resource);
            P_FREE(pigment, node->entry.targets);
            node->next = ready_head;
            ready_head = node;
            if(ready_tail == NULL)
            {
                ready_tail = node;
            }
        }
        else
        {
            node->next     = not_ready_head;
            not_ready_head = node;
            if(not_ready_tail == NULL)
            {
                not_ready_tail = node;
            }
        }

        node = next;
    }

    if(not_ready_head != NULL)
    {
        prepend_chain(queue, not_ready_head, not_ready_tail);
    }

    if(ready_head != NULL)
    {
        prepend_shared_free(queue, ready_head, ready_tail);
    }
}

static void enqueue_or_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count)
{
    PResult result = push_node(pigment, pigment->deletions, destroy_fn, resource, targets, target_count);
    if(result != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Failed to enqueue deferred destroy. Falling back to immediate destroy.");
        destroy_fn(pigment, resource);
    }
}

static PResult push_node(Pigment* pigment, PDeletionQueue* queue, PDestroyFn destroy_fn, void* resource, const PWaitTarget* targets, uint32_t target_count)
{
    PWaitTarget* targets_copy = NULL;
    if(target_count > 0)
    {
        targets_copy = P_NEW_ARRAY_FOR_OBJECT(pigment, targets_copy, target_count);
        if(targets_copy == NULL)
        {
            return PIGMENT_ERROR_OUT_OF_MEMORY;
        }
        memcpy(targets_copy, targets, target_count * sizeof(*targets_copy));
    }

    PDeletionNode* node = node_acquire(pigment, queue);
    if(node == NULL)
    {
        P_FREE(pigment, targets_copy);
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    node->entry = (PDeletionEntry) {
        .destroy_fn   = destroy_fn,
        .resource     = resource,
        .targets      = targets_copy,
        .target_count = target_count,
    };

    PDeletionNode* old_head = atomic_load_explicit(&queue->head, memory_order_relaxed);
    do
    {
        node->next = old_head;
    }
    while(!atomic_compare_exchange_weak_explicit(&queue->head, &old_head, node, memory_order_release, memory_order_relaxed));

    return PIGMENT_SUCCESS;
}

static void prepend_chain(PDeletionQueue* queue, PDeletionNode* chain_head, PDeletionNode* chain_tail)
{
    PDeletionNode* old_head = atomic_load_explicit(&queue->head, memory_order_relaxed);
    do
    {
        chain_tail->next = old_head;
    }
    while(!atomic_compare_exchange_weak_explicit(&queue->head, &old_head, chain_head, memory_order_release, memory_order_relaxed));
}

static void prepend_shared_free(PDeletionQueue* queue, PDeletionNode* chain_head, PDeletionNode* chain_tail)
{
    PDeletionNode* old_head = atomic_load_explicit(&queue->shared_free, memory_order_relaxed);
    do
    {
        chain_tail->next = old_head;
    }
    while(!atomic_compare_exchange_weak_explicit(&queue->shared_free, &old_head, chain_head, memory_order_release, memory_order_relaxed));
}

static PDeletionNode* node_acquire(Pigment* pigment, PDeletionQueue* queue)
{
    PDeletionTsd* tsd = pigment_tsd_get(queue->tsd);
    if(tsd == NULL)
    {
        tsd = P_NEW_FOR_INSTANCE(pigment, tsd);
        if(tsd == NULL)
        {
            return NULL;
        }
        tsd->pigment = pigment;
        if(pigment_tsd_set(queue->tsd, tsd) != 0)
        {
            P_FREE(pigment, tsd);
            return NULL;
        }
    }

    // Try to acquire a node from the thread-local private free list first.
    if(tsd->private_free != NULL)
    {
        PDeletionNode* node = tsd->private_free;
        tsd->private_free   = node->next;
        return node;
    }

    // Try to acquire a node from the shared free list and cache it in the thread-local private free list.
    PDeletionNode* shared_free = atomic_exchange_explicit(&queue->shared_free, NULL, memory_order_acquire);
    if(shared_free != NULL)
    {
        tsd->private_free = shared_free->next;
        return shared_free;
    }

    // If the shared free list is empty, Allocate a new chunk of nodes and add them to the shared free list, then acquire one for the caller.
    PDeletionChunk* chunk = P_NEW_FOR_INSTANCE(pigment, chunk);
    if(chunk == NULL)
    {
        return NULL;
    }

    PDeletionChunk* old_chunks = atomic_load_explicit(&queue->chunks, memory_order_relaxed);
    do
    {
        chunk->next = old_chunks;
    }
    while(!atomic_compare_exchange_weak_explicit(&queue->chunks, &old_chunks, chunk, memory_order_release, memory_order_relaxed));

    for(uint32_t i = 1; i < PIGMENT_DELETION_CHUNK_NODES - 1; i++)
    {
        chunk->nodes[i].next = &chunk->nodes[i + 1];
    }
    chunk->nodes[PIGMENT_DELETION_CHUNK_NODES - 1].next = NULL;

    tsd->private_free = &chunk->nodes[1];

    return &chunk->nodes[0];
}

static void tsd_destructor(void* ptr)
{
    if(ptr == NULL)
    {
        return;
    }
    PDeletionTsd* tsd = (PDeletionTsd*) ptr;
    P_FREE(tsd->pigment, tsd);
}

static PResult collect_active_targets(Pigment* pigment, PWaitTarget** out_targets, uint32_t* out_count)
{
    *out_targets = NULL;
    *out_count   = 0;

    PDevice* device = pigment->device;
    if(device == NULL || device->queues == NULL || device->queue_count == 0)
    {
        return PIGMENT_SUCCESS;
    }

    uint32_t active = 0;
    for(uint32_t i = 0; i < device->queue_count; i++)
    {
        PDeviceQueue* queue = &device->queues[i];
        if(queue->timeline != VK_NULL_HANDLE && device_queue_current_value(queue) > 0)
        {
            active++;
        }
    }

    if(active == 0)
    {
        return PIGMENT_SUCCESS;
    }

    PWaitTarget* targets = P_NEW_ARRAY_FOR_COMMAND(pigment, targets, active);
    if(targets == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    uint32_t fill = 0;
    for(uint32_t i = 0; i < device->queue_count; i++)
    {
        PDeviceQueue* queue    = &device->queues[i];
        uint64_t current_value = device_queue_current_value(queue);
        if(queue->timeline == VK_NULL_HANDLE || current_value == 0)
        {
            continue;
        }

        targets[fill++] = (PWaitTarget) {
            .kind     = P_WAIT_TIMELINE,
            .timeline = {.semaphore = queue->timeline, .value = current_value},
        };
    }

    *out_targets = targets;
    *out_count   = fill;
    return PIGMENT_SUCCESS;
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
