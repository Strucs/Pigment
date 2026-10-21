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

#include "internal.h"
#include "structs.h"

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
    PBool (*ready_fn)(Pigment* pigment, void* resource);
    void* resource;
    PResourceTracker* tracker;
    void* ready_resource;
    VkFence fence;
    PBool owned;
} PDeletionEntry;

struct PDeletionNode {
    PDeletionEntry entry;
    struct PDeletionNode* next;
    uint64_t timeline_values[];
};

typedef struct PDeletionChunk {
    struct PDeletionChunk* next;
    _Alignas(PDeletionNode) unsigned char nodes[];
} PDeletionChunk;

typedef struct PDeletionTsd {
    PDeletionQueue* queue;
    PDeletionNode* private_free;
    struct PDeletionTsd* next;
} PDeletionTsd;

struct PDeletionQueue {
    _Atomic(PDeletionNode*) head;
    _Atomic(PDeletionNode*) shared_free;
    _Atomic(PDeletionChunk*) chunks;
    _Atomic(PDeletionTsd*) tsds;
    pigment_tsd_t tsd;
    uint64_t node_stride;
};

static PBool swapchain_ready(Pigment* pigment, void* resource);
static PBool entry_ready(Pigment* pigment, const PDeletionNode* node);
static void collect_active_targets(Pigment* pigment, PDeletionNode* node);
static void prepend_chain(PDeletionQueue* queue, PDeletionNode* chain_head, PDeletionNode* chain_tail);
static void prepend_shared_free(PDeletionQueue* queue, PDeletionNode* chain_head, PDeletionNode* chain_tail);
static PDeletionNode* node_acquire(Pigment* pigment, PDeletionQueue* queue);
static PDeletionNode* allocate_chunk(Pigment* pigment, PDeletionQueue* queue);
static void tsd_destructor(void* ptr);
static void wait_device_for_destruction(Pigment* pigment);
static void wait_submitted(Pigment* pigment);
static PBool target_signaled(VkDevice device, const PWaitTarget* target);
static PBool wait_target(Pigment* pigment, const PWaitTarget* target);
static void destroy_tracked(Pigment* pigment, PResourceTracker* tracker);
static void destroy_tracked_resources(Pigment* pigment, void* resource);

PDeletionQueue* create_deletion_queue(Pigment* pigment)
{
    PDeletionQueue* queue = P_NEW_FOR_INSTANCE(pigment, queue);
    if(queue == NULL)
    {
        return NULL;
    }

    uint64_t alignment = _Alignof(PDeletionNode);
    uint64_t size      = sizeof(PDeletionNode) + sizeof(uint64_t) * pigment->device->queue_count;
    queue->node_stride = (size + alignment - 1) / alignment * alignment;
    atomic_init(&queue->head, NULL);
    atomic_init(&queue->shared_free, NULL);
    atomic_init(&queue->chunks, NULL);
    atomic_init(&queue->tsds, NULL);

    if(pigment_tsd_init(&queue->tsd, tsd_destructor) != 0)
    {
        P_FREE(pigment, queue);
        return NULL;
    }

    PDeletionNode* first = allocate_chunk(pigment, queue);
    if(first == NULL)
    {
        pigment_tsd_destroy(queue->tsd);
        P_FREE(pigment, queue);
        return NULL;
    }
    prepend_shared_free(queue, first, first);

    return queue;
}

void destroy_deletion_queue(Pigment* pigment, PDeletionQueue* queue)
{
    if(queue == NULL)
    {
        return;
    }

    for(;;)
    {
        PDeletionNode* node = atomic_exchange_explicit(&queue->head, NULL, memory_order_acquire);
        if(node == NULL)
        {
            break;
        }

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
            node = next;
        }
    }

    pigment_tsd_destroy(queue->tsd);

    PDeletionTsd* tsd = atomic_load_explicit(&queue->tsds, memory_order_acquire);
    while(tsd != NULL)
    {
        PDeletionTsd* next = tsd->next;
        tsd_destructor(tsd);
        P_FREE(pigment, tsd);
        tsd = next;
    }

    PDeletionChunk* chunk = atomic_load_explicit(&queue->chunks, memory_order_relaxed);
    while(chunk != NULL)
    {
        PDeletionChunk* next = chunk->next;
        P_FREE(pigment, chunk);
        chunk = next;
    }
    P_FREE(pigment, queue);
    pigment->deletions = NULL;
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

    PDeletionNode* node = node_acquire(pigment, pigment->deletions);
    if(node == NULL)
    {
        wait_submitted(pigment);
        destroy_fn(pigment, resource);
        return;
    }

    defer_destroy_node(pigment, node, destroy_fn, resource);
}

