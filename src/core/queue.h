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

#include "defines.h"

/**
 * @brief Find a queue that has all `required` flags and none of the `forbidden` flags.
 *
 * When several queues match, picks the most specialized one (the one with the fewest extra
 * flags). For example, pigment_get_queue(p, P_QUEUE_TRANSFER_BIT, 0) returns a dedicated
 * transfer queue if one exists, else the next-most-specialized fallback.
 *
 * @param pigment Pigment instance.
 * @param required Flags the queue must support.
 * @param forbidden Flags the queue must NOT support. 0 means any match works.
 *
 * @return The matching queue, or NULL if none found.
 */
PDeviceQueue* pigment_get_queue(Pigment* pigment, PQueueFlags required, PQueueFlags forbidden);

/**
 * @brief How many queues Pigment created on the device.
 *
 * With explicit PQueueRequest config it  always equals queue_request_count
 * (any failed request aborts init_pigment). With the default policy it is the number
 * of defaults the hardware could provide (1 graphics always, +1 dedicated compute if available,
 * +1 dedicated transfer if available).
 *
 * @param pigment Pigment instance.
 *
 * @return Queue count.
 */
uint32_t pigment_get_queue_count(Pigment* pigment);

/**
 * @brief Returns the queue at position `index`. The order matches PigmentConfig.queue_requests
 *        when set, or the default order (graphics, then dedicated compute, then dedicated transfer)
 *        otherwise.
 *
 * @param pigment Pigment instance.
 * @param index Queue index, must be less than pigment_get_queue_count.
 *
 * @return The queue, or NULL if index is out of bounds.
 */
PDeviceQueue* pigment_get_queue_at(Pigment* pigment, uint32_t index);

#endif
