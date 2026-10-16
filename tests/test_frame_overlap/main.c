#include <pigment/pigment.h>
#include <pigment/pigment_sdl.h>

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 3
#define GRAPHICS_QUEUES 2
#define TRANSFER_QUEUE GRAPHICS_QUEUES
#define QUEUE_COUNT (GRAPHICS_QUEUES + 1)

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
    PCommandPool* pools[QUEUE_COUNT];
    PCommandBuffer* cmds[QUEUE_COUNT];
    PCommandBuffer* cmd;
    PFrame* frame;
    PImage* image;
    SDL_Semaphore* start;
    SDL_Semaphore* done;
    uint32_t slot;
    uint32_t recorded;
    uint32_t width;
    uint32_t height;
    PBool stop;
} Worker;

static uint32_t recreations;
static _Atomic uint32_t errors;
static _Atomic uint32_t expected_errors;
static _Atomic PBool expect_error;
static _Atomic uint64_t last_progress;
static _Atomic PBool watchdog_stop;

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    (void) user_data;
    if(atomic_load_explicit(&expect_error, memory_order_relaxed)
       && (severity & PIGMENT_LOG_ERROR_BIT) && type == PIGMENT_LOG_TYPE_GENERAL_BIT)
    {
        atomic_fetch_add_explicit(&expected_errors, 1, memory_order_relaxed);
        return;
    }

    if((severity & PIGMENT_LOG_ERROR_BIT)
       || ((severity & PIGMENT_LOG_WARN_BIT) && (type & PIGMENT_LOG_TYPE_VALIDATION_BIT))
       || (record->message != NULL && strstr(record->message, "Disabling validation") != NULL))
    {
        atomic_fetch_add_explicit(&errors, 1, memory_order_relaxed);
    }

    pigment_default_log_callback(severity, type, record, NULL);
}

static void on_recreate(Pigment* pigment, const PSwapchainRecreateEvent* event, void* user_data)
{
    (void) pigment;
    (void) event;
    (void) user_data;
    recreations++;
}

