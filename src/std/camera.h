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
#include "buffer_version.h"

typedef struct PCamera PCamera;

PIGMENT_API PCamera* pigment_std_create_camera(Pigment* pigment);
PIGMENT_API void pigment_std_destroy_camera(Pigment* pigment, PCamera* camera);

PIGMENT_API void pigment_std_camera_set_view(PCamera* camera, PMat4 view);
PIGMENT_API void pigment_std_camera_set_projection(PCamera* camera, PMat4 projection);

/**
 * @brief Prepare an immutable GPU version of the camera.
 *
 * The caller must serialize access to this object. Failure preserves CPU data for retry.
 * Release the returned reference with pigment_std_release_buffer_version after recording.
 * Unchanged data shares its version.
 *
 * @param pigment Pigment instance.
 * @param camera Source object.
 *
 * @return Prepared version, or NULL on invalid input or publication failure.
 */
PIGMENT_API PStdBufferVersion* pigment_std_camera_prepare(Pigment* pigment, PCamera* camera);

#ifdef __cplusplus
}
#endif

#endif
