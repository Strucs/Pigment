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

#ifndef PIGMENT_STD_CAMERA_H
#define PIGMENT_STD_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "pigment/defines.h"

typedef struct PCamera PCamera;

PIGMENT_API PCamera* pigment_std_create_camera(Pigment* pigment);
PIGMENT_API void pigment_std_destroy_camera(Pigment* pigment, PCamera* camera);

PIGMENT_API void pigment_std_camera_set_view(PCamera* camera, PMat4 view);
PIGMENT_API void pigment_std_camera_set_projection(PCamera* camera, PMat4 projection);

/**
 * @brief Publish the camera and retain its buffer in a command buffer.
 *
 * The address belongs to this recording. Request it again for each new recording.
 * Unchanged data shares its version. Modified data reuses an idle buffer or allocates one.
 * If publication fails, CPU changes are preserved for the next call.
 * The caller must serialize access to this object.
 *
 * @param pigment Pigment instance.
 * @param camera Camera to publish.
 * @param cmd Command buffer that uses the returned address.
 *
 * @return Buffer address, or 0 on invalid input or publication failure.
 */
PIGMENT_API uint64_t pigment_std_camera_address(Pigment* pigment, PCamera* camera, PCommandBuffer* cmd);

#ifdef __cplusplus
}
#endif

#endif
