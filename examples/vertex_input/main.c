#include <pigment/pigment_std.h>
#include <pigment/pigment_sdl.h>

#include "test_vert_spv.h"
#include "test_frag_spv.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct Vertex {
    float pos[3];
    float color[3];
} Vertex;

#define FRAMES_IN_FLIGHT 2

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Vertex Input",
        .app_version = PIGMENT_MAKE_VERSION(0, 1, 0),
    };

    SDL_Window* window        = NULL;
    Pigment* pigment          = NULL;
    PCommandPool* pool        = NULL;
    PWindowRenderer* renderer = NULL;
    PLayout* layout           = NULL;
    PPipeline* pipeline       = NULL;
    PBuffer* vertex_buffer    = NULL;
    int error_code            = 1;

    PCommandBuffer* frame_cmds[FRAMES_IN_FLIGHT] = {0};

    if(!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Vertex Input", 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if(window == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    PigmentLoggerCreateInfo loggers[] = {
        {
         .severity_filter = PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
         .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
         .callback        = pigment_default_log_callback,
         .user_data       = NULL,
         },
    };
    PigmentConfig config = {
        .max_frames_in_flight = FRAMES_IN_FLIGHT,
        .loggers              = loggers,
        .logger_count         = sizeof(loggers) / sizeof(loggers[0]),
        .enable_validation    = P_TRUE,
    };

    pigment = init_pigment(&app_info, &config);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
        goto FREE;
    }

    PCommandPoolDesc pool_desc = {
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
        .name         = "test_vertex_input_pool",
    };
    pool = pigment_create_command_pool(pigment, &pool_desc);
    if(pool == NULL)
    {
        fprintf(stderr, "Failed to create command pool!\n");
        goto FREE;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSizeInPixels(window, &win_w, &win_h);
    PWindowHandles handles        = pigment_sdl_get_window_handles(window);
    PSwapchainDesc swapchain_desc = {
        .width  = (uint32_t) win_w,
        .height = (uint32_t) win_h,
    };

    if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, FRAMES_IN_FLIGHT, frame_cmds) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create frame command buffers!\n");
        goto FREE;
    }

    renderer = pigment_renderer_create(pigment, &handles, &swapchain_desc);
    if(renderer == NULL)
    {
        fprintf(stderr, "Failed to create renderer!\n");
        goto FREE;
    }

    PLayoutDesc layout_desc = {
        .name = "test_vertex_input_layout",
    };
    layout = pigment_create_layout(pigment, &layout_desc);
    if(layout == NULL)
    {
        fprintf(stderr, "Failed to create layout!\n");
        goto FREE;
    }

    PVertexBindingDesc bindings[] = {
        {.binding = 0, .stride = sizeof(Vertex), .input_rate = P_VERTEX_INPUT_RATE_VERTEX},
    };

    PVertexAttributeDesc attributes[] = {
        {.location = 0,
         .binding  = 0,
         .format   = P_FORMAT_R32G32B32_SFLOAT,
         .offset   = offsetof(Vertex,   pos)},
        {.location = 1,
         .binding  = 0,
         .format   = P_FORMAT_R32G32B32_SFLOAT,
         .offset   = offsetof(Vertex, color)},
    };

    PFormat color_format = pigment_get_color_format(renderer);
    PSampleCount samples = pigment_get_sample_count(renderer);

    PPipelineDesc pipeline_desc = {
        .layout                 = layout,
        .vertex_shader          = (const uint32_t*) test_vert_spv,
        .vertex_shader_size     = (uint32_t) sizeof(test_vert_spv),
        .fragment_shader        = (const uint32_t*) test_frag_spv,
        .fragment_shader_size   = (uint32_t) sizeof(test_frag_spv),
        .color_formats          = &color_format,
        .color_format_count     = 1,
        .depth_format           = P_FORMAT_UNDEFINED,
        .polygon_mode           = P_POLYGON_MODE_FILL,
        .topology               = P_TOPOLOGY_TRIANGLE_LIST,
        .sample_count           = samples,
        .vertex_bindings        = bindings,
        .vertex_binding_count   = 1,
        .vertex_attributes      = attributes,
        .vertex_attribute_count = 2,
        .name                   = "test_vertex_input_pipeline",
    };

    if(pigment_create_graphic_pipelines(pigment, NULL, &pipeline_desc, 1, &pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipeline!\n");
        goto FREE;
    }

    Vertex triangle[3] = {
        {{-0.5f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        { {0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
    PBufferDesc vertex_buffer_desc = {
        .size   = sizeof(triangle),
        .usage  = P_BUFFER_USAGE_VERTEX | P_BUFFER_USAGE_TRANSFER_DST,
        .memory = {.required = P_MEMORY_DEVICE_LOCAL_BIT},
        .name   = "test_vertex_input_vertex_buffer",
    };
    vertex_buffer = pigment_create_buffer(pigment, &vertex_buffer_desc);
    if(vertex_buffer == NULL)
    {
        fprintf(stderr, "Failed to create vertex buffer!\n");
        goto FREE;
    }

    PBufferUploadDesc upload = {.dst = vertex_buffer, .data = triangle, .size = sizeof(triangle), .offset = 0};
    if(pigment_std_buffer_upload(pigment, pool, NULL, &upload, 1, NULL) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to upload vertex buffer!\n");
        goto FREE;
    }

    SDL_ShowWindow(window);

    PBool running       = P_TRUE;
    uint32_t frame_slot = 0;

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

        pigment_wait_frame_ready(pigment, renderer, frame_slot);

        PFrame* frame;
        PResult acquire_result = pigment_begin_frame_context(pigment, renderer, frame_slot, UINT64_MAX, &frame);
        if(acquire_result == PIGMENT_RECREATE_REQUIRED)
        {
            pigment_recreate_swapchain(pigment, renderer);
            continue;
        }
        if(acquire_result != PIGMENT_SUCCESS)
        {
            break;
        }

        PCommandBuffer* cmd = frame_cmds[pigment_frame_slot(frame)];
        pigment_begin_recording(pigment, cmd, P_CMD_BUFFER_USAGE_DEFAULT, NULL);

        PSwapchainPassDesc pass_desc = {
            .clear_color = {0.01f, 0.01f, 0.01f, 1.0f},
            .no_depth    = P_TRUE,
        };
        pigment_cmd_begin_swapchain_pass(pigment, cmd, frame, &pass_desc);

        pigment_bind_pipeline(pigment, cmd, pipeline);
        PBuffer* vertex_buffers[] = {vertex_buffer};
        pigment_cmd_bind_vertex_buffers(pigment, cmd, 0, 1, vertex_buffers, NULL);
        pigment_cmd_draw(pigment, cmd, 3, 1, 0, 0);

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
        frame_slot = (frame_slot + 1) % pigment_max_frames_in_flight(pigment);
    }

    error_code = 0;

FREE:
    if(vertex_buffer != NULL)
    {
        pigment_destroy_buffer(pigment, vertex_buffer);
    }
    if(pipeline != NULL)
    {
        pigment_destroy_pipeline(pigment, pipeline);
    }
    if(layout != NULL)
    {
        pigment_destroy_layout(pigment, layout);
    }
    if(renderer != NULL)
    {
        pigment_renderer_destroy(pigment, renderer);
    }
    if(pool != NULL)
    {
        pigment_destroy_command_pool(pigment, pool);
    }
    if(pigment != NULL)
    {
        destroy_pigment(pigment);
    }
    if(window != NULL)
    {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();

    return error_code;
}
