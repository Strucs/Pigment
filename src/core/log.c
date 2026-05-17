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

#include "log.h"

#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define PIGMENT_LOG_STACK_BUFFER 1024
#define PIGMENT_LOG_SNAPSHOT_MAX 16

typedef struct LoggerSnapshot {
    void* user_data;
    PigmentLogCallback callback;
} LoggerSnapshot;

static void recompute_active_masks_locked(PLogState* log)
{
    uint32_t sev_mask  = 0;
    uint32_t type_mask = 0;
    for(PigmentLogger* logger = log->loggers; logger != NULL; logger = logger->next)
    {
        sev_mask |= (uint32_t) logger->severity_filter;
        type_mask |= (uint32_t) logger->type_filter;
    }
    atomic_store_explicit(&log->active_severities, sev_mask, memory_order_release);
    atomic_store_explicit(&log->active_types, type_mask, memory_order_release);
}

PResult pigment_log_init(Pigment* pigment)
{
    if(pigment == NULL)
    {
        return PIGMENT_ERROR;
    }

    PLogState* log = P_NEW_FOR_INSTANCE(pigment, log);
    if(log == NULL)
    {
        return PIGMENT_ERROR_OUT_OF_MEMORY;
    }

    if(pigment_rwlock_init(&log->lock) != 0)
    {
        P_FREE(pigment, log);
        return PIGMENT_ERROR;
    }

    pigment->log = log;
    return PIGMENT_SUCCESS;
}

void pigment_log_destroy(Pigment* pigment)
{
    if(pigment == NULL || pigment->log == NULL)
    {
        return;
    }

    PLogState* log = pigment->log;

    pigment_rwlock_wrlock(&log->lock);
    PigmentLogger* logger = log->loggers;
    while(logger != NULL)
    {
        PigmentLogger* next = logger->next;
        P_FREE(pigment, logger);
        logger = next;
    }
    log->loggers      = NULL;
    log->logger_count = 0;
    pigment_rwlock_wrunlock(&log->lock);
    pigment_rwlock_destroy(&log->lock);

    P_FREE(pigment, log);
    pigment->log = NULL;
}

PigmentLogger* pigment_logger_create(Pigment* pigment, const PigmentLoggerCreateInfo* info)
{
    if(pigment == NULL || pigment->log == NULL || info == NULL || info->callback == NULL)
    {
        return NULL;
    }

    PLogState* log = pigment->log;

    PigmentLogger* logger = P_NEW_FOR_INSTANCE(pigment, logger);
    if(logger == NULL)
    {
        return NULL;
    }

    logger->severity_filter = info->severity_filter;
    logger->type_filter     = info->type_filter;
    logger->callback        = info->callback;
    logger->user_data       = info->user_data;

    pigment_rwlock_wrlock(&log->lock);
    logger->next = log->loggers;
    log->loggers = logger;
    log->logger_count++;
    recompute_active_masks_locked(log);
    pigment_rwlock_wrunlock(&log->lock);

    return logger;
}

void pigment_logger_destroy(Pigment* pigment, PigmentLogger* logger)
{
    if(pigment == NULL || pigment->log == NULL || logger == NULL)
    {
        return;
    }
    PLogState* log = pigment->log;

    pigment_rwlock_wrlock(&log->lock);
    PigmentLogger** cursor = &log->loggers;
    while(*cursor != NULL && *cursor != logger)
    {
        cursor = &(*cursor)->next;
    }
    if(*cursor != NULL)
    {
        *cursor = logger->next;
        log->logger_count--;
    }
    recompute_active_masks_locked(log);
    pigment_rwlock_wrunlock(&log->lock);

    P_FREE(pigment, logger);
}

