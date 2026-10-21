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

#ifndef PIGMENT_DELETION_H
#define PIGMENT_DELETION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

typedef void (*PDestroyFn)(Pigment*, void*);

/**
 * @brief Destroy a custom resource after all currently submitted GPU work finishes.
 *
 * Built-in pigment_destroy_* functions already defer destruction.
 * Stop future uses and keep the resource alive until the callback.
 * Recorded commands that have not been submitted are not tracked by this function.
 * May block and invoke the callback before returning. Serialize this call with
 * submissions and presentation on all queues. Wait errors are logged and do not cancel the request.
 *
 * @param pigment Pigment instance.
 * @param destroy_fn The function to call to destroy the resource.
 * @param resource The resource to destroy. Passed as the second argument to destroy_fn.
 */
PIGMENT_API void pigment_defer_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource);

/**
 * @brief Defer destruction until recorded references and GPU uses have ended.
 *
 * Command buffers retain resources until reset or destruction, even without a submit.
 * Request destruction once per initialized tracker and stop adding new uses.
 * Keep the tracker and resource alive until the callback, which may run before returning.
 * Request destruction of images wrapped with this tracker too. They are destroyed before the callback.
 * The callback may release a custom tracker with pigment_destroy_resource_tracker.
 * An uninitialized tracker is logged and rejected. The resource remains owned by the caller.
 * A NULL tracker uses pigment_defer_destroy.
 *
 * @param pigment Pigment instance.
 * @param destroy_fn The function to call to destroy the resource.
 * @param resource The resource to destroy.
 * @param tracker Initialized resource tracker consumed by this request, or NULL.
 */
PIGMENT_API void pigment_defer_destroy_tracked(Pigment* pigment, PDestroyFn destroy_fn, void* resource, PResourceTracker* tracker);

/**
 * @brief Run pending destroys whose GPU work has completed.
 *
 * Also called by pigment_begin_frame_context and pigment_wait_idle.
 * May block while waiting for presentation to finish. Serialize this call with
 * submissions and presentation on all queues.
 *
 * @param pigment Pigment instance.
 */
PIGMENT_API void pigment_drain_pending(Pigment* pigment);

#ifdef __cplusplus
}
#endif

#endif
