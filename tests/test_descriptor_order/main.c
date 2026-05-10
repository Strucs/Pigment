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
        .app_name    = "test_descriptor_order",
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

    PDescriptorBinding bindings[] = {
        {
         .binding = 0,
         .type    = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .count   = 1,
         .stages  = P_SHADER_STAGE_FRAGMENT_BIT,
         .flags   = P_DESCRIPTOR_BINDING_NONE_BIT,
         },
    };
    PDescriptorSetLayoutDesc layout_desc = {
        .bindings      = bindings,
        .binding_count = 1,
        .name          = "test_layout",
    };

    PDescriptorSetLayout* layout = pigment_create_descriptor_set_layout(pigment, &layout_desc);

    PDescriptorPoolSize pool_sizes[] = {
        {.type = P_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .count = 4},
    };

    PDescriptorPoolDesc pool_desc = {
        .pool_sizes      = pool_sizes,
        .pool_size_count = 1,
        .max_sets        = 4,
        .allow_free_set  = P_TRUE,
        .name            = "test_pool",
    };

    PDescriptorPool* pool = pigment_create_descriptor_pool(pigment, &pool_desc);

    PDescriptorSet* set_a = pigment_create_descriptor_set(pigment, pool, layout, 0, "set_a");
    PDescriptorSet* set_b = pigment_create_descriptor_set(pigment, pool, layout, 0, "set_b");

    pigment_destroy_descriptor_set(pigment, set_a);
    pigment_destroy_descriptor_pool(pigment, pool);
    pigment_drain_pending(pigment);
    (void) set_b;

    PDescriptorPool* pool_b = pigment_create_descriptor_pool(pigment, &pool_desc);
    pigment_create_descriptor_set(pigment, pool_b, layout, 0, "reset_set_a");
    pigment_create_descriptor_set(pigment, pool_b, layout, 0, "reset_set_b");
    pigment_reset_descriptor_pool(pigment, pool_b);
    pigment_create_descriptor_set(pigment, pool_b, layout, 0, "after_reset");

    pigment_destroy_descriptor_pool(pigment, pool_b);
    pigment_destroy_descriptor_set_layout(pigment, layout);
    pigment_drain_pending(pigment);

    destroy_pigment(pigment);
    SDL_Quit();

    printf("OK\n");
    return 0;
}
