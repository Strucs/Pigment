#include <pigment/pigment.h>

#include "empty_comp_spv.h"

#include <SDL3/SDL.h>

#include <assert.h>
#include <stdio.h>

static uint32_t errors;

static void log_callback(PigmentLogSeverity severity, PigmentLogType type, const PigmentLogRecord* record, void* user_data)
{
    if((severity & PIGMENT_LOG_ERROR_BIT) || ((severity & PIGMENT_LOG_WARN_BIT) && (type & PIGMENT_LOG_TYPE_VALIDATION_BIT)))
    {
        errors++;
    }
    pigment_default_log_callback(severity, type, record, user_data);
}

static void test_recorded_sets(Pigment* pigment, PDescriptorSetLayout* set_layout, const PDescriptorPoolDesc* pool_desc)
{
    PCommandPoolDesc command_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
    };
    PCommandPool* commands = pigment_create_command_pool(pigment, &command_desc);
    PCommandBuffer* cmd;
    assert(commands != NULL);
    assert(pigment_create_command_buffers(pigment, commands, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &cmd) == PIGMENT_SUCCESS);

    PLayoutDesc layout_desc = {.set_layouts = &set_layout, .set_layout_count = 1};
    PLayout* layout         = pigment_create_layout(pigment, &layout_desc);
    assert(layout != NULL);

    for(uint32_t i = 0; i < 2; i++)
    {
        PDescriptorPoolDesc desc     = *pool_desc;
        desc.allow_free_set          = i == 0;
        PDescriptorPool* pool        = pigment_create_descriptor_pool(pigment, &desc);
        PDescriptorSetAllocate alloc = {.layout = set_layout};
        PDescriptorSet* set;
        assert(pool != NULL);
        assert(pigment_create_descriptor_sets(pigment, pool, &alloc, 1, &set) == PIGMENT_SUCCESS);

        PComputePipelineDesc pipeline_desc = {
            .layout              = layout,
            .compute_shader      = (const uint32_t*) empty_comp_spv,
            .compute_shader_size = (uint32_t) sizeof(empty_comp_spv),
        };
        PPipeline* pipeline;
        assert(pigment_create_compute_pipelines(pigment, NULL, &pipeline_desc, 1, &pipeline) == PIGMENT_SUCCESS);
        pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);
        pigment_bind_pipeline(pigment, cmd, pipeline);
        pigment_cmd_bind_descriptor_sets(pigment, cmd, pipeline, 0, &set, 1, NULL, 0);
        pigment_cmd_dispatch(pigment, cmd, 1, 1, 1);
        pigment_end_recording(pigment, cmd);

        if(desc.allow_free_set)
        {
            pigment_destroy_descriptor_sets(pigment, &set, 1);
        }
        pigment_destroy_descriptor_pool(pigment, pool);
        pigment_destroy_pipeline(pigment, pipeline);
        pigment_drain_pending(pigment);

        PSubmit submit = {.cmds = &cmd, .cmd_count = 1};
        PSubmitHandle handle;
        assert(pigment_queue_submit(pigment, &submit, 1, &handle) == PIGMENT_SUCCESS);
        pigment_submit_wait(pigment, handle);
        pigment_reset_command_pool(pigment, commands);
        pigment_drain_pending(pigment);
    }

    pigment_destroy_command_pool(pigment, commands);
    pigment_destroy_layout(pigment, layout);
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
        .app_name    = "test_descriptor_order",
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
    test_recorded_sets(pigment, layout, &pool_desc);
    pigment_destroy_descriptor_set_layout(pigment, layout);
    pigment_drain_pending(pigment);

    destroy_pigment(pigment);
    assert(errors == 0);
    SDL_Quit();

    printf("OK\n");
    return 0;
}
