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

#ifndef PIGMENT_LOG_H
#define PIGMENT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "defines.h"

#include <stddef.h>
#include <stdint.h>

typedef struct PigmentLogRecord {
    const char* message_id_name;
    int32_t message_id;
    const char* message;
} PigmentLogRecord;

typedef void (*PigmentLogCallback)(
    PigmentLogSeverity severity,
    PigmentLogType type,
    const PigmentLogRecord* record,
    void* user_data
);

struct PigmentLoggerCreateInfo {
    void* user_data;
    PigmentLogSeverity severity_filter;
    PigmentLogType type_filter;
    PigmentLogCallback callback;
};

struct PigmentLogger {
    void* user_data;
    PigmentLogSeverity severity_filter;
    PigmentLogType type_filter;
    PigmentLogCallback callback;
    PigmentLogger* next;
};

PigmentLogger* pigment_logger_create(Pigment* pigment, const PigmentLoggerCreateInfo* info);
void pigment_logger_destroy(Pigment* pigment, PigmentLogger* logger);

void pigment_default_log_callback(
    PigmentLogSeverity severity,
    PigmentLogType type,
    const PigmentLogRecord* record,
    void* user_data
);

#ifdef __cplusplus
}
#endif

#endif
