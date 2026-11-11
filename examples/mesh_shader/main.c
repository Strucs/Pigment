#include <pigment/pigment_std.h>
#include <pigment/pigment_sdl.h>

#include "triangle_mesh_spv.h"
#include "triangle_frag_spv.h"
#include "triangle_task_spv.h"

#include <stdio.h>

#define FRAMES_IN_FLIGHT 2

int main(void)
{
    PAppInfo app = {.app_name = "Mesh Shader"};

    PigmentLoggerCreateInfo logger = {
        .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
        .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
        .callback        = pigment_default_log_callback,
    };

    PigmentConfig config = {
        .max_frames_in_flight  = FRAMES_IN_FLIGHT,
        .enable_validation     = P_TRUE,
        .enable_best_practices = P_TRUE,
        .loggers               = &logger,
        .logger_count          = 1,
    };

    Pigment* pigment = init_pigment(&app, &config);
    if(pigment == NULL)
    {
        return 1;
    }

    SDL_Window* window        = NULL;
    PCommandPool* pool        = NULL;
    PWindowRenderer* renderer = NULL;
    PLayout* layout           = NULL;
    PPipeline* pipeline       = NULL;
    int error_code            = 1;

    PCommandBuffer* cmds[FRAMES_IN_FLIGHT] = {0};

    if(!pigment_supports(pigment, P_FEATURE_MESH_SHADER))
    {
        puts("Mesh shaders are unavailable on this device or driver. Skipping the example.");
        error_code = 0;
        goto FREE;
    }

    if(!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        goto FREE;
    }

    window = SDL_CreateWindow("Mesh Shader", 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if(window == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        goto FREE;
    }

    PCommandPoolDesc pool_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
    };
    pool = pigment_create_command_pool(pigment, &pool_desc);
    if(pool == NULL || pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, FRAMES_IN_FLIGHT, cmds) != PIGMENT_SUCCESS)
    {
        goto FREE;
    }

    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);

    PWindowHandles handles   = pigment_sdl_get_window_handles(window);
    PSwapchainDesc swapchain = {.width = (uint32_t) width, .height = (uint32_t) height};
    renderer                 = pigment_renderer_create(pigment, &handles, &swapchain);
    if(renderer == NULL)
    {
        goto FREE;
    }

    PLayoutDesc layout_desc = {0};
    layout                  = pigment_create_layout(pigment, &layout_desc);
    if(layout == NULL)
    {
        goto FREE;
    }

    PShaderStageDesc stages[] = {
        {    .stage = P_SHADER_STAGE_MESH_BIT, .code = (const uint32_t*) triangle_mesh_spv, .code_size = sizeof(triangle_mesh_spv)},
        {.stage = P_SHADER_STAGE_FRAGMENT_BIT, .code = (const uint32_t*) triangle_frag_spv, .code_size = sizeof(triangle_frag_spv)},
        {    .stage = P_SHADER_STAGE_TASK_BIT, .code = (const uint32_t*) triangle_task_spv, .code_size = sizeof(triangle_task_spv)},
    };

    uint32_t stage_count = sizeof(stages) / sizeof(stages[0]);
    if(!pigment_supports(pigment, P_FEATURE_TASK_SHADER))
    {
        stage_count--;
    }

    PFormat format              = pigment_get_color_format(renderer);
    PPipelineDesc pipeline_desc = {
        .layout             = layout,
        .stages             = stages,
        .stage_count        = stage_count,
        .color_formats      = &format,
        .color_format_count = 1,
        .sample_count       = pigment_get_sample_count(renderer),
        .name               = "mesh_triangle",
    };

    if(pigment_create_graphic_pipelines(pigment, NULL, &pipeline_desc, 1, &pipeline) != PIGMENT_SUCCESS)
    {
        goto FREE;
    }

    SDL_ShowWindow(window);

    PBool running = P_TRUE;
    uint32_t slot = 0;

    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    running = P_FALSE;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    pigment_renderer_resize(renderer, (uint32_t) event.window.data1, (uint32_t) event.window.data2);
                    break;
                default:
                    break;
            }
        }

        pigment_wait_frame_ready(pigment, renderer, slot);
        pigment_drain_pending(pigment);

        PFrame* frame;
        PResult acquire_result = pigment_begin_frame_context(pigment, renderer, slot, UINT64_MAX, &frame);
        if(acquire_result == PIGMENT_RECREATE_REQUIRED)
        {
            pigment_recreate_swapchain(pigment, renderer);
            continue;
        }

        if(acquire_result != PIGMENT_SUCCESS)
        {
            break;
        }

        PCommandBuffer* cmd = cmds[pigment_frame_slot(frame)];
        pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

        PSwapchainPassDesc pass = {
            .clear_color = {0.02f, 0.02f, 0.02f, 1.0f},
            .no_depth    = P_TRUE
        };
        pigment_cmd_begin_swapchain_pass(pigment, cmd, frame, &pass);

        pigment_bind_pipeline(pigment, cmd, pipeline);
        pigment_cmd_draw_mesh_tasks(pigment, cmd, 1, 1, 1);

        pigment_cmd_end_swapchain_pass(cmd, frame);

        pigment_end_recording(pigment, cmd);

        PSubmit submit = {
            .cmds           = &cmd,
            .cmd_count      = 1,
            .frame          = frame,
            .wait_acquire   = P_TRUE,
            .acquire_stage  = P_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .signal_present = P_TRUE,
        };

        pigment_queue_submit(pigment, &submit, 1, NULL);
        pigment_present_frame(pigment, frame);
        slot = (slot + 1) % FRAMES_IN_FLIGHT;
    }

    error_code = 0;

FREE:
    pigment_destroy_pipeline(pigment, pipeline);
    pigment_destroy_layout(pigment, layout);
    pigment_renderer_destroy(pigment, renderer);
    pigment_destroy_command_pool(pigment, pool);
    destroy_pigment(pigment);

    if(window != NULL)
    {
        SDL_DestroyWindow(window);
    }

    SDL_Quit();

    return error_code;
}
