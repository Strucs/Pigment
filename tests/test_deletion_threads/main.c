#include <pigment/pigment.h>

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define NUM_THREADS 8
#define ITERS_PER_THREAD 1000000
#define DRAIN_EVERY_N 1024

static _Atomic int destroyed_count = 0;

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