void resource_tracker_retain(PResourceTracker* tracker)
{
    atomic_fetch_add_explicit(&tracker->references, 1, memory_order_relaxed);
}

void resource_tracker_release(Pigment* pigment, PResourceTracker* tracker)
{
    if(atomic_fetch_sub_explicit(&tracker->references, 1, memory_order_acq_rel) == 1)
    {
        destroy_tracked(pigment, tracker);
    }
}

void resource_tracker_add(PResourceTracker* tracker, PTrackedResource* resource)
{
    resource_tracker_retain(tracker);

    PTrackedResource* head = atomic_load_explicit(&tracker->dependents, memory_order_relaxed);
    do
    {
        resource->next = head;
    }
    while(!atomic_compare_exchange_weak_explicit(&tracker->dependents, &head, resource, memory_order_release, memory_order_relaxed));
}

static void destroy_tracked_resources(Pigment* pigment, void* resource)
{
    PResourceTracker* tracker = resource;
    PTrackedResource* node    = atomic_load_explicit(&tracker->dependents, memory_order_acquire);
    while(node != NULL)
    {
        PTrackedResource* next = node->next;
        node->destroy_fn(pigment, node->resource);
        node = next;
    }

    tracker->destroy_fn(pigment, tracker->resource);
}

void pigment_defer_destroy_tracked(Pigment* pigment, PDestroyFn destroy_fn, void* resource, PResourceTracker* tracker)
{
    if(pigment == NULL || destroy_fn == NULL || resource == NULL)
    {
        return;
    }

    if(tracker == NULL)
    {
        pigment_defer_destroy(pigment, destroy_fn, resource);
        return;
    }

    if(tracker->last_used == NULL)
    {
        PLOG_ERROR(pigment, "Uninitialized resource tracker in destruction request.");
        return;
    }

    tracker->destroy_fn = destroy_fn;
    tracker->resource   = resource;
    resource_tracker_release(pigment, tracker);
}

static void destroy_tracked(Pigment* pigment, PResourceTracker* tracker)
{
    if(pigment->deletions == NULL)
    {
        destroy_tracked_resources(pigment, tracker);
        return;
    }

    PDeletionNode* node    = tracker->deletion;
    node->entry.destroy_fn = destroy_tracked_resources;
    node->entry.resource   = tracker;
    node->entry.tracker    = tracker;

    // The final reference fixes the submit values for the rest of this lifetime.
    PBool submitted = P_FALSE;
    for(uint32_t i = 0; i < pigment->device->queue_count; i++)
    {
        submitted |= atomic_load_explicit(&tracker->last_used[i], memory_order_relaxed) != 0;
    }

    if(!submitted && node->entry.ready_fn == NULL)
    {
        destroy_tracked_resources(pigment, tracker);
        return;
    }

    prepend_chain(pigment->deletions, node, node);
}

// These nodes have no timeline_values storage
// Never pass them to defer_destroy_node or collect_active_targets
PDeletionNode* create_tracker_deletion(Pigment* pigment)
{
    PDeletionNode* node = P_NEW_FOR_OBJECT(pigment, node);
    if(node != NULL)
    {
        node->entry.owned = P_TRUE;
    }

    return node;
}

PDeletionNode* create_resource_deletion(Pigment* pigment)
{
    uint64_t size = sizeof(PDeletionNode) + sizeof(uint64_t) * pigment->device->queue_count;
    if(size > SIZE_MAX)
    {
        return NULL;
    }

    PDeletionNode* node = P_CALLOC_OBJECT(pigment, 1, size, _Alignof(PDeletionNode));
    if(node != NULL)
    {
        node->entry.owned = P_TRUE;
    }

    return node;
}

void defer_destroy_node(Pigment* pigment, PDeletionNode* node, PDestroyFn destroy_fn, void* resource)
{
    if(pigment->deletions == NULL)
    {
        destroy_fn(pigment, resource);
        return;
    }

    node->entry.destroy_fn = destroy_fn;
    node->entry.resource   = resource;
    collect_active_targets(pigment, node);
    prepend_chain(pigment->deletions, node, node);
}

void prepare_swapchain_deletion(PSwapchain* swapchain)
{
    PDeletionNode* node        = swapchain->tracker.deletion;
    node->entry.ready_fn       = swapchain_ready;
    node->entry.ready_resource = swapchain;
}

