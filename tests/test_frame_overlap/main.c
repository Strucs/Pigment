#include <pigment/pigment.h>
#include <pigment/pigment_sdl.h>

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 3

#define CHECK(condition)                                                         \
    do                                                                           \
    {                                                                            \
        if(!(condition))                                                         \
        {                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                  \
        }                                                                        \
    }                                                                            \
    while(0)

typedef struct Worker {
    Pigment* pigment;
    PCommandPool* pool;
    PCommandBuffer* cmd;
    PFrame* frame;
    PImage* image;
    SDL_Semaphore* start;
    SDL_Semaphore* done;
    uint32_t slot;
    uint32_t recorded;
    PBool stop;
} Worker;

static _Atomic uint32_t errors;
static _Atomic uint64_t last_progress;
static _Atomic PBool watchdog_stop;

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    (void) user_data;
    if((severity & PIGMENT_LOG_ERROR_BIT)
       || ((severity & PIGMENT_LOG_WARN_BIT) && (type & PIGMENT_LOG_TYPE_VALIDATION_BIT))
       || (record->message != NULL && strstr(record->message, "Disabling validation") != NULL))
    {
        atomic_fetch_add_explicit(&errors, 1, memory_order_relaxed);
    }

    pigment_default_log_callback(severity, type, record, NULL);
}

