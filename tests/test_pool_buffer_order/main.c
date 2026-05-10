#include <pigment.h>

#include <SDL3/SDL.h>

#include <stdio.h>

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
         .callback        = pigment_default_log_callback,
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
        .queue_flags = P_QUEUE_GRAPHICS_BIT,
        .flags       = P_COMMAND_POOL_FLAG_TRANSIENT,
        .name        = "test_transient_pool",
    };

    PCommandPool* pool = pigment_create_command_pool(pigment, &pool_desc);
    if(pool == NULL)
    {
        fprintf(stderr, "pigment_create_command_pool failed\n");
        destroy_pigment(pigment);
        SDL_Quit();
        return 1;
    }

    PCommandBuffer* cmd = pigment_create_command_buffer(pigment, pool);
    if(cmd == NULL)
    {
        fprintf(stderr, "pigment_create_command_buffer failed\n");
        pigment_destroy_command_pool(pigment, pool);
        destroy_pigment(pigment);
        SDL_Quit();
        return 1;
    }

    pigment_destroy_command_buffer(pigment, cmd);
    pigment_destroy_command_pool(pigment, pool);

    pigment_drain_pending(pigment);

    destroy_pigment(pigment);
    SDL_Quit();

    printf("OK\n");
    return 0;
}
