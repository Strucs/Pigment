#include <pigment/pigment.h>
#include <pigment/pigment_sdl.h>

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORDERS 4
#define MAX_SLOTS 3
#define WORDS 64
#define STALL_TIMEOUT_MS 30000

#define CHECK(condition)                                                                              \
    do                                                                                                \
    {                                                                                                 \
        if(!(condition))                                                                              \
        {                                                                                             \
            fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #condition, SDL_GetError()); \
            exit(EXIT_FAILURE);                                                                       \
        }                                                                                             \
    }                                                                                                 \
    while(0)

typedef enum SubmitMode {
    SUBMIT_COMBINED,
    SUBMIT_BATCHED_GRAPHICS,
    SUBMIT_SPLIT_GRAPHICS,
    SUBMIT_SPLIT_QUEUES,
    SUBMIT_SEPARATE_PRESENT_SIGNAL,
    SUBMIT_INDEPENDENT_COPY,
    SUBMIT_TRANSFER_FIRST_BATCHED,
    SUBMIT_TRANSFER_AFTER_ACQUIRE,
    SUBMIT_TRANSFER_FIRST_SPLIT,
    SUBMIT_TRANSFER_DEPENDENCY,
    SUBMIT_MODE_COUNT,
} SubmitMode;

typedef enum TestQueue {
    GRAPHICS_QUEUE,
    COPY_QUEUE,
    TRANSFER_QUEUE,
    QUEUE_COUNT,
} TestQueue;

static const char* const mode_names[SUBMIT_MODE_COUNT] = {
    "combined",
    "batched_graphics",
    "split_graphics",
    "split_queues",
    "separate_present_signal",
    "independent_copy",
    "transfer_first_batched",
    "transfer_after_acquire",
    "transfer_first_split",
    "transfer_dependency",
};

#define MAX_SUBMITS 3

static PBool uses_transfer_queue(SubmitMode mode)
{
    return mode == SUBMIT_TRANSFER_FIRST_BATCHED || mode == SUBMIT_TRANSFER_AFTER_ACQUIRE
           || mode == SUBMIT_TRANSFER_FIRST_SPLIT || mode == SUBMIT_TRANSFER_DEPENDENCY;
}

static PBool copy_is_independent(SubmitMode mode)
{
    return mode == SUBMIT_INDEPENDENT_COPY || mode == SUBMIT_TRANSFER_FIRST_BATCHED
           || mode == SUBMIT_TRANSFER_AFTER_ACQUIRE || mode == SUBMIT_TRANSFER_FIRST_SPLIT;
}

static TestQueue copy_queue_for_mode(SubmitMode mode)
{
    if(uses_transfer_queue(mode))
    {
        return TRANSFER_QUEUE;
    }

    if(mode == SUBMIT_SPLIT_QUEUES
       || mode == SUBMIT_SEPARATE_PRESENT_SIGNAL
       || mode == SUBMIT_INDEPENDENT_COPY)
    {
        return COPY_QUEUE;
    }

    return GRAPHICS_QUEUE;
}

typedef struct Slot {
    PBuffer* source;
    PBuffer* intermediate;
    PBuffer* readback;
    PSubmitHandle handles[QUEUE_COUNT];
    uint32_t sequence;
    PBool pending;
} Slot;

typedef struct Test {
    Pigment* pigment;
    PWindowRenderer* renderer;
    SDL_ThreadID main_thread;
    SDL_Semaphore* record[RECORDERS];
    SDL_Semaphore* submit;
    SDL_Semaphore* done;
    Slot slots[MAX_SLOTS];
    PCommandBuffer* cmds[MAX_SLOTS];
    PCommandBuffer* copies[QUEUE_COUNT][MAX_SLOTS];
    PDeviceQueue* queues[QUEUE_COUNT];

    // One CPU frame at a time. Semaphore handoffs protect these fields.
    PFrame* frame;
    PCommandBuffer* cmd;
    PImage* image;
    PSubmitHandle handles[QUEUE_COUNT];
    uint32_t slot;
    uint32_t sequence;
    uint32_t stage;
    uint32_t recorded[RECORDERS];
    uint32_t submitted;
    uint32_t mode_submitted[SUBMIT_MODE_COUNT];
    uint32_t mode_verified[SUBMIT_MODE_COUNT];
    SDL_ThreadID recording_thread;
    PBool stop;

    _Atomic uint32_t completed;
    _Atomic int errors;
    _Atomic PBool expect_submit_error;
    _Atomic uint32_t submit_errors;
    _Atomic PBool watchdog_stop;
} Test;