static void test_non_graphics_pass(Worker* worker, PDeviceQueue* queue)
{
    if(pigment_queue_flags(queue) & P_QUEUE_GRAPHICS_BIT)
    {
        printf("No non-graphics queue available for the pass rejection check.\n");
        return;
    }

    PCommandBuffer* cmd = worker->cmds[TRANSFER_QUEUE];
    pigment_begin_recording(worker->pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
    uint32_t before = atomic_load_explicit(&expected_errors, memory_order_relaxed);
    atomic_store_explicit(&expect_error, P_TRUE, memory_order_relaxed);
    pigment_cmd_begin_swapchain_pass(worker->pigment, cmd, worker->frame, NULL);
    pigment_cmd_end_swapchain_pass(cmd, worker->frame);
    pigment_end_recording(worker->pigment, cmd);

    PSubmit submit       = {.queue = queue, .cmds = &cmd, .cmd_count = 1};
    PSubmitHandle handle = {0};
    CHECK(pigment_queue_submit(worker->pigment, &submit, 1, &handle) == PIGMENT_ERROR);
    atomic_store_explicit(&expect_error, P_FALSE, memory_order_relaxed);
    CHECK(atomic_load_explicit(&expected_errors, memory_order_relaxed) == before + 2);
    CHECK(atomic_load_explicit(&errors, memory_order_relaxed) == 0);
    CHECK(handle.queue == NULL && handle.value == 0);
    pigment_reset_command_pool(worker->pigment, worker->pools[TRANSFER_QUEUE]);
    printf("Non-graphics swapchain pass rejected before Vulkan recording.\n");
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
        CHECK(pigment_image_width(worker->image) == worker->width);
        CHECK(pigment_image_height(worker->image) == worker->height);
        pigment_begin_recording(worker->pigment, worker->cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

        PSwapchainPassDesc pass = {
            .clear_color = {0.1f * (float) worker->slot, 0.2f, 0.4f, 1.0f},
        };

        pigment_cmd_begin_swapchain_pass(worker->pigment, worker->cmd, worker->frame, &pass);
        pigment_cmd_end_swapchain_pass(worker->cmd, worker->frame);
        pigment_end_recording(worker->pigment, worker->cmd);

        // Access the shared swapchain image from the auxiliary family too.
        PCommandBuffer* auxiliary = worker->cmds[TRANSFER_QUEUE];
        pigment_begin_recording(worker->pigment, auxiliary, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

        PImageBarrier barrier = {
            .image      = worker->image,
            .old_layout = P_IMAGE_LAYOUT_PRESENT,
            .new_layout = P_IMAGE_LAYOUT_GENERAL,
            .src        = {            P_PIPELINE_STAGE_NONE,                                               P_MEMORY_ACCESS_NONE},
            .dst        = {P_PIPELINE_STAGE_ALL_COMMANDS_BIT, P_MEMORY_ACCESS_MEMORY_READ_BIT | P_MEMORY_ACCESS_MEMORY_WRITE_BIT},
        };

        pigment_cmd_image_barriers(worker->pigment, auxiliary, &barrier, 1);
        barrier.old_layout = P_IMAGE_LAYOUT_GENERAL;
        barrier.new_layout = P_IMAGE_LAYOUT_PRESENT;
        barrier.src        = barrier.dst;
        barrier.dst        = (PSyncFlags) {P_PIPELINE_STAGE_NONE, P_MEMORY_ACCESS_NONE};

        pigment_cmd_image_barriers(worker->pigment, auxiliary, &barrier, 1);
        pigment_end_recording(worker->pigment, auxiliary);
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

    PAppInfo app = {
        .app_name    = "test_frame_overlap",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PigmentLoggerCreateInfo logger = {
        .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
        .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
        .callback        = log_callback,
    };

    PQueueRequest queue_requests[] = {
        {.required = P_QUEUE_GRAPHICS_BIT, .count = GRAPHICS_QUEUES, .priority = 1.0f},
        {.required = P_QUEUE_TRANSFER_BIT,               .count = 1, .priority = 1.0f},
    };

    PigmentConfig config = {
        .max_frames_in_flight = SLOTS,
        .loggers              = &logger,
        .logger_count         = 1,
        .enable_validation    = P_TRUE,
        .queue_requests       = queue_requests,
        .queue_request_count  = sizeof(queue_requests) / sizeof(queue_requests[0]),
    };

    Pigment* pigment = init_pigment(&app, &config);

    CHECK(pigment != NULL);
    uint32_t queue_count = pigment_request_queue_count(pigment, 0);
    CHECK(queue_count > 0);
    PDeviceQueue* queues[QUEUE_COUNT];
    queues[0]                                 = pigment_request_queue(pigment, 0, 0);
    queues[1]                                 = queue_count > 1 ? pigment_request_queue(pigment, 0, 1) : queues[0];
    queues[TRANSFER_QUEUE]                    = pigment_request_queue_count(pigment, 1) > 0 ? pigment_request_queue(pigment, 1, 0) : queues[0];
    uint32_t used_queues                      = queues[0] == queues[1] ? 1 : 2;
    uint32_t queue_submitted[GRAPHICS_QUEUES] = {0};
    uint32_t auxiliary_frames                 = 0;
    printf("Graphics queues used=%u families=(%u,%u) recorders=%u\n", used_queues, pigment_queue_family(queues[0]), pigment_queue_family(queues[1]), SLOTS);

    printf("Auxiliary acquire/present family=%u\n", pigment_queue_family(queues[TRANSFER_QUEUE]));
    PDeviceQueue* shared_queues[QUEUE_COUNT];
    memcpy(shared_queues, queues, sizeof(queues));
    PWindowHandles handles   = pigment_sdl_get_window_handles(window);
    PSwapchainDesc swapchain = {
        .width              = 320,
        .height             = 240,
        .image_count        = SLOTS + 1,
        .present_mode       = P_PRESENT_MODE_IMMEDIATE,
        .samples            = P_SAMPLE_COUNT_4,
        .shared_queues      = shared_queues,
        .shared_queue_count = QUEUE_COUNT,
    };

    PWindowRenderer* renderer = pigment_renderer_create(pigment, &handles, &swapchain);
    CHECK(renderer != NULL);
    // Recreates must use the renderer's copy of the queue list.
    memset(shared_queues, 0, sizeof(shared_queues));
    uint32_t callback = pigment_register_swapchain_recreate(pigment, renderer, on_recreate, NULL);
    PFrame* invalid   = (PFrame*) renderer;
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

        for(uint32_t q = 0; q < QUEUE_COUNT; q++)
        {
            PCommandPoolDesc pool = {.queue_family = pigment_queue_family(queues[q]), .flags = P_COMMAND_POOL_FLAG_RESET_BUFFER};
            worker->pools[q]      = pigment_create_command_pool(pigment, &pool);
            CHECK(worker->pools[q] != NULL);
            CHECK(pigment_create_command_buffers(pigment, worker->pools[q], P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &worker->cmds[q]) == PIGMENT_SUCCESS);
        }
        threads[i] = SDL_CreateThread(record_worker, "overlap_recorder", worker);
        CHECK(threads[i] != NULL);
    }

    uint32_t acquired_total = 0;
    uint32_t peak_active    = 0;
    uint32_t mixed_batches  = 0;
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
            CHECK(pigment_submit_complete(pigment, previous[slot]));

            for(uint32_t q = 0; q < QUEUE_COUNT; q++)
            {
                pigment_reset_command_pool(pigment, worker->pools[q]);
            }
            pigment_drain_pending(pigment);

            PResult result = pigment_begin_frame_context(pigment, renderer, slot, 0, &worker->frame);
            if(result == PIGMENT_RECREATE_REQUIRED || result == PIGMENT_NOT_READY)
            {
                CHECK(worker->frame == NULL);
                break;
            }
            CHECK(result == PIGMENT_SUCCESS);
            CHECK(worker->frame != NULL);
            worker->cmd   = worker->cmds[(batch + slot) % GRAPHICS_QUEUES];
            worker->image = pigment_frame_image(worker->frame);
            CHECK(worker->image != NULL);
            worker->width  = pigment_image_width(worker->image);
            worker->height = pigment_image_height(worker->image);
            if(acquired_total == 0 && count == 0)
            {
                test_non_graphics_pass(worker, queues[TRANSFER_QUEUE]);
            }

            for(uint32_t j = 0; j < count; j++)
            {
                CHECK(worker->frame != workers[order[j]].frame);
                CHECK(worker->image != workers[order[j]].image);
            }
            order[count++] = slot;

            PFrame* busy = worker->frame;
            CHECK(pigment_begin_frame_context(pigment, renderer, slot, UINT64_MAX, &busy) == PIGMENT_NOT_READY);
            CHECK(busy == NULL);

            // Keep frames from successive generations active together.
            if(batch % 31 == 0 && i + 1 < SLOTS)
            {
                pigment_set_sample_count(renderer, i & 1 ? P_SAMPLE_COUNT_4 : P_SAMPLE_COUNT_1);
                CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
                pigment_drain_pending(pigment);
                CHECK(pigment_frame_image(worker->frame) == worker->image);
                mixed_batches += i == 0;
            }
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
            CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
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

        if(resize)
        {
            // Recreate again with recorded commands still waiting for submission.
            pigment_set_sample_count(renderer, (batch & 1) ? P_SAMPLE_COUNT_4 : P_SAMPLE_COUNT_1);
            CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
            pigment_drain_pending(pigment);
        }
        uint32_t before_present = recreations;

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
                .queue          = queues[(batch + slot) % GRAPHICS_QUEUES],
                .cmds           = &worker->cmd,
                .cmd_count      = 1,
                .waits          = &waits[i],
                .wait_count     = previous[slot].value != 0,
                .frame          = worker->frame,
                .wait_acquire   = P_TRUE,
                .signal_present = P_TRUE,
            };
        }

        // Alternate direct rendering with acquire/present signals on another family.
        PBool auxiliary_wsi = (batch & 1) != 0;
        PSubmit auxiliary[SLOTS];
        if(auxiliary_wsi)
        {
            for(uint32_t i = 0; i < count; i++)
            {
                auxiliary[i]                = submits[i];
                auxiliary[i].queue          = queues[TRANSFER_QUEUE];
                auxiliary[i].cmds           = NULL;
                auxiliary[i].cmd_count      = 0;
                auxiliary[i].signal_present = P_FALSE;
                submits[i].wait_acquire     = P_FALSE;
                submits[i].signal_present   = P_FALSE;
            }
            CHECK(pigment_queue_submit(pigment, auxiliary, count, submitted) == PIGMENT_SUCCESS);
            for(uint32_t i = 0; i < count; i++)
            {
                waits[i]              = (PSubmitWait) {.handle = submitted[i]};
                submits[i].wait_count = 1;
            }
        }

        CHECK(pigment_queue_submit(pigment, submits, count, submitted) == PIGMENT_SUCCESS);
        if(auxiliary_wsi)
        {
            for(uint32_t i = 0; i < count; i++)
            {
                waits[i]                    = (PSubmitWait) {.handle = submitted[i]};
                uint32_t slot               = order[count - i - 1];
                auxiliary[i].cmds           = &workers[slot].cmds[TRANSFER_QUEUE];
                auxiliary[i].cmd_count      = 1;
                auxiliary[i].wait_count     = 1;
                auxiliary[i].wait_acquire   = P_FALSE;
                auxiliary[i].signal_present = P_TRUE;
            }
            CHECK(pigment_queue_submit(pigment, auxiliary, count, submitted) == PIGMENT_SUCCESS);
            auxiliary_frames += count;
        }

        for(uint32_t i = 0; i < count; i++)
        {
            uint32_t slot  = order[count - i - 1];
            Worker* worker = &workers[slot];
            CHECK(pigment_frame_slot(worker->frame) == slot);
            pigment_present_frame(pigment, worker->frame);
            CHECK(pigment_frame_slot(worker->frame) == UINT32_MAX);
            CHECK(pigment_frame_image(worker->frame) == NULL);
            previous[slot] = submitted[i];
            queue_submitted[(batch + slot) % GRAPHICS_QUEUES]++;
            if(resize && i + 1 < count)
            {
                CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
            }
        }

        SDL_PumpEvents();
        CHECK(pigment_recreate_swapchain(pigment, renderer) == PIGMENT_SUCCESS);
        if(resize)
        {
            CHECK(recreations == before_present);
        }
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
        CHECK(workers[i].recorded > 0);
        recorded += workers[i].recorded;
        pigment_wait_frame_ready(pigment, renderer, i);
        CHECK(pigment_submit_complete(pigment, previous[i]));
        SDL_DestroySemaphore(workers[i].start);
        SDL_DestroySemaphore(workers[i].done);
    }

    for(uint32_t q = 0; q < GRAPHICS_QUEUES; q++)
    {
        CHECK(queue_submitted[q] > 0);
        printf("graphics[%u]: frames=%u\n", q, queue_submitted[q]);
    }
    CHECK(auxiliary_frames > 0 && auxiliary_frames < acquired_total);
    CHECK(recorded == acquired_total);
    CHECK(peak_active > 1);
    CHECK(mixed_batches > 1);
    pigment_unregister_swapchain_recreate(pigment, callback);

    // Recorded references keep retired generations alive even after renderer destruction.
    pigment_renderer_destroy(pigment, renderer);
    pigment_drain_pending(pigment);
    for(uint32_t i = 0; i < SLOTS; i++)
    {
        for(uint32_t q = 0; q < QUEUE_COUNT; q++)
        {
            pigment_destroy_command_pool(pigment, workers[i].pools[q]);
        }
    }
    destroy_pigment(pigment);
    CHECK(atomic_load_explicit(&errors, memory_order_relaxed) == 0);
    atomic_store_explicit(&watchdog_stop, P_TRUE, memory_order_relaxed);
    SDL_WaitThread(watchdog_thread, NULL);
    printf("OK frames=%u graphics_queues=%u auxiliary_frames=%u peak_active=%u recreations=%u mixed_batches=%u\n", acquired_total, used_queues, auxiliary_frames, peak_active, recreations, mixed_batches);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
