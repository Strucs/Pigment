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

#include "pigment_sdl.h"

static SDL_LogPriority severity_to_sdl(PigmentLogSeverity severity)
{
    switch(severity)
    {
        case PIGMENT_LOG_TRACE_BIT:
            return SDL_LOG_PRIORITY_TRACE;
        case PIGMENT_LOG_DEBUG_BIT:
            return SDL_LOG_PRIORITY_DEBUG;
        case PIGMENT_LOG_INFO_BIT:
            return SDL_LOG_PRIORITY_INFO;
        case PIGMENT_LOG_WARN_BIT:
            return SDL_LOG_PRIORITY_WARN;
        case PIGMENT_LOG_ERROR_BIT:
            return SDL_LOG_PRIORITY_ERROR;
        default:
            return SDL_LOG_PRIORITY_INFO;
    }
}

void pigment_sdl_log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    (void) type;
    (void) user_data;

    SDL_LogPriority priority = severity_to_sdl(severity);

    if(record->message_id_name != NULL)
    {
        SDL_LogMessage(SDL_LOG_CATEGORY_GPU, priority, "[%s] %s", record->message_id_name, record->message);
    }
    else
    {
        SDL_LogMessage(SDL_LOG_CATEGORY_GPU, priority, "%s", record->message);
    }
}
