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

    PDescriptorSetAllocate set_allocs[2] = {
        {.layout = layout, .variable_count = 0, .name = "set_a"},
        {.layout = layout, .variable_count = 0, .name = "set_b"},
    };
    PDescriptorSet* sets[2];
    pigment_create_descriptor_sets(pigment, pool, set_allocs, 2, sets);
    PDescriptorSet* set_a = sets[0];
    PDescriptorSet* set_b = sets[1];

    pigment_destroy_descriptor_sets(pigment, &set_a, 1);
    pigment_destroy_descriptor_pool(pigment, pool);
    pigment_drain_pending(pigment);
    (void) set_b;

    PDescriptorPool* pool_b = pigment_create_descriptor_pool(pigment, &pool_desc);

    PDescriptorSetAllocate reset_allocs[2] = {
        {.layout = layout, .variable_count = 0, .name = "reset_set_a"},
        {.layout = layout, .variable_count = 0, .name = "reset_set_b"},
    };
    PDescriptorSet* reset_sets[2];
    pigment_create_descriptor_sets(pigment, pool_b, reset_allocs, 2, reset_sets);

    pigment_reset_descriptor_pool(pigment, pool_b);

    PDescriptorSetAllocate after_alloc = {.layout = layout, .variable_count = 0, .name = "after_reset"};
    PDescriptorSet* after_set;
    pigment_create_descriptor_sets(pigment, pool_b, &after_alloc, 1, &after_set);

    pigment_destroy_descriptor_pool(pigment, pool_b);
    pigment_destroy_descriptor_set_layout(pigment, layout);
    pigment_drain_pending(pigment);

    destroy_pigment(pigment);
    SDL_Quit();

    printf("OK\n");
    return 0;
}
