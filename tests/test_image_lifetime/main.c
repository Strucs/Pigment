#include <pigment/pigment.h>
#include <pigment/pigment_vk.h>

#include <SDL3/SDL.h>

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define IMAGES 8
#define PIXELS 16
#define ITERATIONS 96

typedef struct Owner {
    PResourceTracker* tracker;
    PImage* images[IMAGES];
    _Atomic uint32_t destroyed;
} Owner;

typedef struct Release {
    Pigment* pigment;
    PImage* image;
    SDL_Semaphore* start;
} Release;

static _Atomic uint32_t errors;
static _Thread_local int fail_calloc_after = -1;
static _Thread_local PBool expect_error;
static _Thread_local uint32_t expected_errors;

static void* test_calloc(void* user_data, uint64_t size, uint64_t alignment, PAllocScope scope)
{
    if(scope == P_ALLOC_SCOPE_OBJECT && fail_calloc_after >= 0 && fail_calloc_after-- == 0)
    {
        return NULL;
    }

    return pigment_default_allocator.calloc(user_data, size, alignment, scope);
}

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    if(expect_error && (severity & PIGMENT_LOG_ERROR_BIT) && !(type & PIGMENT_LOG_TYPE_VALIDATION_BIT))
    {
        expected_errors++;
        return;
    }

    if((severity & PIGMENT_LOG_ERROR_BIT) || ((severity & PIGMENT_LOG_WARN_BIT) && (type & PIGMENT_LOG_TYPE_VALIDATION_BIT)))
    {
        atomic_fetch_add_explicit(&errors, 1, memory_order_relaxed);
    }
    pigment_default_log_callback(severity, type, record, user_data);
}

static void destroy_owner(Pigment* pigment, void* resource)
{
    Owner* owner = resource;
    for(uint32_t i = 0; i < IMAGES; i++)
    {
        pigment_destroy_image(pigment, owner->images[i]);
    }

    pigment_destroy_resource_tracker(pigment, owner->tracker);
    atomic_fetch_add_explicit(&owner->destroyed, 1, memory_order_relaxed);
}

static int release_wrapper(void* resource)
{
    Release* release = resource;
    SDL_WaitSemaphore(release->start);
    pigment_destroy_image(release->pigment, release->image);
    return 0;
}

/**
 * Modes 0/1: shared tracker, owner destruction requested before/after wrapper releases.
 * Mode 2: recorded commands discarded without submission.
 * Mode 3: private tracker per wrapper.
 * Modes 4/5: no recording, owner destruction requested before/after wrapper releases.
 */