typedef struct Recorder {
    Test* test;
    uint32_t index;
} Recorder;

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    Test* test = user_data;
    if((severity & PIGMENT_LOG_ERROR_BIT) && (type & PIGMENT_LOG_TYPE_GENERAL_BIT)
       && atomic_load_explicit(&test->expect_submit_error, memory_order_relaxed)
       && record->message != NULL && strstr(record->message, "pigment_queue_submit:") != NULL)
    {
        atomic_fetch_add_explicit(&test->submit_errors, 1, memory_order_relaxed);
        return;
    }

    if((severity & PIGMENT_LOG_ERROR_BIT)
       || ((severity & PIGMENT_LOG_WARN_BIT) && (type & PIGMENT_LOG_TYPE_VALIDATION_BIT))
       || (record->message != NULL && strstr(record->message, "Disabling validation") != NULL))
    {
        atomic_fetch_add_explicit(&test->errors, 1, memory_order_relaxed);
    }

    pigment_default_log_callback(severity, type, record, NULL);
}

static uint32_t env_count(const char* name, uint32_t fallback, uint32_t maximum)
{
    const char* text = SDL_getenv(name);
    if(text == NULL)
    {
        return fallback;
    }

    char* end;
    unsigned long value = strtoul(text, &end, 10);
    CHECK(end != text && *end == '\0' && value > 0 && value <= maximum);

    return (uint32_t) value;
}

static uint32_t pattern(uint32_t sequence, uint32_t word)
{
    return sequence * WORDS + word + 1;
}

static void verify_slot(Test* test, Slot* slot)
{
    if(!slot->pending)
    {
        return;
    }

    for(uint32_t i = 0; i < QUEUE_COUNT; i++)
    {
        CHECK(pigment_submit_complete(test->pigment, slot->handles[i]));
    }

    pigment_buffer_invalidate(test->pigment, slot->readback, 0, WORDS * sizeof(uint32_t));

    const uint32_t* words = pigment_buffer_mapped(slot->readback);
    for(uint32_t i = 0; i < WORDS; i++)
    {
        CHECK(words[i] == pattern(slot->sequence, i));
    }

    test->mode_verified[slot->sequence % SUBMIT_MODE_COUNT]++;
    slot->pending = P_FALSE;
}

