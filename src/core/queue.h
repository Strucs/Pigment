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

#ifndef PIGMENT_QUEUE_H
#define PIGMENT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

/**
 * @brief Find a queue that has all `required` flags.
 *
 * When several queues match, picks the most specialized one (the fewest extra flags). A graphics
 * or compute queue always reports transfer support, so pigment_get_queue(p, P_QUEUE_TRANSFER_BIT)
 * returns a dedicated transfer queue when one exists, else the next-most-specialized fallback.
 *
 * @param pigment Pigment instance.
 * @param required Flags the queue must support.
 *
 * @return The matching queue, or NULL if none found.
 */
PIGMENT_API PDeviceQueue* pigment_get_queue(Pigment* pigment, PQueueFlags required);

/**
 * @brief How many queues Pigment created on the device.
 *
 * @param pigment Pigment instance.
 *
 * @return Queue count.
 */
PIGMENT_API uint32_t pigment_get_queue_count(Pigment* pigment);

/**
 * @brief Returns the queue at `index` in the internal device queue list.
 *
 * @param pigment Pigment instance.
 * @param index Queue index, must be less than pigment_get_queue_count.
 *
 * @return The queue, or NULL if index is out of bounds.
 */
PIGMENT_API PDeviceQueue* pigment_get_queue_at(Pigment* pigment, uint32_t index);

/**
 * @brief How many queues the request at `request_index` actually obtained.
 *
 * A request asks for up to PQueueRequest.count distinct queues and is clamped to what the hardware
 * exposes, so this can be less than requested. The index matches PigmentConfig.queue_requests, or
 * the default order (graphics, compute, transfer) when no requests are set. Use it to size a
 * worker pool to the queues actually available.
 *
 * @param pigment Pigment instance.
 * @param request_index Index into PigmentConfig.queue_requests.
 *
 * @return Realized queue count for that request, 0 if request_index is out of bounds.
 */
PIGMENT_API uint32_t pigment_request_queue_count(Pigment* pigment, uint32_t request_index);

/**
 * @brief Returns the `index`-th queue obtained by the request at `request_index`.
 *
 * @param pigment Pigment instance.
 * @param request_index Index into PigmentConfig.queue_requests.
 * @param index Queue index within the request, must be less than pigment_request_queue_count.
 *
 * @return The queue, or NULL if either index is out of bounds.
 */
PIGMENT_API PDeviceQueue* pigment_request_queue(Pigment* pigment, uint32_t request_index, uint32_t index);

/**
 * @brief Index of the Vulkan queue family this queue belongs to.
 *
 * @param queue Queue to query.
 *
 * @return The queue family index, or UINT32_MAX if queue is NULL.
 */
PIGMENT_API uint32_t pigment_queue_family(PDeviceQueue* queue);

/**
 * @brief Capability flags this queue supports.
 *
 * These are the queue family capabilities, so a queue can support more than the request that
 * reserved it asked for (a transfer queue from the graphics family have graphics flag too).
 *
 * @param queue Queue to query.
 *
 * @return The queue flags, or 0 if queue is NULL.
 */
PIGMENT_API PQueueFlags pigment_queue_flags(PDeviceQueue* queue);

#ifdef __cplusplus
}
#endif

#endif