static void test_images(Pigment* pigment, PCommandPool* pool, PCommandBuffer* cmd, PBuffer* readback, uint32_t mode)
{
    Owner owner    = {0};
    PBool shared   = mode != 3;
    PBool recorded = mode < 4;
    if(shared)
    {
        owner.tracker = pigment_create_resource_tracker(pigment);
        assert(owner.tracker != NULL);
    }

    SDL_Semaphore* start = SDL_CreateSemaphore(0);
    assert(start != NULL);
    Release releases[IMAGES];
    SDL_Thread* threads[IMAGES];

    if(recorded)
    {
        pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
    }

    for(uint32_t i = 0; i < IMAGES; i++)
    {
        PImageDesc desc = {
            .width  = 4,
            .height = 4,
            .format = P_FORMAT_R8G8B8A8_UNORM,
            .usage  = P_IMAGE_USAGE_RENDER_COLOR | P_IMAGE_USAGE_TRANSFER_SRC,
        };

        owner.images[i] = pigment_create_image(pigment, &desc);
        assert(owner.images[i] != NULL);

        // The owner keeps the native image alive until the wrappers can be released.
        PImage* wrapper = pigment_vk_wrap_image(pigment, pigment_vk_image(owner.images[i]), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 4, 4, owner.tracker);
        assert(wrapper != NULL);
        assert(image_get_or_create_view(pigment, wrapper, &(PImageViewDesc) {0}) != NULL);

        if(recorded)
        {
            PAttachmentRef attachment = {
                .image       = wrapper,
                .layer_count = 1,
                .store_op    = P_STORE_OP_STORE
            };

            PRenderPassDesc pass = {
                .color_attachments = &attachment,
                .color_count       = 1,
                .clear_color       = {(float) (i & 1), 0.0f, 1.0f, 1.0f},
                .layer_count       = 1,
            };

            pigment_begin_render_pass(pigment, cmd, &pass);
            pigment_end_render_pass(pigment, cmd, &pass);

            PImageBarrier transition = {
                .image      = wrapper,
                .old_layout = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
                .new_layout = P_IMAGE_LAYOUT_TRANSFER_SRC,
                .src        = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
                .dst        = {               P_PIPELINE_STAGE_TRANSFER_BIT,          P_MEMORY_ACCESS_TRANSFER_READ_BIT},
            };

            pigment_cmd_image_barriers(pigment, cmd, &transition, 1);
            PBufferImageCopy copy = {
                .buffer_offset = i * PIXELS * 4,
                .layer_count   = 1,
                .extent_w      = 4,
                .extent_h      = 4,
                .extent_d      = 1,
            };

            pigment_cmd_copy_image_to_buffer(pigment, cmd, wrapper, P_IMAGE_LAYOUT_TRANSFER_SRC, readback, &copy, 1);
        }

        releases[i] = (Release) {.pigment = pigment, .image = wrapper, .start = start};
        threads[i]  = SDL_CreateThread(release_wrapper, "image_release", &releases[i]);
        assert(threads[i] != NULL);
    }

    if(recorded)
    {
        PBufferBarrier barrier = {
            .buffer = readback,
            .src    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
            .dst    = {    P_PIPELINE_STAGE_HOST_BIT,      P_MEMORY_ACCESS_HOST_READ_BIT},
        };

        pigment_cmd_buffer_barriers(pigment, cmd, &barrier, 1);
        pigment_end_recording(pigment, cmd);
    }

    if(mode == 0 || mode == 4)
    {
        pigment_defer_destroy_tracked(pigment, destroy_owner, &owner, owner.tracker);
    }

    // Release wrappers concurrently before submitting any recorded commands.
    for(uint32_t i = 0; i < IMAGES; i++)
    {
        SDL_SignalSemaphore(start);
    }

    for(uint32_t i = 0; i < IMAGES; i++)
    {
        SDL_WaitThread(threads[i], NULL);
    }

    if(shared && mode != 0 && mode != 4)
    {
        pigment_defer_destroy_tracked(pigment, destroy_owner, &owner, owner.tracker);
    }

    pigment_drain_pending(pigment);

    if(recorded)
    {
        assert(atomic_load_explicit(&owner.destroyed, memory_order_relaxed) == 0);
    }

    // Recorded references must preserve images and views despite destruction requests.
    if(recorded && mode != 2)
    {
        PSubmit submit = {.cmds = &cmd, .cmd_count = 1};
        PSubmitHandle handle;
        assert(pigment_queue_submit(pigment, &submit, 1, &handle) == PIGMENT_SUCCESS);
        pigment_submit_wait(pigment, handle);
        pigment_buffer_invalidate(pigment, readback, 0, IMAGES * PIXELS * 4);

        // Check every pixel to confirm the images remained usable after destruction requests.
        const uint8_t* pixels = pigment_buffer_mapped(readback);
        for(uint32_t i = 0; i < IMAGES; i++)
        {
            for(uint32_t j = 0; j < PIXELS; j++)
            {
                uint32_t offset = (i * PIXELS + j) * 4;
                assert(pixels[offset] == ((i & 1) ? 255 : 0));
                assert(pixels[offset + 1] == 0);
                assert(pixels[offset + 2] == 255);
                assert(pixels[offset + 3] == 255);
            }
        }

        pigment_drain_pending(pigment);

        // GPU completion alone does not release recorded references.
        assert(atomic_load_explicit(&owner.destroyed, memory_order_relaxed) == 0);
    }

    if(recorded)
    {
        // Reset releases recorded references, including commands never submitted.
        pigment_reset_command_pool(pigment, pool);
    }

    pigment_drain_pending(pigment);
    if(shared)
    {
        // The shared owner must be destroyed exactly once.
        assert(atomic_load_explicit(&owner.destroyed, memory_order_relaxed) == 1);
    }
    else
    {
        destroy_owner(pigment, &owner);
    }

    SDL_DestroySemaphore(start);
}

