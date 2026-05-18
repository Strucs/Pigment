/**
 * Copyright 2025-2026 Angel-Leduc TA
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

#ifndef PIGMENT_LOG_INTERNAL_H
#define PIGMENT_LOG_INTERNAL_H

#include "defines.h"

PResult pigment_log_init(Pigment* pigment);
void pigment_log_destroy(Pigment* pigment);

void pigment_log_dispatch(Pigment* pigment, PigmentLogSeverity severity, PigmentLogType type, const char* message_id_name, int32_t message_id, const char* fmt, ...)
    __attribute__((format(printf, 6, 7)));

PBool pigment_log_should_dispatch(const Pigment* p, PigmentLogSeverity sev, PigmentLogType type);

#define PLOG(pigment, sev, type, id_name, id, fmt, ...)                                               \
    do                                                                                                \
    {                                                                                                 \
        Pigment* _p = (pigment);                                                                      \
        if(pigment_log_should_dispatch(_p, (sev), (type)))                                            \
        {                                                                                             \
            pigment_log_dispatch(_p, (sev), (type), (id_name), (id), fmt __VA_OPT__(, ) __VA_ARGS__); \
        }                                                                                             \
    }                                                                                                 \
    while(0)

#define PLOG_TRACE(p, fmt, ...) \
    PLOG(p, PIGMENT_LOG_TRACE_BIT, PIGMENT_LOG_TYPE_GENERAL_BIT, NULL, 0, fmt __VA_OPT__(, ) __VA_ARGS__)

#define PLOG_DEBUG(p, fmt, ...) \
    PLOG(p, PIGMENT_LOG_DEBUG_BIT, PIGMENT_LOG_TYPE_GENERAL_BIT, NULL, 0, fmt __VA_OPT__(, ) __VA_ARGS__)

#define PLOG_INFO(p, fmt, ...) \
    PLOG(p, PIGMENT_LOG_INFO_BIT, PIGMENT_LOG_TYPE_GENERAL_BIT, NULL, 0, fmt __VA_OPT__(, ) __VA_ARGS__)

#define PLOG_WARN(p, fmt, ...) \
    PLOG(p, PIGMENT_LOG_WARN_BIT, PIGMENT_LOG_TYPE_GENERAL_BIT, NULL, 0, fmt __VA_OPT__(, ) __VA_ARGS__)

#define PLOG_ERROR(p, fmt, ...) \
    PLOG(p, PIGMENT_LOG_ERROR_BIT, PIGMENT_LOG_TYPE_GENERAL_BIT, NULL, 0, fmt __VA_OPT__(, ) __VA_ARGS__)

#endif