static int record_worker(void* arg)
{
    Recorder* recorder = arg;
    Test* test         = recorder->test;
    CHECK(SDL_GetCurrentThreadID() != test->main_thread);

    for(;;)
    {
        SDL_WaitSemaphore(test->record[recorder->index]);
        if(test->stop)
        {
            return 0;
        }

        CHECK(test->stage == 0);
        CHECK(test->cmds[test->slot] == test->cmd);
        CHECK(pigment_frame_slot(test->frame) == test->slot);
        CHECK(pigment_frame_image(test->frame) == test->image);

        test->recording_thread = SDL_GetCurrentThreadID();
        if(((test->sequence >> 2) & 3) == recorder->index)
        {
            SDL_Delay(0);
        }

        pigment_begin_recording(test->pigment, test->cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

        PSwapchainPassDesc pass = {
            .clear_color = {(float) (test->sequence % 256) / 255.0f, 0.2f, 0.4f, 1.0f},
            .no_depth    = P_TRUE,
        };

        pigment_cmd_begin_swapchain_pass(test->pigment, test->cmd, test->frame, &pass);
        pigment_cmd_end_swapchain_pass(test->cmd, test->frame);

        Slot* slot       = &test->slots[test->slot];
        PBufferCopy copy = {.size = WORDS * sizeof(uint32_t)};
        pigment_cmd_copy_buffer(test->pigment, test->cmd, slot->source, slot->intermediate, &copy, 1);

        // Batched copies on one queue use a barrier. Separate calls use explicit waits.
        SubmitMode mode = (SubmitMode) (test->sequence % SUBMIT_MODE_COUNT);
        if(mode == SUBMIT_COMBINED || mode == SUBMIT_BATCHED_GRAPHICS)
        {
            PBufferBarrier barrier = {
                .buffer = slot->intermediate,
                .src    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
                .dst    = {P_PIPELINE_STAGE_TRANSFER_BIT,  P_MEMORY_ACCESS_TRANSFER_READ_BIT},
            };

            pigment_cmd_buffer_barriers(test->pigment, test->cmd, &barrier, 1);
        }

        pigment_end_recording(test->pigment, test->cmd);

        PCommandBuffer* copy_cmd = test->copies[copy_queue_for_mode(mode)][test->slot];
        pigment_begin_recording(test->pigment, copy_cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
        PBufferBarrier reuse_barrier = {
            .buffer = slot->readback,
            .src    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
            .dst    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        };

        pigment_cmd_buffer_barriers(test->pigment, copy_cmd, &reuse_barrier, 1);
        PBuffer* source = copy_is_independent(mode) ? slot->source : slot->intermediate;
        pigment_cmd_copy_buffer(test->pigment, copy_cmd, source, slot->readback, &copy, 1);

        PBufferBarrier barrier = {
            .buffer = slot->readback,
            .src    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
            .dst    = {    P_PIPELINE_STAGE_HOST_BIT,      P_MEMORY_ACCESS_HOST_READ_BIT},
        };

        pigment_cmd_buffer_barriers(test->pigment, copy_cmd, &barrier, 1);
        pigment_end_recording(test->pigment, copy_cmd);

        test->recorded[recorder->index]++;
        test->stage = 1;
        SDL_SignalSemaphore(test->submit);
    }
}

static void expect_submit_failure(Test* test, const PSubmit* submits, uint32_t count)
{
    PSubmitHandle handles[MAX_SUBMITS];
    CHECK(count <= MAX_SUBMITS);
    memset(handles, 0xff, sizeof(handles));
    uint32_t previous = atomic_load_explicit(&test->submit_errors, memory_order_relaxed);
    atomic_store_explicit(&test->expect_submit_error, P_TRUE, memory_order_relaxed);
    CHECK(pigment_queue_submit(test->pigment, submits, count, handles) == PIGMENT_ERROR);
    atomic_store_explicit(&test->expect_submit_error, P_FALSE, memory_order_relaxed);
    CHECK(atomic_load_explicit(&test->submit_errors, memory_order_relaxed) == previous + 1);

    for(uint32_t i = 0; i < count; i++)
    {
        CHECK(handles[i].queue == NULL && handles[i].value == 0);
    }
}

static int submit_worker(void* arg)
{
    Test* test                        = arg;
    uint64_t last_values[QUEUE_COUNT] = {0};
    CHECK(SDL_GetCurrentThreadID() != test->main_thread);

    for(;;)
    {
        SDL_WaitSemaphore(test->submit);
        if(test->stop)
        {
            return 0;
        }

        CHECK(test->stage == 1);
        CHECK(SDL_GetCurrentThreadID() != test->recording_thread);
        pigment_destroy_buffer(test->pigment, test->slots[test->slot].source);
        test->slots[test->slot].source = NULL;
        pigment_drain_pending(test->pigment);
        if((test->sequence & 7) == 0)
        {
            SDL_Delay(0);
        }

        SubmitMode mode        = (SubmitMode) (test->sequence % SUBMIT_MODE_COUNT);
        PBool first_cycle      = test->sequence < SUBMIT_MODE_COUNT;
        TestQueue copy_queue   = copy_queue_for_mode(mode);
        PBool separate_present = uses_transfer_queue(mode)
                                 || mode == SUBMIT_SEPARATE_PRESENT_SIGNAL
                                 || mode == SUBMIT_INDEPENDENT_COPY
                                 || pigment_queue_family(test->queues[copy_queue]) != pigment_queue_family(test->queues[GRAPHICS_QUEUE]);

        // Normally render, copy, then an optional empty submit that signals present.
        // The transfer-first scenarios swap the first two submits below.
        PCommandBuffer* cmds[]       = {test->cmd, test->copies[copy_queue_for_mode(mode)][test->slot]};
        PSubmit submits[MAX_SUBMITS] = {
            {
             .queue         = test->queues[GRAPHICS_QUEUE],
             .cmds          = &cmds[0],
             .cmd_count     = 1,
             .frame         = test->frame,
             .wait_acquire  = P_TRUE,
             .acquire_stage = (test->sequence & 1) ? P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : P_PIPELINE_STAGE_NONE,
             },
            {
             .queue          = test->queues[copy_queue],
             .cmds           = &cmds[1],
             .cmd_count      = 1,
             .frame          = test->frame,
             .signal_present = !separate_present,
             },
            {
             .queue          = test->queues[GRAPHICS_QUEUE],
             .frame          = test->frame,
             .signal_present = P_TRUE,
             },
        };

        // Order reuse of our buffers against the previous frame on this slot.
        PSubmitWait reuse[QUEUE_COUNT];
        uint32_t reuse_count = 0;
        for(uint32_t i = 0; i < QUEUE_COUNT; i++)
        {
            PSubmitHandle handle = test->slots[test->slot].handles[i];
            if(handle.value != 0)
            {
                reuse[reuse_count++] = (PSubmitWait) {.handle = handle};
            }
        }

        submits[0].waits      = reuse;
        submits[0].wait_count = reuse_count;
        if(copy_is_independent(mode))
        {
            // The readback runs independently of rendering and the final present signal.
            submits[1].waits      = reuse;
            submits[1].wait_count = reuse_count;
        }

        if(first_cycle && mode == SUBMIT_COMBINED)
        {
            submits[0].wait_acquire   = P_FALSE;
            submits[0].signal_present = P_TRUE;
            expect_submit_failure(test, submits, 1);
            submits[0].signal_present = P_FALSE;
            submits[0].wait_acquire   = P_TRUE;
            submits[1].wait_acquire   = P_TRUE;
            expect_submit_failure(test, submits, 2);
            submits[1].wait_acquire   = P_FALSE;
            submits[0].signal_present = P_TRUE;
            expect_submit_failure(test, submits, 2);
            submits[0].signal_present = P_FALSE;
        }

        uint32_t count = separate_present ? 3 : 2;
        if(mode == SUBMIT_COMBINED)
        {
            count                     = 1;
            submits[0].cmd_count      = sizeof(cmds) / sizeof(cmds[0]);
            submits[0].signal_present = P_TRUE;
        }

        if(mode == SUBMIT_TRANSFER_FIRST_BATCHED || mode == SUBMIT_TRANSFER_FIRST_SPLIT)
        {
            // Transfer work may be tracked before the graphics acquire wait.
            PSubmit graphics          = submits[0];
            submits[0]                = submits[1];
            submits[1]                = graphics;
            submits[1].signal_present = P_TRUE;
            count                     = 2;
        }

        if(first_cycle && mode == SUBMIT_TRANSFER_FIRST_BATCHED)
        {
            // An auxiliary submit must not count as consuming the acquire semaphore.
            submits[1].wait_acquire = P_FALSE;
            expect_submit_failure(test, submits, count);
            submits[1].wait_acquire = P_TRUE;

            if(pigment_queue_family(test->queues[TRANSFER_QUEUE]) != pigment_queue_family(test->queues[GRAPHICS_QUEUE]))
            {
                PSubmit invalid = {.queue = test->queues[TRANSFER_QUEUE], .frame = test->frame, .wait_acquire = P_TRUE};
                expect_submit_failure(test, &invalid, 1);
                invalid.wait_acquire   = P_FALSE;
                invalid.signal_present = P_TRUE;
                expect_submit_failure(test, &invalid, 1);
            }
        }

        if(first_cycle && mode == SUBMIT_TRANSFER_AFTER_ACQUIRE)
        {
            // The intermediate transfer submit must not hide a prior acquire wait.
            submits[2].wait_acquire = P_TRUE;
            expect_submit_failure(test, submits, count);
            submits[2].wait_acquire = P_FALSE;
        }

        PSubmitHandle handles[MAX_SUBMITS];
        if(mode == SUBMIT_TRANSFER_FIRST_SPLIT)
        {
            CHECK(pigment_queue_submit(test->pigment, submits, 1, handles) == PIGMENT_SUCCESS);
            if(first_cycle && mode == SUBMIT_TRANSFER_FIRST_SPLIT)
            {
                submits[1].wait_acquire = P_FALSE;
                expect_submit_failure(test, &submits[1], 1);
                submits[1].wait_acquire = P_TRUE;
            }
            CHECK(pigment_queue_submit(test->pigment, &submits[1], 1, &handles[1]) == PIGMENT_SUCCESS);
        }
        else if(mode == SUBMIT_SPLIT_GRAPHICS || mode == SUBMIT_SPLIT_QUEUES || mode == SUBMIT_SEPARATE_PRESENT_SIGNAL || mode == SUBMIT_TRANSFER_DEPENDENCY)
        {
            CHECK(pigment_queue_submit(test->pigment, submits, 1, handles) == PIGMENT_SUCCESS);
            PSubmitWait wait      = {.handle = handles[0], .stage = P_PIPELINE_STAGE_TRANSFER_BIT};
            submits[1].waits      = &wait;
            submits[1].wait_count = 1;
            CHECK(pigment_queue_submit(test->pigment, &submits[1], 1, &handles[1]) == PIGMENT_SUCCESS);
            if(count == 3)
            {
                PSubmitWait final_wait = {.handle = handles[1]};
                submits[2].waits       = &final_wait;
                submits[2].wait_count  = 1;
                CHECK(pigment_queue_submit(test->pigment, &submits[2], 1, &handles[2]) == PIGMENT_SUCCESS);
            }
        }
        else
        {
            CHECK(pigment_queue_submit(test->pigment, submits, count, handles) == PIGMENT_SUCCESS);
        }

        memset(test->handles, 0, sizeof(test->handles));
        for(uint32_t i = 0; i < count; i++)
        {
            uint32_t queue = 0;
            while(queue < QUEUE_COUNT && handles[i].queue != test->queues[queue])
            {
                queue++;
            }

            CHECK(queue < QUEUE_COUNT && handles[i].value > last_values[queue]);
            last_values[queue]   = handles[i].value;
            test->handles[queue] = handles[i];
        }

        if(first_cycle && mode == SUBMIT_COMBINED)
        {
            expect_submit_failure(test, submits, 1);
        }

        if((test->sequence & 1) != 0)
        {
            pigment_present_frame(test->pigment, test->frame);
        }

        test->mode_submitted[mode]++;
        test->submitted++;
        test->stage = 2;
        SDL_SignalSemaphore(test->done);
    }
}

static int watchdog(void* arg)
{
    Test* test        = arg;
    uint32_t previous = 0;
    Uint64 progress   = SDL_GetTicks();

    while(!atomic_load_explicit(&test->watchdog_stop, memory_order_relaxed))
    {
        SDL_Delay(100);
        uint32_t completed = atomic_load_explicit(&test->completed, memory_order_relaxed);
        if(completed != previous)
        {
            previous = completed;
            progress = SDL_GetTicks();
        }

        if(SDL_GetTicks() - progress > STALL_TIMEOUT_MS)
        {
            fprintf(stderr, "FAIL: no progress for %d ms after %u frames\n", STALL_TIMEOUT_MS, completed);
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

int main(void)
{
    // Set the native environment before SDL or Vulkan can start any threads.
    CHECK(SDL_setenv_unsafe("VK_LAYER_VALIDATE_SYNC", "1", 1) == 0);
    CHECK(SDL_setenv_unsafe("VK_LAYER_THREAD_SAFETY", "1", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO));

    uint32_t frame_count = env_count("PIGMENT_TEST_FRAMES", 10000, 10000000);
    uint32_t slot_count  = env_count("PIGMENT_TEST_SLOTS", 3, MAX_SLOTS);
    CHECK(frame_count >= SUBMIT_MODE_COUNT);
    Test test          = {.main_thread = SDL_GetCurrentThreadID()};
    SDL_Window* window = SDL_CreateWindow("test_frame_threads", 320, 240, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    CHECK(window != NULL);

    PAppInfo app = {
        .app_name    = "test_frame_threads",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PigmentLoggerCreateInfo logger = {
        .user_data       = &test,
        .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
        .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
        .callback        = log_callback,
    };

    PQueueRequest queues[] = {
        {.required = P_QUEUE_GRAPHICS_BIT, .count = env_count("PIGMENT_TEST_QUEUES", 2, 2), .priority = 1.0f},
        {.required = P_QUEUE_TRANSFER_BIT, .count = 1, .priority = 1.0f},
    };

    PigmentConfig config = {
        .max_frames_in_flight = slot_count,
        .loggers              = &logger,
        .logger_count         = 1,
        .enable_validation    = P_TRUE,
        .queue_requests       = queues,
        .queue_request_count  = sizeof(queues) / sizeof(queues[0]),
    };

    test.pigment = init_pigment(&app, &config);
    CHECK(test.pigment != NULL);
    CHECK(atomic_load_explicit(&test.errors, memory_order_relaxed) == 0);

    uint32_t queue_count = pigment_request_queue_count(test.pigment, 0);
    CHECK(queue_count > 0);
    test.queues[GRAPHICS_QUEUE] = pigment_request_queue(test.pigment, 0, 0);
    test.queues[TRANSFER_QUEUE] = pigment_request_queue_count(test.pigment, 1) > 0
                                      ? pigment_request_queue(test.pigment, 1, 0)
                                      : test.queues[GRAPHICS_QUEUE];
    test.queues[COPY_QUEUE]     = queue_count > 1
                                      ? pigment_request_queue(test.pigment, 0, 1)
                                      : test.queues[TRANSFER_QUEUE];

    uint32_t used_queues   = 0;
    uint32_t used_families = 0;
    for(uint32_t role = 0; role < QUEUE_COUNT; role++)
    {
        PBool new_queue  = P_TRUE;
        PBool new_family = P_TRUE;
        for(uint32_t previous = 0; previous < role; previous++)
        {
            new_queue &= test.queues[role] != test.queues[previous];
            new_family &= pigment_queue_family(test.queues[role]) != pigment_queue_family(test.queues[previous]);
        }
        used_queues += new_queue;
        used_families += new_family;
    }

    printf("Queues used=%u families=%u (graphics=%u copy=%u transfer=%u)\n", used_queues, used_families, pigment_queue_family(test.queues[GRAPHICS_QUEUE]), pigment_queue_family(test.queues[COPY_QUEUE]), pigment_queue_family(test.queues[TRANSFER_QUEUE]));

    if(used_queues == 1)
    {
        printf("All scenarios use one queue. Cross-queue synchronization is not covered.\n");
    }

    PCommandPool* pools[QUEUE_COUNT];
    for(uint32_t role = 0; role < QUEUE_COUNT; role++)
    {
        PCommandPoolDesc pool_desc = {
            .queue_family = pigment_queue_family(test.queues[role]),
            .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
            .name         = "frame_threads_pool",
        };

        pools[role] = pigment_create_command_pool(test.pigment, &pool_desc);
        CHECK(pools[role] != NULL);
        CHECK(pigment_create_command_buffers(test.pigment, pools[role], P_COMMAND_BUFFER_LEVEL_PRIMARY, slot_count, test.copies[role]) == PIGMENT_SUCCESS);
    }
    CHECK(pigment_create_command_buffers(test.pigment, pools[GRAPHICS_QUEUE], P_COMMAND_BUFFER_LEVEL_PRIMARY, slot_count, test.cmds) == PIGMENT_SUCCESS);

    PWindowHandles handles   = pigment_sdl_get_window_handles(window);
    PSwapchainDesc swapchain = {.width = 320, .height = 240, .present_mode = P_PRESENT_MODE_IMMEDIATE};
    test.renderer            = pigment_renderer_create(test.pigment, &handles, &swapchain);
    CHECK(test.renderer != NULL);

    for(uint32_t i = 0; i < slot_count; i++)
    {
        PBufferDesc desc = {
            .size               = WORDS * sizeof(uint32_t),
            .usage              = P_BUFFER_USAGE_TRANSFER_DST,
            .memory             = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT},
            .name               = "frame_threads_readback",
            .shared_queues      = test.queues,
            .shared_queue_count = QUEUE_COUNT,
        };

        test.slots[i].readback = pigment_create_buffer(test.pigment, &desc);
        CHECK(test.slots[i].readback != NULL);
        CHECK(pigment_buffer_mapped(test.slots[i].readback) != NULL);

        desc.usage                 = P_BUFFER_USAGE_TRANSFER_SRC | P_BUFFER_USAGE_TRANSFER_DST;
        desc.memory                = (PMemoryRequest) {.required = P_MEMORY_DEVICE_LOCAL_BIT};
        desc.name                  = "frame_threads_intermediate";
        test.slots[i].intermediate = pigment_create_buffer(test.pigment, &desc);
        CHECK(test.slots[i].intermediate != NULL);
    }

    test.submit = SDL_CreateSemaphore(0);
    test.done   = SDL_CreateSemaphore(0);
    CHECK(test.submit != NULL && test.done != NULL);

    SDL_Thread* threads[RECORDERS];
    Recorder recorders[RECORDERS];
    for(uint32_t i = 0; i < RECORDERS; i++)
    {
        test.record[i] = SDL_CreateSemaphore(0);
        CHECK(test.record[i] != NULL);
        recorders[i] = (Recorder) {.test = &test, .index = i};
        threads[i]   = SDL_CreateThread(record_worker, "frame_recorder", &recorders[i]);
        CHECK(threads[i] != NULL);
    }

    SDL_Thread* submit_thread   = SDL_CreateThread(submit_worker, "frame_submit", &test);
    SDL_Thread* watchdog_thread = SDL_CreateThread(watchdog, "frame_watchdog", &test);
    CHECK(submit_thread != NULL && watchdog_thread != NULL);

    Uint64 start             = SDL_GetTicks();
    uint32_t recreations     = 0;
    uint32_t acquire_retries = 0;

    for(uint32_t sequence = 0; sequence < frame_count; sequence++)
    {
        SDL_PumpEvents();
        if(sequence > 0 && sequence % 257 == 0)
        {
            int width = (sequence & 1) ? 352 : 320;
            CHECK(SDL_SetWindowSize(window, width, 240));
            CHECK(SDL_SyncWindow(window));
            int height;
            CHECK(SDL_GetWindowSizeInPixels(window, &width, &height));
            pigment_renderer_resize(test.renderer, (uint32_t) width, (uint32_t) height);
        }

        for(;;)
        {
            pigment_wait_frame_ready(test.pigment, test.renderer, sequence % slot_count);
            pigment_drain_pending(test.pigment);
            const uint64_t timeouts[] = {0, 1000000, UINT64_MAX};
            uint64_t timeout          = timeouts[sequence % 3];
            uint32_t slot             = sequence % slot_count;
            PResult result            = pigment_begin_frame_context(test.pigment, test.renderer, slot, timeout, &test.frame);
            if(result == PIGMENT_SUCCESS)
            {
                CHECK(test.frame != NULL);
                break;
            }

            CHECK(test.frame == NULL);
            if(result == PIGMENT_NOT_READY || result == PIGMENT_TIMEOUT)
            {
                CHECK((timeout == 0 && result == PIGMENT_NOT_READY) || (timeout != 0 && result == PIGMENT_TIMEOUT));
                acquire_retries++;
                SDL_PumpEvents();
                SDL_Delay(1);
                continue;
            }

            CHECK(result == PIGMENT_RECREATE_REQUIRED);
            CHECK(pigment_recreate_swapchain(test.pigment, test.renderer) == PIGMENT_SUCCESS);
            recreations++;
        }

        test.slot = pigment_frame_slot(test.frame);
        CHECK(test.slot == sequence % slot_count);
        test.cmd   = test.cmds[test.slot];
        test.image = pigment_frame_image(test.frame);
        CHECK(test.cmd != NULL && test.image != NULL);

        Slot* slot = &test.slots[test.slot];
        verify_slot(&test, slot);

        PBufferDesc source_desc = {
            .size               = WORDS * sizeof(uint32_t),
            .usage              = P_BUFFER_USAGE_TRANSFER_SRC,
            .memory             = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT},
            .name               = "frame_threads_source",
            .shared_queues      = test.queues,
            .shared_queue_count = QUEUE_COUNT,
        };

        slot->source = pigment_create_buffer(test.pigment, &source_desc);
        CHECK(slot->source != NULL);
        uint32_t* words = pigment_buffer_mapped(slot->source);
        CHECK(words != NULL);
        for(uint32_t i = 0; i < WORDS; i++)
        {
            words[i] = pattern(sequence, i);
        }

        pigment_buffer_flush(test.pigment, slot->source, 0, WORDS * sizeof(uint32_t));

        test.sequence = sequence;
        test.stage    = 0;
        SDL_SignalSemaphore(test.record[sequence % RECORDERS]);

        while(!SDL_WaitSemaphoreTimeout(test.done, 1))
        {
            SDL_PumpEvents();
        }

        CHECK(test.stage == 2);
        if((sequence & 1) == 0)
        {
            pigment_present_frame(test.pigment, test.frame);
        }

        CHECK(atomic_load_explicit(&test.errors, memory_order_relaxed) == 0);

        if(sequence == 0)
        {
            PSubmit inactive = {.frame = test.frame, .wait_acquire = P_TRUE, .signal_present = P_TRUE};
            expect_submit_failure(&test, &inactive, 1);

            PFrame* invalid = test.frame;
            CHECK(pigment_begin_frame_context(NULL, test.renderer, 0, 0, &invalid) == PIGMENT_ERROR);
            CHECK(invalid == NULL);
            invalid = test.frame;
            CHECK(pigment_begin_frame_context(test.pigment, NULL, 0, 0, &invalid) == PIGMENT_ERROR);
            CHECK(invalid == NULL);
            CHECK(pigment_begin_frame_context(test.pigment, test.renderer, 0, 0, NULL) == PIGMENT_ERROR);
        }

        memcpy(slot->handles, test.handles, sizeof(slot->handles));
        slot->sequence = sequence;
        slot->pending  = P_TRUE;
        atomic_store_explicit(&test.completed, sequence + 1, memory_order_relaxed);
    }

    test.stop = P_TRUE;
    for(uint32_t i = 0; i < RECORDERS; i++)
    {
        SDL_SignalSemaphore(test.record[i]);
        SDL_WaitThread(threads[i], NULL);
        CHECK(test.recorded[i] == frame_count / RECORDERS + (i < frame_count % RECORDERS));
        SDL_DestroySemaphore(test.record[i]);
    }

    SDL_SignalSemaphore(test.submit);
    SDL_WaitThread(submit_thread, NULL);
    CHECK(test.submitted == frame_count);
    SDL_DestroySemaphore(test.submit);
    SDL_DestroySemaphore(test.done);

    pigment_wait_idle(test.pigment);
    for(uint32_t i = 0; i < slot_count; i++)
    {
        verify_slot(&test, &test.slots[i]);
        pigment_destroy_buffer(test.pigment, test.slots[i].source);
        pigment_destroy_buffer(test.pigment, test.slots[i].intermediate);
        pigment_destroy_buffer(test.pigment, test.slots[i].readback);
    }

    for(uint32_t mode = 0; mode < SUBMIT_MODE_COUNT; mode++)
    {
        uint32_t expected = frame_count / SUBMIT_MODE_COUNT + (mode < frame_count % SUBMIT_MODE_COUNT);
        CHECK(test.mode_submitted[mode] == expected);
        CHECK(test.mode_verified[mode] == expected);
        printf("%s: submitted=%u readbacks=%u\n", mode_names[mode], test.mode_submitted[mode], test.mode_verified[mode]);
    }

    pigment_renderer_destroy(test.pigment, test.renderer);
    for(uint32_t role = 0; role < QUEUE_COUNT; role++)
    {
        pigment_destroy_command_pool(test.pigment, pools[role]);
    }
    destroy_pigment(test.pigment);
    CHECK(atomic_load_explicit(&test.errors, memory_order_relaxed) == 0);

    atomic_store_explicit(&test.watchdog_stop, P_TRUE, memory_order_relaxed);
    SDL_WaitThread(watchdog_thread, NULL);

    printf("OK frames=%u slots=%u queues_used=%u recorders=%d recreations=%u acquire_retries=%u elapsed=%.3f s\n", frame_count, slot_count, used_queues, RECORDERS, recreations, acquire_retries, (double) (SDL_GetTicks() - start) / 1000.0);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