static PBool swapchain_ready(Pigment* pigment, void* resource)
{
    const PSwapchain* swapchain = resource;
    VkDevice device             = pigment->device->logical_device;
    PSync* sync                 = swapchain->sync;

    for(uint32_t i = 0; i < pigment->config.max_frames_in_flight; i++)
    {
        VkFence fences[] = {
            sync->acquisitions[i].pending ? sync->acquisitions[i].fence : VK_NULL_HANDLE,
            sync->present_fences != NULL ? sync->present_fences[i] : VK_NULL_HANDLE,
        };

        for(uint32_t f = 0; f < sizeof(fences) / sizeof(fences[0]); f++)
        {
            if(fences[f] == VK_NULL_HANDLE)
            {
                continue;
            }

            if(vkGetFenceStatus(device, fences[f]) != VK_SUCCESS)
            {
                return P_FALSE;
            }
        }
    }

    if(sync->present_fences == NULL)
    {
        // Without presentation fences, wait for the device before destruction.
        VkResult result = vkDeviceWaitIdle(device);
        if(result != VK_SUCCESS)
        {
            PLOG_ERROR(pigment, "Failed to wait for swapchain destruction (result: %d)", result);
            return P_FALSE;
        }
    }

    return P_TRUE;
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

    PDeletionNode* node = node_acquire(pigment, pigment->deletions);
    if(node == NULL)
    {
        PWaitTarget target = {.kind = P_WAIT_FENCE, .fence = fence};
        if(fence != VK_NULL_HANDLE && !wait_target(pigment, &target))
        {
            wait_device_for_destruction(pigment);
        }
        destroy_fn(pigment, resource);
        return;
    }

    node->entry.destroy_fn = destroy_fn;
    node->entry.resource   = resource;
    node->entry.fence      = fence;
    prepend_chain(pigment->deletions, node, node);
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

        PBool ready = entry_ready(pigment, node);

        if(ready)
        {
            PBool owned = node->entry.owned;
            node->entry.destroy_fn(pigment, node->entry.resource);
            if(!owned)
            {
                node->next = ready_head;
                ready_head = node;
                if(ready_tail == NULL)
                {
                    ready_tail = node;
                }
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
        if(tsd != NULL)
        {
            tsd->queue = queue;
            if(pigment_tsd_set(queue->tsd, tsd) != 0)
            {
                P_FREE(pigment, tsd);
                tsd = NULL;
            }
            else
            {
                PDeletionTsd* head = atomic_load_explicit(&queue->tsds, memory_order_relaxed);
                do
                {
                    tsd->next = head;
                }
                while(!atomic_compare_exchange_weak_explicit(&queue->tsds, &head, tsd, memory_order_release, memory_order_relaxed));
            }
        }
    }

    // Try to acquire a node from the thread-local private free list first.
    if(tsd != NULL && tsd->private_free != NULL)
    {
        PDeletionNode* node = tsd->private_free;
        tsd->private_free   = node->next;
        memset(node, 0, (size_t) queue->node_stride);
        return node;
    }

    // Try to acquire a node from the shared free list and cache it in the thread-local private free list.
    PDeletionNode* node = atomic_exchange_explicit(&queue->shared_free, NULL, memory_order_acquire);
    if(node == NULL)
    {
        return allocate_chunk(pigment, queue);
    }

    PDeletionNode* rest = node->next;
    if(tsd != NULL)
    {
        tsd->private_free = rest;
    }
    else if(rest != NULL)
    {
        PDeletionNode* tail = rest;
        while(tail->next != NULL)
        {
            tail = tail->next;
        }
        prepend_shared_free(queue, rest, tail);
    }
    memset(node, 0, (size_t) queue->node_stride);

    return node;
}

static void tsd_destructor(void* ptr)
{
    PDeletionTsd* tsd = ptr;
    if(tsd == NULL)
    {
        return;
    }

    if(tsd->private_free != NULL)
    {
        PDeletionNode* tail = tsd->private_free;
        while(tail->next != NULL)
        {
            tail = tail->next;
        }
        prepend_shared_free(tsd->queue, tsd->private_free, tail);
        tsd->private_free = NULL;
    }
}

static PDeletionNode* allocate_chunk(Pigment* pigment, PDeletionQueue* queue)
{
    uint64_t size = sizeof(PDeletionChunk) + queue->node_stride * PIGMENT_DELETION_CHUNK_NODES;
    if(size > SIZE_MAX)
    {
        return NULL;
    }

    PDeletionChunk* chunk = P_CALLOC_INSTANCE(pigment, 1, size, _Alignof(PDeletionChunk));
    if(chunk == NULL)
    {
        return NULL;
    }

    PDeletionChunk* head = atomic_load_explicit(&queue->chunks, memory_order_relaxed);
    do
    {
        chunk->next = head;
    }
    while(!atomic_compare_exchange_weak_explicit(&queue->chunks, &head, chunk, memory_order_release, memory_order_relaxed));

    for(uint32_t i = 0; i < PIGMENT_DELETION_CHUNK_NODES - 1; i++)
    {
        PDeletionNode* node = (PDeletionNode*) (chunk->nodes + queue->node_stride * i);
        node->next          = (PDeletionNode*) (chunk->nodes + queue->node_stride * (i + 1));
    }

    PDeletionNode* first = (PDeletionNode*) chunk->nodes;
    PDeletionNode* tail  = (PDeletionNode*) (chunk->nodes + queue->node_stride * (PIGMENT_DELETION_CHUNK_NODES - 1));
    prepend_shared_free(queue, first->next, tail);
    first->next = NULL;

    return first;
}

static void collect_active_targets(Pigment* pigment, PDeletionNode* node)
{
    for(uint32_t i = 0; i < pigment->device->queue_count; i++)
    {
        PDeviceQueue* queue                = &pigment->device->queues[i];
        node->timeline_values[queue->slot] = device_queue_current_value(queue);
    }
}

static void wait_device_for_destruction(Pigment* pigment)
{
    PResult result = device_wait_idle(pigment);
    if(result != PIGMENT_SUCCESS)
    {
        PLOG_ERROR(pigment, "Device wait idle failed during deferred destruction (result: %d). Continuing.", result);
    }
}

static void wait_submitted(Pigment* pigment)
{
    for(uint32_t i = 0; i < pigment->device->queue_count; i++)
    {
        PDeviceQueue* queue = &pigment->device->queues[i];
        uint64_t value      = device_queue_current_value(queue);
        if(queue->timeline == VK_NULL_HANDLE || value == 0)
        {
            continue;
        }

        PWaitTarget target = {
            .kind     = P_WAIT_TIMELINE,
            .timeline = {.semaphore = queue->timeline, .value = value},
        };

        if(!wait_target(pigment, &target))
        {
            wait_device_for_destruction(pigment);
            return;
        }
    }
}

static PBool entry_ready(Pigment* pigment, const PDeletionNode* node)
{
    VkDevice device             = pigment->device->logical_device;
    const PDeletionEntry* entry = &node->entry;
    if(entry->fence != VK_NULL_HANDLE)
    {
        PWaitTarget target = {.kind = P_WAIT_FENCE, .fence = entry->fence};
        if(!target_signaled(device, &target))
        {
            return P_FALSE;
        }
    }

    for(uint32_t i = 0; i < pigment->device->queue_count; i++)
    {
        PDeviceQueue* queue = &pigment->device->queues[i];

        uint64_t value = entry->tracker != NULL
                             ? atomic_load_explicit(&entry->tracker->last_used[queue->slot], memory_order_relaxed)
                             : node->timeline_values[queue->slot];

        if(queue->timeline == VK_NULL_HANDLE || value == 0)
        {
            continue;
        }

        PWaitTarget target = {
            .kind     = P_WAIT_TIMELINE,
            .timeline = {.semaphore = queue->timeline, .value = value},
        };

        if(!target_signaled(device, &target))
        {
            return P_FALSE;
        }
    }

    return entry->ready_fn == NULL || entry->ready_fn(pigment, entry->ready_resource);
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

static PBool wait_target(Pigment* pigment, const PWaitTarget* target)
{
    VkDevice device = pigment->device->logical_device;
    VkResult result;

    if(target->kind == P_WAIT_FENCE)
    {
        result = vkWaitForFences(device, 1, &target->fence, VK_TRUE, UINT64_MAX);
    }
    else
    {
        VkSemaphoreWaitInfo wait_info = {
            .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores    = &target->timeline.semaphore,
            .pValues        = &target->timeline.value,
        };

        result = vkWaitSemaphores(device, &wait_info, UINT64_MAX);
    }

    if(result != VK_SUCCESS)
    {
        PLOG_ERROR(pigment, "Deferred destroy wait failed (result: %d)", result);
        return P_FALSE;
    }

    return P_TRUE;
}