// Each recording uses a different allocation of the same PImage.
static void test_resize(Pigment* pigment, PCommandPool* pool, PCommandBuffer* cmd, PBuffer* readback, PBool submit)
{
    PImageDesc desc = {
        .width  = 4,
        .height = 4,
        .format = P_FORMAT_R8G8B8A8_UNORM,
        .usage  = P_IMAGE_USAGE_RENDER_COLOR | P_IMAGE_USAGE_TRANSFER_SRC,
    };

    PImage* image = pigment_create_image(pigment, &desc);
    assert(image != NULL);
    pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

    for(uint32_t i = 0; i < IMAGES; i++)
    {
        PAttachmentRef attachment = {
            .image       = image,
            .layer_count = 1,
            .store_op    = P_STORE_OP_STORE,
        };

        PRenderPassDesc pass = {
            .color_attachments = &attachment,
            .color_count       = 1,
            .clear_color       = {(float) (i & 1), 0.0f, 1.0f, 1.0f},
            .layer_count       = 1,
        };

        pigment_begin_render_pass(pigment, cmd, &pass);
        pigment_end_render_pass(pigment, cmd, &pass);

        PImageBarrier transition = {
            .image      = image,
            .old_layout = P_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .new_layout = P_IMAGE_LAYOUT_TRANSFER_SRC,
            .src        = {P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, P_MEMORY_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
            .dst        = {               P_PIPELINE_STAGE_TRANSFER_BIT,          P_MEMORY_ACCESS_TRANSFER_READ_BIT},
        };

        pigment_cmd_image_barriers(pigment, cmd, &transition, 1);
        PBufferImageCopy copy = {
            .buffer_offset = i * PIXELS * 4,
            .layer_count   = 1,
            .extent_w      = 4,
            .extent_h      = 4,
            .extent_d      = 1,
        };

        pigment_cmd_copy_image_to_buffer(pigment, cmd, image, P_IMAGE_LAYOUT_TRANSFER_SRC, readback, &copy, 1);

        VkImage previous          = pigment_vk_image(image);
        PImageView* view          = image_get_or_create_view(pigment, image, &(PImageViewDesc) {0});
        VkImageView previous_view = pigment_vk_image_view(view);

        pigment_image_resize(pigment, image, 5 + i, 5 + i);
        pigment_drain_pending(pigment);

        assert(pigment_image_width(image) == 5 + i && pigment_image_height(image) == 5 + i);
        assert(pigment_vk_image(image) != previous);
        assert(pigment_vk_image_view(view) == previous_view);
        assert(image_get_or_create_view(pigment, image, &(PImageViewDesc) {0}) != view);
    }

    PBufferBarrier barrier = {
        .buffer = readback,
        .src    = {P_PIPELINE_STAGE_TRANSFER_BIT, P_MEMORY_ACCESS_TRANSFER_WRITE_BIT},
        .dst    = {    P_PIPELINE_STAGE_HOST_BIT,      P_MEMORY_ACCESS_HOST_READ_BIT},
    };

    pigment_cmd_buffer_barriers(pigment, cmd, &barrier, 1);
    pigment_end_recording(pigment, cmd);

    // The current allocation is unused and can be freed before the older ones.
    pigment_destroy_image(pigment, image);
    pigment_drain_pending(pigment);

    if(submit)
    {
        // Resubmission must retain every old allocation after the first GPU completion.
        for(uint32_t repeat = 0; repeat < 2; repeat++)
        {
            PSubmit submission = {.cmds = &cmd, .cmd_count = 1};
            PSubmitHandle handle;
            assert(pigment_queue_submit(pigment, &submission, 1, &handle) == PIGMENT_SUCCESS);
            pigment_submit_wait(pigment, handle);
            pigment_buffer_invalidate(pigment, readback, 0, IMAGES * PIXELS * 4);

            const uint8_t* pixels = pigment_buffer_mapped(readback);
            for(uint32_t i = 0; i < IMAGES; i++)
            {
                for(uint32_t j = 0; j < PIXELS; j++)
                {
                    uint32_t offset = (i * PIXELS + j) * 4;
                    assert(pixels[offset] == ((i & 1) ? 255 : 0));
                    assert(pixels[offset + 1] == 0);
                    assert(pixels[offset + 2] == 255);
                    assert(pixels[offset + 3] == 255);
                }
            }

            pigment_drain_pending(pigment);
        }
    }

    // Also release old allocations when their commands were never submitted.
    pigment_reset_command_pool(pigment, pool);
    pigment_drain_pending(pigment);
}

static void test_resize_failure(Pigment* pigment)
{
    PImageDesc desc = {
        .width  = 4,
        .height = 4,
        .format = P_FORMAT_R8G8B8A8_UNORM,
        .usage  = P_IMAGE_USAGE_TRANSFER_SRC | P_IMAGE_USAGE_SAMPLED,
        .flags  = P_IMAGE_FLAG_HOST_MAPPED,
    };

    PImage* image = pigment_create_image(pigment, &desc);
    assert(image != NULL);
    VkImage original   = pigment_vk_image(image);
    PImageView* view   = image_get_or_create_view(pigment, image, &(PImageViewDesc) {0});
    void* mapped       = pigment_image_mapped(image);
    uint64_t row_pitch = pigment_image_row_pitch(image);
    assert(mapped != NULL && row_pitch >= 16);

    // Fail the old bundle, new tracker, timeline array and view allocations in turn.
    for(int fail = 0; fail < 4; fail++)
    {
        expected_errors   = 0;
        expect_error      = P_TRUE;
        fail_calloc_after = fail;
        pigment_image_resize(pigment, image, 8, 8);
        assert(fail_calloc_after == -1 && expected_errors > 0);
        expect_error = P_FALSE;

        assert(pigment_vk_image(image) == original);
        assert(pigment_image_width(image) == 4 && pigment_image_height(image) == 4);
        assert(image_get_or_create_view(pigment, image, &(PImageViewDesc) {0}) == view);
        assert(pigment_image_mapped(image) == mapped);
        assert(pigment_image_row_pitch(image) == row_pitch);
    }

    // An external wrapper cannot replace the allocation owned by its caller.
    PImage* wrapper = pigment_vk_wrap_image(pigment, original, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 4, 4, NULL);
    assert(wrapper != NULL);
    expected_errors = 0;
    expect_error    = P_TRUE;
    pigment_image_resize(pigment, wrapper, 8, 8);
    expect_error = P_FALSE;
    assert(expected_errors == 1);
    assert(pigment_vk_image(wrapper) == original);
    assert(pigment_image_width(wrapper) == 4 && pigment_image_height(wrapper) == 4);
    pigment_destroy_image(pigment, wrapper);
    pigment_drain_pending(pigment);

    pigment_image_resize(pigment, image, 8, 8);
    assert(pigment_image_width(image) == 8 && pigment_image_height(image) == 8);
    assert(pigment_image_mapped(image) != NULL && pigment_image_row_pitch(image) >= 32);
    pigment_destroy_image(pigment, image);
    pigment_drain_pending(pigment);
}

int main(void)
{
    assert(SDL_setenv_unsafe("VK_LAYER_VALIDATE_SYNC", "1", 1) == 0);
    assert(SDL_setenv_unsafe("VK_LAYER_THREAD_SAFETY", "1", 1) == 0);
    assert(SDL_Init(0));

    PAppInfo app = {
        .app_name    = "test_image_lifetime",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PigmentLoggerCreateInfo logger = {
        .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
        .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
        .callback        = log_callback,
    };

    PAllocator allocator = pigment_default_allocator;
    allocator.calloc     = test_calloc;

    PigmentConfig config = {
        .allocator         = &allocator,
        .enable_validation = P_TRUE,
        .loggers           = &logger,
        .logger_count      = 1
    };

    Pigment* pigment = init_pigment(&app, &config);
    assert(pigment != NULL);

    PCommandPoolDesc pool_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT))
    };

    PCommandPool* pool = pigment_create_command_pool(pigment, &pool_desc);
    PCommandBuffer* cmd;

    assert(pool != NULL);
    assert(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) == PIGMENT_SUCCESS);

    PBufferDesc buffer_desc = {
        .size   = IMAGES * PIXELS * 4,
        .usage  = P_BUFFER_USAGE_TRANSFER_DST,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT},
    };

    PBuffer* readback = pigment_create_buffer(pigment, &buffer_desc);
    assert(readback != NULL && pigment_buffer_mapped(readback) != NULL);

    for(uint32_t i = 0; i < ITERATIONS; i++)
    {
        test_images(pigment, pool, cmd, readback, i % 6);
        test_resize(pigment, pool, cmd, readback, (i & 1) == 0);
    }

    test_resize_failure(pigment);

    pigment_destroy_buffer(pigment, readback);
    pigment_destroy_command_pool(pigment, pool);
    destroy_pigment(pigment);
    assert(atomic_load_explicit(&errors, memory_order_relaxed) == 0);
    printf("OK iterations=%u images=%u release_threads=%u resizes=%u\n", ITERATIONS, ITERATIONS * IMAGES, IMAGES, ITERATIONS * IMAGES);

    SDL_Quit();
    return 0;
}
