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
 * @brief Defer a destroy until the GPU is done with the resource.
 *
 * `destroy_fn` runs at a later drain, once every renderer's in-flight frame
 * has completed. Use this for your own resources because built-in pigment_destroy_*
 * already defer through the deletion queue.
 *
 * @param pigment Pigment instance.
 * @param destroy_fn The function to call to destroy the resource.
 * @param resource The resource to destroy. Passed as the second argument to destroy_fn.
 */
void pigment_defer_destroy(Pigment* pigment, PDestroyFn destroy_fn, void* resource);

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