static int record_worker(void* arg)
{
    Worker* worker = arg;
    for(;;)
    {
        SDL_WaitSemaphore(worker->start);
        if(worker->stop)
        {
            return 0;
        }

        CHECK(pigment_frame_slot(worker->frame) == worker->slot);
        CHECK(pigment_frame_image(worker->frame) == worker->image);
        pigment_begin_recording(worker->pigment, worker->cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

        PSwapchainPassDesc pass = {
            .clear_color = {0.1f * (float) worker->slot, 0.2f, 0.4f, 1.0f},
        };

        pigment_cmd_begin_swapchain_pass(worker->pigment, worker->cmd, worker->frame, &pass);
        pigment_cmd_end_swapchain_pass(worker->cmd, worker->frame);
        pigment_end_recording(worker->pigment, worker->cmd);
        worker->recorded++;
        SDL_SignalSemaphore(worker->done);
    }
}

static int watchdog(void* arg)
{
    (void) arg;
    while(!atomic_load_explicit(&watchdog_stop, memory_order_relaxed))
    {
        SDL_Delay(100);
        CHECK(SDL_GetTicks() - atomic_load_explicit(&last_progress, memory_order_relaxed) < 30000);
    }

    return 0;
}

int main(void)
{
    CHECK(SDL_setenv_unsafe("VK_VALIDATION_VALIDATE_SYNC", "true", 1) == 0);
    CHECK(SDL_setenv_unsafe("VK_VALIDATION_THREAD_SAFETY", "true", 1) == 0);
    CHECK(SDL_Init(SDL_INIT_VIDEO));

    SDL_Window* window = SDL_CreateWindow("test_frame_overlap", 320, 240, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    CHECK(window != NULL);

    PAppInfo app                   = {.app_name = "test_frame_overlap", .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)};
    PigmentLoggerCreateInfo logger = {
        .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
        .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
        .callback        = log_callback,
    };

    PQueueRequest queue_request = {.required = P_QUEUE_GRAPHICS_BIT, .count = 2, .priority = 1.0f};
    PigmentConfig config        = {
        .max_frames_in_flight = SLOTS,
        .loggers              = &logger,
        .logger_count         = 1,
        .enable_validation    = P_TRUE,
        .queue_requests       = &queue_request,
        .queue_request_count  = 1,
    };

    Pigment* pigment = init_pigment(&app, &config);

    CHECK(pigment != NULL);
    uint32_t queue_count = pigment_request_queue_count(pigment, 0);
    CHECK(queue_count > 0);
    PDeviceQueue* queues[2];
    queues[0] = pigment_request_queue(pigment, 0, 0);
    queues[1] = queues[0];
    for(uint32_t i = 1; i < queue_count; i++)
    {
        PDeviceQueue* candidate = pigment_request_queue(pigment, 0, i);
        if(pigment_queue_family(candidate) == pigment_queue_family(queues[0]))
        {
            queues[1] = candidate;
            break;
        }
    }

    PWindowHandles handles   = pigment_sdl_get_window_handles(window);
    PSwapchainDesc swapchain = {
        .width        = 320,
        .height       = 240,
        .image_count  = SLOTS + 1,
        .present_mode = P_PRESENT_MODE_IMMEDIATE,
        .samples      = P_SAMPLE_COUNT_4,
    };

    PWindowRenderer* renderer = pigment_renderer_create(pigment, &handles, &swapchain);
    CHECK(renderer != NULL);
    PFrame* invalid = (PFrame*) renderer;
    CHECK(pigment_begin_frame_context(pigment, renderer, SLOTS, 0, &invalid) == PIGMENT_ERROR);
    CHECK(invalid == NULL);

    Worker workers[SLOTS] = {0};
    SDL_Thread* threads[SLOTS];
    PSubmitHandle previous[SLOTS] = {0};

    for(uint32_t i = 0; i < SLOTS; i++)
    {
        Worker* worker  = &workers[i];
        worker->pigment = pigment;
        worker->slot    = i;
        worker->start   = SDL_CreateSemaphore(0);
        worker->done    = SDL_CreateSemaphore(0);
        CHECK(worker->start != NULL && worker->done != NULL);

        PCommandPoolDesc pool = {.queue_family = pigment_queue_family(queues[0]), .flags = P_COMMAND_POOL_FLAG_RESET_BUFFER};
        worker->pool          = pigment_create_command_pool(pigment, &pool);
        CHECK(worker->pool != NULL);
        CHECK(pigment_create_command_buffers(pigment, worker->pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &worker->cmd) == PIGMENT_SUCCESS);
        threads[i] = SDL_CreateThread(record_worker, "overlap_recorder", worker);
        CHECK(threads[i] != NULL);
    }

    uint32_t acquired_total = 0;
    uint32_t peak_active    = 0;
    uint32_t recreations    = 0;
    Uint64 progress         = SDL_GetTicks();
    atomic_store_explicit(&last_progress, progress, memory_order_relaxed);
    SDL_Thread* watchdog_thread = SDL_CreateThread(watchdog, "overlap_watchdog", NULL);
    CHECK(watchdog_thread != NULL);

    for(uint32_t batch = 0; batch < 2000; batch++)
    {
        SDL_PumpEvents();
        uint32_t order[SLOTS];
        uint32_t count = 0;

        for(uint32_t i = 0; i < SLOTS; i++)
        {
            uint32_t slot  = (batch + i) % SLOTS;
            Worker* worker = &workers[slot];
            pigment_wait_frame_ready(pigment, renderer, slot);

            PResult result = pigment_begin_frame_context(pigment, renderer, slot, 0, &worker->frame);
            if(result == PIGMENT_RECREATE_REQUIRED || result == PIGMENT_NOT_READY)
            {
                CHECK(worker->frame == NULL);
                break;
            }
            CHECK(result == PIGMENT_SUCCESS);
            CHECK(worker->frame != NULL);
            worker->image = pigment_frame_image(worker->frame);
            CHECK(worker->image != NULL);
            for(uint32_t j = 0; j < count; j++)
            {
                CHECK(worker->frame != workers[order[j]].frame);
                CHECK(worker->image != workers[order[j]].image);
            }
            order[count++] = slot;

            PFrame* busy = worker->frame;
            CHECK(pigment_begin_frame_context(pigment, renderer, slot, UINT64_MAX, &busy) == PIGMENT_NOT_READY);
            CHECK(busy == NULL);
        }

        if(count == 0)
        {
            CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
            CHECK(SDL_GetTicks() - progress < 30000);
            SDL_Delay(1);
            continue;
        }

        if(count > peak_active)
        {
            peak_active = count;
        }

        PBool resize = batch % 127 == 0;
        if(resize)
        {
            int width = (batch & 1) ? 352 : 320;
            CHECK(SDL_SetWindowSize(window, width, 240));
            CHECK(SDL_SyncWindow(window));
            pigment_renderer_resize(renderer, (uint32_t) width, 240);
            pigment_set_sample_count(renderer, (batch & 1) ? P_SAMPLE_COUNT_1 : P_SAMPLE_COUNT_4);
            CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_NOT_READY);
        }

        for(uint32_t i = 0; i < count; i++)
        {
            SDL_SignalSemaphore(workers[order[i]].start);
        }

        for(uint32_t i = 0; i < count; i++)
        {
            while(!SDL_WaitSemaphoreTimeout(workers[order[i]].done, 1))
            {
                SDL_PumpEvents();
                CHECK(SDL_GetTicks() - progress < 30000);
            }
        }

        // Submit in reverse acquisition order, on alternating graphics queues.
        PSubmit submits[SLOTS];
        PSubmitWait waits[SLOTS];
        PSubmitHandle submitted[SLOTS];

        for(uint32_t i = 0; i < count; i++)
        {
            uint32_t slot  = order[count - i - 1];
            Worker* worker = &workers[slot];
            waits[i]       = (PSubmitWait) {.handle = previous[slot]};
            submits[i]     = (PSubmit) {
                .queue          = queues[(batch + slot) % 2],
                .cmds           = &worker->cmd,
                .cmd_count      = 1,
                .waits          = &waits[i],
                .wait_count     = previous[slot].value != 0,
                .frame          = worker->frame,
                .wait_acquire   = P_TRUE,
                .signal_present = P_TRUE,
            };
        }

        CHECK(pigment_queue_submit(pigment, submits, count, submitted) == PIGMENT_SUCCESS);

        for(uint32_t i = 0; i < count; i++)
        {
            uint32_t slot  = order[count - i - 1];
            Worker* worker = &workers[slot];
            CHECK(pigment_frame_slot(worker->frame) == slot);
            pigment_present_frame(pigment, worker->frame);
            CHECK(pigment_frame_slot(worker->frame) == UINT32_MAX);
            CHECK(pigment_frame_image(worker->frame) == NULL);
            previous[slot] = submitted[i];
            if(resize && i + 1 < count)
            {
                CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_NOT_READY);
            }
        }

        SDL_PumpEvents();
        CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
        recreations += resize;
        acquired_total += count;
        progress = SDL_GetTicks();
        atomic_store_explicit(&last_progress, progress, memory_order_relaxed);
        CHECK(atomic_load_explicit(&errors, memory_order_relaxed) == 0);
    }

    uint32_t recorded = 0;
    for(uint32_t i = 0; i < SLOTS; i++)
    {
        workers[i].stop = P_TRUE;
        SDL_SignalSemaphore(workers[i].start);
        SDL_WaitThread(threads[i], NULL);
        recorded += workers[i].recorded;
        pigment_wait_frame_ready(pigment, renderer, i);
        CHECK(pigment_submit_complete(pigment, previous[i]));
        pigment_destroy_command_pool(pigment, workers[i].pool);
        SDL_DestroySemaphore(workers[i].start);
        SDL_DestroySemaphore(workers[i].done);
    }

    CHECK(recorded == acquired_total);
    CHECK(peak_active > 1);

    pigment_renderer_destroy(pigment, renderer);
    destroy_pigment(pigment);
    CHECK(atomic_load_explicit(&errors, memory_order_relaxed) == 0);
    atomic_store_explicit(&watchdog_stop, P_TRUE, memory_order_relaxed);
    SDL_WaitThread(watchdog_thread, NULL);
    printf("OK frames=%u peak_active=%u recreations=%u\n", acquired_total, peak_active, recreations);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
