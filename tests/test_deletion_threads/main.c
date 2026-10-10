#include <pigment/pigment.h>

#include <SDL3/SDL.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define NUM_THREADS 8
#define ITERS_PER_THREAD 1000000
#define DRAIN_EVERY_N 1024
#define TRACKED_RESOURCES 256

static _Atomic int destroyed_count = 0;

typedef struct Resource {
    PResourceTracker* tracker;
    _Atomic uint32_t destroyed;
} Resource;

typedef struct RecordedTest {
    Pigment* pigment;
    SDL_Semaphore* ready;
    SDL_Semaphore* release;
    Resource resources[TRACKED_RESOURCES];
} RecordedTest;

static void destroy_resource(Pigment* pigment, void* ptr)
{
    Resource* resource = ptr;
    atomic_fetch_add_explicit(&resource->destroyed, 1, memory_order_relaxed);
    pigment_destroy_resource_tracker(pigment, resource->tracker);
}

static int recorded_worker(void* arg)
{
    RecordedTest* test = arg;

    PCommandPoolDesc desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(test->pigment, P_QUEUE_GRAPHICS_BIT)),
    };

    PCommandPool* pool = pigment_create_command_pool(test->pigment, &desc);
    PCommandBuffer* cmd;

    assert(pool != NULL);
    assert(pigment_create_command_buffers(test->pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) == PIGMENT_SUCCESS);

    pigment_begin_recording(test->pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

    for(uint32_t i = 0; i < TRACKED_RESOURCES; i++)
    {
        pigment_cmd_use(test->pigment, cmd, test->resources[i].tracker);
    }

    pigment_end_recording(test->pigment, cmd);
    SDL_SignalSemaphore(test->ready);
    SDL_WaitSemaphore(test->release);

    pigment_reset_command_pool(test->pigment, pool);
    pigment_destroy_command_pool(test->pigment, pool);
    return 0;
}

static void test_recorded_releases(Pigment* pigment)
{
    RecordedTest test = {
        .pigment = pigment,
        .ready   = SDL_CreateSemaphore(0),
        .release = SDL_CreateSemaphore(0),
    };

    assert(test.ready != NULL && test.release != NULL);
    for(uint32_t i = 0; i < TRACKED_RESOURCES; i++)
    {
        test.resources[i].tracker = pigment_create_resource_tracker(pigment);
        assert(test.resources[i].tracker != NULL);
    }

    SDL_Thread* threads[NUM_THREADS];
    for(uint32_t i = 0; i < NUM_THREADS; i++)
    {
        threads[i] = SDL_CreateThread(recorded_worker, "recorded_release", &test);
        assert(threads[i] != NULL);
    }
    for(uint32_t i = 0; i < NUM_THREADS; i++)
    {
        SDL_WaitSemaphore(test.ready);
    }

    for(uint32_t i = 0; i < NUM_THREADS; i++)
    {
        SDL_SignalSemaphore(test.release);
    }

    // The destruction request races with releases from eight independent pools.
    for(uint32_t i = 0; i < TRACKED_RESOURCES; i++)
    {
        pigment_defer_destroy_tracked(pigment, destroy_resource, &test.resources[i], test.resources[i].tracker);
    }

    for(uint32_t i = 0; i < NUM_THREADS; i++)
    {
        SDL_WaitThread(threads[i], NULL);
    }

    pigment_drain_pending(pigment);

    for(uint32_t i = 0; i < TRACKED_RESOURCES; i++)
    {
        assert(atomic_load_explicit(&test.resources[i].destroyed, memory_order_relaxed) == 1);
    }

    SDL_DestroySemaphore(test.ready);
    SDL_DestroySemaphore(test.release);
}

static void dummy_destroy(Pigment* pigment, void* resource)
{
    (void) pigment;
    (void) resource;
    atomic_fetch_add_explicit(&destroyed_count, 1, memory_order_relaxed);
}

static int worker(void* arg)
{
    Pigment* pigment = arg;
    for(int i = 0; i < ITERS_PER_THREAD; i++)
    {
        pigment_defer_destroy(pigment, dummy_destroy, (void*) (uintptr_t) (i + 1));
        if((i & (DRAIN_EVERY_N - 1)) == 0)
        {
            pigment_drain_pending(pigment);
        }
    }
    return 0;
}

int main(void)
{
    if(!SDL_Init(0))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    PAppInfo app_info = {
        .app_name    = "test_deletion_threads",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0),
    };

    PigmentConfig config = {
        .enable_validation = P_TRUE,
    };

    Pigment* pigment = init_pigment(&app_info, &config);
    if(pigment == NULL)
    {
        fprintf(stderr, "init_pigment failed\n");
        SDL_Quit();
        return 1;
    }

    Uint64 start = SDL_GetTicksNS();

    SDL_Thread* threads[NUM_THREADS];
    for(int i = 0; i < NUM_THREADS; i++)
    {
        char name[16];
        SDL_snprintf(name, sizeof(name), "worker_%d", i);
        threads[i] = SDL_CreateThread(worker, name, pigment);
        if(threads[i] == NULL)
        {
            fprintf(stderr, "SDL_CreateThread: %s\n", SDL_GetError());
            destroy_pigment(pigment);
            SDL_Quit();
            return 1;
        }
    }

    int target = NUM_THREADS * ITERS_PER_THREAD;
    while(atomic_load_explicit(&destroyed_count, memory_order_relaxed) < target)
    {
        pigment_drain_pending(pigment);
    }

    for(int i = 0; i < NUM_THREADS; i++)
    {
        SDL_WaitThread(threads[i], NULL);
    }

    pigment_drain_pending(pigment);

    Uint64 elapsed = SDL_GetTicksNS() - start;
    int got        = atomic_load_explicit(&destroyed_count, memory_order_relaxed);

    printf("threads=%d iters_per_thread=%d total=%d got=%d elapsed=%.3f ms\n", NUM_THREADS, ITERS_PER_THREAD, target, got, elapsed / 1e6);

    test_recorded_releases(pigment);
    destroy_pigment(pigment);
    SDL_Quit();

    if(got != target)
    {
        fprintf(stderr, "FAIL: expected %d destroys, got %d\n", target, got);
        return 1;
    }

    printf("OK\n");
    return 0;
}