void pigment_log_dispatch(Pigment* pigment, PigmentLogSeverity severity, PigmentLogType type, const char* message_id_name, int32_t message_id, const char* fmt, ...)
{
    PLogState* log = pigment->log;

    char stack_buf[PIGMENT_LOG_STACK_BUFFER];
    char* buf      = stack_buf;
    char* heap_buf = NULL;

    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);

    int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    va_end(args);

    if(needed < 0)
    {
        va_end(args_copy);
        return;
    }

    if((size_t) needed >= sizeof(stack_buf))
    {
        heap_buf = P_ALLOC_COMMAND(pigment, (size_t) needed + 1, _Alignof(char));
        if(heap_buf != NULL)
        {
            vsnprintf(heap_buf, (size_t) needed + 1, fmt, args_copy);
            buf = heap_buf;
        }
        // If malloc fails, keep the truncated message in stack_buf rather than giving up completely.
    }
    va_end(args_copy);

    LoggerSnapshot snapshots[PIGMENT_LOG_SNAPSHOT_MAX] = {0};
    size_t snapshot_count                              = 0;

    pigment_rwlock_rdlock(&log->lock);
    for(PigmentLogger* logger = log->loggers; logger != NULL && snapshot_count < PIGMENT_LOG_SNAPSHOT_MAX; logger = logger->next)
    {
        if((logger->severity_filter & severity) && (logger->type_filter & type))
        {
            snapshots[snapshot_count].callback  = logger->callback;
            snapshots[snapshot_count].user_data = logger->user_data;
            snapshot_count++;
        }
    }
    pigment_rwlock_rdunlock(&log->lock);

    // Dispatch to snapshots outside of lock to avoid deadlocks if callback tries to create/destroy loggers.
    PigmentLogRecord record = {
        .message_id_name = message_id_name,
        .message_id      = message_id,
        .message         = buf,
    };
    for(size_t i = 0; i < snapshot_count; i++)
    {
        snapshots[i].callback(severity, type, &record, snapshots[i].user_data);
    }

    P_FREE(pigment, heap_buf);
}

#ifdef _WIN32
    #include <io.h>
    #define pigment_isatty(fd) _isatty(fd)
    #define pigment_fileno(stream) _fileno(stream)
#else
    #include <unistd.h>
    #define pigment_isatty(fd) isatty(fd)
    #define pigment_fileno(stream) fileno(stream)
#endif

static _Atomic int g_default_color_state = -1;

static int detect_color_support(void)
{
    int fd = pigment_fileno(stderr);
    if(!pigment_isatty(fd))
    {
        return 0;
    }

    // (https://no-color.org/)
    const char* nc = getenv("NO_COLOR");
    if(nc != NULL && nc[0] != '\0')
    {
        return 0;
    }

#ifdef _WIN32

    HANDLE h = (HANDLE) _get_osfhandle(fd);
    if(h == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    DWORD mode = 0;
    if(!GetConsoleMode(h, &mode))
    {
        return 0;
    }
    if(!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        if(!SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        {
            return 0;
        }
    }
#endif

    return 1;
}

static int color_enabled(void)
{
    int v = atomic_load_explicit(&g_default_color_state, memory_order_acquire);
    if(v < 0)
    {
        v = detect_color_support();
        atomic_store_explicit(&g_default_color_state, v, memory_order_release);
    }
    return v;
}

void pigment_default_log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    (void) type;
    (void) user_data;

    const char* level_str;
    const char* color;

    switch(severity)
    {
        case PIGMENT_LOG_TRACE_BIT:
            level_str = "TRACE";
            color     = "\x1b[2;37m";    // gray
            break;
        case PIGMENT_LOG_DEBUG_BIT:
            level_str = "DEBUG";
            color     = "\x1b[34m";    // blue
            break;
        case PIGMENT_LOG_INFO_BIT:
            level_str = " INFO";
            color     = "\x1b[32m";    // green
            break;
        case PIGMENT_LOG_WARN_BIT:
            level_str = " WARN";
            color     = "\x1b[33m";    // yellow
            break;
        case PIGMENT_LOG_ERROR_BIT:
            level_str = "ERROR";
            color     = "\x1b[31m";    // red
            break;
        default:
            return;
    }

    const char* reset      = "\x1b[0m";
    const int colored      = color_enabled();
    const char* lvl_prefix = colored ? color : "";
    const char* lvl_suffix = colored ? reset : "";

    if(record->message_id_name != NULL)
    {
        fprintf(stderr, "%s%s%s [%s] %s\n", lvl_prefix, level_str, lvl_suffix, record->message_id_name, record->message);
    }
    else
    {
        fprintf(stderr, "%s%s%s %s\n", lvl_prefix, level_str, lvl_suffix, record->message);
    }
}
