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

#include "defines.h"

typedef void (*PDestroyFn)(Pigment*, void*);

/**
 * @brief Defer a destroy until every queue has finished its currently-submitted work.
 *
 * Use this for your own custom resources. Built-in pigment_destroy_* functions
 * (buffer, image, sampler, pipeline, command_pool, etc.) already
 * defer internally, so no need to call this on top of them. Prefer pigment_defer_destroy_tracked
 * when the resource carries a PResourceTracker.
 *
 * @param pigment Pigment instance.
 * @param destroy_fn The function to call to destroy the resource.
 * @param resource The resource to destroy. Passed as the second argument to destroy_fn.
 */
void pigment_defer_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource);

/**
 * @brief Defer a destroy with a precise wait target read from a resource tracker.
 *
 * Waits only on the submit value the resource was last stamped with with pigment_cmd_use_*.
 * If the tracker has never been stamped, the destroy runs immediately. Use this for your own
 * custom tracked resources. Built-in pigment_destroy_* functions that take a PResourceTracker
 * already use this internally.
 *
 * @param pigment Pigment instance.
 * @param destroy_fn The function to call to destroy the resource.
 * @param resource The resource to destroy.
 * @param tracker Tracker embedded in the resource (or any compatible struct).
 */
void pigment_defer_destroy_tracked(Pigment* pigment, PDestroyFn destroy_fn, void* resource, const PResourceTracker* tracker);

/**
 * @brief Run pending destroys whose GPU work has completed.
 *
 * Already called from pigment_begin_frame and pigment_wait_idle. Call manually
 * in headless or between heavy batches to reclaim memory without forcing the
 * full GPU pause that pigment_wait_idle imposes.
 *
 * @param pigment Pigment instance.
 */
void pigment_drain_pending(Pigment* pigment);

#endif
