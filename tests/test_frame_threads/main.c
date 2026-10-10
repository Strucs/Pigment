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

typedef struct Slot {
    PBuffer* source;
    PBuffer* readback;
    PSubmitHandle handle;
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

    // One CPU frame at a time. Semaphore handoffs protect these fields.
    PFrame* frame;
    PCommandBuffer* cmd;
    PImage* image;
    PSubmitHandle handle;
    uint32_t slot;
    uint32_t sequence;
    uint32_t stage;
    uint32_t recorded[RECORDERS];
    uint32_t submitted;
    SDL_ThreadID recording_thread;
    PBool stop;

    _Atomic uint32_t completed;
    _Atomic int errors;
    _Atomic PBool watchdog_stop;
} Test;

typedef struct Recorder {
    Test* test;
    uint32_t index;
} Recorder;

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    Test* test = user_data;
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

    CHECK(pigment_submit_complete(test->pigment, slot->handle));
    pigment_buffer_invalidate(test->pigment, slot->readback, 0, WORDS * sizeof(uint32_t));

    const uint32_t* words = pigment_buffer_mapped(slot->readback);
    for(uint32_t i = 0; i < WORDS; i++)
    {
        CHECK(words[i] == pattern(slot->sequence, i));
    }

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
        pigment_cmd_copy_buffer(test->pigment, test->cmd, slot->source, slot->readback, &copy, 1);

        PBufferBarrier barrier = {
            .buffer = slot->readback,
            .src    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
            .dst    = {    P_PIPELINE_STAGE_HOST_BIT,      P_MEMORY_ACCESS_HOST_READ_BIT},
        };

        pigment_cmd_buffer_barriers(test->pigment, test->cmd, &barrier, 1);
        pigment_end_recording(test->pigment, test->cmd);

        test->recorded[recorder->index]++;
        test->stage = 1;
        SDL_SignalSemaphore(test->submit);
    }
}

static int submit_worker(void* arg)
{
    Test* test          = arg;
    uint64_t last_value = 0;
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
        if((test->sequence & 7) == 0)
        {
            SDL_Delay(0);
        }

        test->handle = pigment_queue_submit_frame_context(test->pigment, test->frame, test->cmd, NULL, NULL, 0);
        CHECK(test->handle.queue != NULL && test->handle.value > last_value);
        last_value = test->handle.value;

        if((test->sequence & 1) != 0)
        {
            pigment_present_frame(test->pigment, test->frame);
        }

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
    CHECK(SDL_setenv_unsafe("VK_VALIDATION_VALIDATE_SYNC", "true", 1) == 0);
    CHECK(SDL_setenv_unsafe("VK_VALIDATION_THREAD_SAFETY", "true", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO));

    uint32_t frame_count = env_count("PIGMENT_TEST_FRAMES", 10000, 10000000);
    uint32_t slot_count  = env_count("PIGMENT_TEST_SLOTS", 3, MAX_SLOTS);
    Test test            = {.main_thread = SDL_GetCurrentThreadID()};
    SDL_Window* window   = SDL_CreateWindow("test_frame_threads", 320, 240, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    CHECK(window != NULL);

    PAppInfo app                   = {.app_name = "test_frame_threads", .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)};
    PigmentLoggerCreateInfo logger = {
        .user_data       = &test,
        .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
        .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
        .callback        = log_callback,
    };

    PigmentConfig config = {
        .max_frames_in_flight = slot_count,
        .loggers              = &logger,
        .logger_count         = 1,
        .enable_validation    = P_TRUE,
    };

    test.pigment = init_pigment(&app, &config);
    CHECK(test.pigment != NULL);
    CHECK(atomic_load_explicit(&test.errors, memory_order_relaxed) == 0);

    PCommandPoolDesc pool_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(test.pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
        .name         = "frame_threads_pool",
    };

    PCommandPool* pool = pigment_create_command_pool(test.pigment, &pool_desc);
    CHECK(pool != NULL);
    CHECK(pigment_create_command_buffers(test.pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, slot_count, test.cmds) == PIGMENT_SUCCESS);

    PWindowHandles handles   = pigment_sdl_get_window_handles(window);
    PSwapchainDesc swapchain = {.width = 320, .height = 240, .present_mode = P_PRESENT_MODE_IMMEDIATE};
    test.renderer            = pigment_renderer_create(test.pigment, &handles, &swapchain);
    CHECK(test.renderer != NULL);

    for(uint32_t i = 0; i < slot_count; i++)
    {
        PBufferDesc desc = {
            .size   = WORDS * sizeof(uint32_t),
            .usage  = P_BUFFER_USAGE_TRANSFER_SRC,
            .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT},
            .name   = "frame_threads_source",
        };

        test.slots[i].source   = pigment_create_buffer(test.pigment, &desc);
        desc.usage             = P_BUFFER_USAGE_TRANSFER_DST;
        desc.name              = "frame_threads_readback";
        test.slots[i].readback = pigment_create_buffer(test.pigment, &desc);
        CHECK(test.slots[i].source != NULL && test.slots[i].readback != NULL);
        CHECK(pigment_buffer_mapped(test.slots[i].source) != NULL);
        CHECK(pigment_buffer_mapped(test.slots[i].readback) != NULL);
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

    Uint64 start         = SDL_GetTicks();
    uint32_t recreations = 0;

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
            pigment_wait_frame_ready(test.pigment, test.renderer);
            test.frame = pigment_begin_frame_context(test.pigment, test.renderer);
            if(test.frame != NULL)
            {
                break;
            }
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

        uint32_t* words = pigment_buffer_mapped(slot->source);
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

        slot->handle   = test.handle;
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
        pigment_destroy_buffer(test.pigment, test.slots[i].readback);
    }

    pigment_renderer_destroy(test.pigment, test.renderer);
    pigment_destroy_command_pool(test.pigment, pool);
    destroy_pigment(test.pigment);
    CHECK(atomic_load_explicit(&test.errors, memory_order_relaxed) == 0);

    atomic_store_explicit(&test.watchdog_stop, P_TRUE, memory_order_relaxed);
    SDL_WaitThread(watchdog_thread, NULL);

    printf("OK frames=%u slots=%u recorders=%d recreations=%u elapsed=%.3f s\n", frame_count, slot_count, RECORDERS, recreations, (double) (SDL_GetTicks() - start) / 1000.0);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
