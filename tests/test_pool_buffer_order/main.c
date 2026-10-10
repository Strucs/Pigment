#include <pigment/pigment.h>

#include <SDL3/SDL.h>

#include <assert.h>
#include <stdio.h>

typedef struct Resource {
    PResourceTracker* tracker;
    PSubmitHandle handle;
    uint32_t destroyed;
} Resource;

static uint32_t errors;

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    if((severity & PIGMENT_LOG_ERROR_BIT) || ((severity & PIGMENT_LOG_WARN_BIT) && (type & PIGMENT_LOG_TYPE_VALIDATION_BIT)))
    {
        errors++;
    }
    pigment_default_log_callback(severity, type, record, user_data);
}

static void destroy_resource(Pigment* pigment, void* ptr)
{
    Resource* resource = ptr;
    assert(pigment_submit_complete(pigment, resource->handle));
    resource->destroyed++;
    pigment_destroy_resource_tracker(pigment, resource->tracker);
}

static void test_recorded_resources(Pigment* pigment, PCommandPoolDesc* desc)
{
    PCommandPool* primary_pool   = pigment_create_command_pool(pigment, desc);
    PCommandPool* secondary_pool = pigment_create_command_pool(pigment, desc);
    PCommandBuffer* primary;
    PCommandBuffer* secondary;
    assert(primary_pool != NULL && secondary_pool != NULL);
    assert(pigment_create_command_buffers(pigment, primary_pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &primary) == PIGMENT_SUCCESS);
    assert(pigment_create_command_buffers(pigment, secondary_pool, P_COMMAND_BUFFER_LEVEL_SECONDARY, 1, &secondary) == PIGMENT_SUCCESS);

    Resource resource = {.tracker = pigment_create_resource_tracker(pigment)};
    assert(resource.tracker != NULL);
    PCommandBufferInheritance inheritance = {0};
    pigment_begin_recording(pigment, secondary, P_CMD_BUFFER_USAGE_DEFAULT, &inheritance);
    pigment_cmd_use(pigment, secondary, resource.tracker);
    pigment_cmd_use(pigment, secondary, resource.tracker);
    pigment_end_recording(pigment, secondary);
    pigment_begin_recording(pigment, primary, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
    pigment_cmd_execute_commands(pigment, primary, &secondary, 1);
    pigment_end_recording(pigment, primary);

    pigment_defer_destroy_tracked(pigment, destroy_resource, &resource, resource.tracker);
    pigment_drain_pending(pigment);
    assert(resource.destroyed == 0);

    PSubmit submit = {.cmds = &primary, .cmd_count = 1};
    for(uint32_t i = 0; i < 2; i++)
    {
        assert(pigment_queue_submit(pigment, &submit, 1, &resource.handle) == PIGMENT_SUCCESS);
        pigment_submit_wait(pigment, resource.handle);
        pigment_drain_pending(pigment);
        assert(resource.destroyed == 0);
    }

    // Resetting a secondary invalidates the primary, which still owns its references.
    pigment_reset_command_pool(pigment, secondary_pool);
    pigment_drain_pending(pigment);
    assert(resource.destroyed == 0);
    pigment_begin_recording(pigment, primary, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
    pigment_end_recording(pigment, primary);
    pigment_drain_pending(pigment);
    assert(resource.destroyed == 1);

    // Abandon a recording without submitting it, then free it or its pool.
    for(uint32_t i = 0; i < 2; i++)
    {
        Resource abandoned = {.tracker = pigment_create_resource_tracker(pigment)};
        assert(abandoned.tracker != NULL);
        pigment_begin_recording(pigment, primary, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
        pigment_cmd_use(pigment, primary, abandoned.tracker);
        pigment_end_recording(pigment, primary);
        pigment_defer_destroy_tracked(pigment, destroy_resource, &abandoned, abandoned.tracker);
        pigment_drain_pending(pigment);
        assert(abandoned.destroyed == 0);
        if(i == 0)
        {
            pigment_destroy_command_buffers(pigment, &primary, 1);
        }
        else
        {
            pigment_destroy_command_pool(pigment, primary_pool);
        }
        pigment_drain_pending(pigment);
        assert(abandoned.destroyed == 1);
        if(i == 0)
        {
            assert(pigment_create_command_buffers(pigment, primary_pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &primary) == PIGMENT_SUCCESS);
        }
    }

    pigment_destroy_command_pool(pigment, secondary_pool);
    pigment_drain_pending(pigment);
}

int main(void)
{
    if(!SDL_Init(0))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    PAppInfo app_info = {
        .app_name    = "test_pool_buffer_order",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0),
    };

    PigmentLoggerCreateInfo loggers[] = {
        {
         .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
         .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
         .callback        = log_callback,
         },
    };

    PigmentConfig config = {
        .loggers           = loggers,
        .logger_count      = 1,
        .enable_validation = P_TRUE,
    };

    Pigment* pigment = init_pigment(&app_info, &config);
    if(pigment == NULL)
    {
        fprintf(stderr, "init_pigment failed\n");
        SDL_Quit();
        return 1;
    }

    PCommandPoolDesc pool_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
        .name         = "test_transient_pool",
    };

    PCommandPool* pool = pigment_create_command_pool(pigment, &pool_desc);
    if(pool == NULL)
    {
        fprintf(stderr, "pigment_create_command_pool failed\n");
        destroy_pigment(pigment);
        SDL_Quit();
        return 1;
    }

    PCommandBuffer* cmd = NULL;
    if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "pigment_create_command_buffers failed\n");
        pigment_destroy_command_pool(pigment, pool);
        destroy_pigment(pigment);
        SDL_Quit();
        return 1;
    }

    pigment_destroy_command_buffers(pigment, &cmd, 1);
    pigment_destroy_command_pool(pigment, pool);

    pigment_drain_pending(pigment);

    test_recorded_resources(pigment, &pool_desc);
    destroy_pigment(pigment);
    assert(errors == 0);
    SDL_Quit();

    printf("OK\n");
    return 0;
}
