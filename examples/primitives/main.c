#include <pigment/pigment_std.h>
#include <pigment/pigment_sdl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/fps_camera.h"
#include "wave_comp_spv.h"

#define SPHERE_INSTANCE_COUNT 100

typedef struct WavePushConstants {
    uint64_t heights_address;
    float time;
    uint32_t count;
} WavePushConstants;

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Primitives",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    SDL_Window* window                                    = NULL;
    Pigment* pigment                                      = NULL;
    PCommandPool* pool                                    = NULL;
    PWindowRenderer* renderer                             = NULL;
    PStdBindless* bindless                                = NULL;
    PCamera* camera                                       = NULL;
    PInstanceRing* ring                                   = NULL;
    PMaterials* materials                                 = NULL;
    PLights* lights                                       = NULL;
    PPipeline* pipeline                                   = NULL;
    PPipeline* skybox_pipeline                            = NULL;
    PPipeline* crt_pipeline                               = NULL;
    PStdCanvas* canvas                                    = NULL;
    PRenderTarget* rt                                     = NULL;
    uint32_t cubemap_slot                                 = 0;
    uint32_t rt_slot                                      = 0;
    PMeshBuffers* gpu_cube                                = NULL;
    PMeshBuffers* gpu_sphere                              = NULL;
    PMeshBuffers* gpu_plane                               = NULL;
    PMeshBuffers* gpu_quad                                = NULL;
    PDrawCall draw_calls[5]                               = {0};
    PInstanceData instances[4]                            = {0};
    PInstanceData sphere_instances[SPHERE_INSTANCE_COUNT] = {0};
    PLayout* wave_layout                                  = NULL;
    PPipeline* wave_pipeline                              = NULL;
    PBuffer* heights_buffer                               = NULL;
    PCommandBuffer* compute_cmd                           = NULL;
    int error_code                                        = 1;

    if(!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Primitives + Instancing", 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(window == NULL)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    PigmentLoggerCreateInfo loggers[] = {
        {
         .severity_filter = PIGMENT_LOG_TRACE_BIT | PIGMENT_LOG_DEBUG_BIT | PIGMENT_LOG_INFO_BIT | PIGMENT_LOG_WARN_BIT | PIGMENT_LOG_ERROR_BIT,
         .type_filter     = PIGMENT_LOG_TYPE_GENERAL_BIT | PIGMENT_LOG_TYPE_VALIDATION_BIT | PIGMENT_LOG_TYPE_PERFORMANCE_BIT,
         .callback        = pigment_default_log_callback,
         .user_data       = NULL,
         },
    };
    PigmentConfig config = {
        .loggers           = loggers,
        .logger_count      = sizeof(loggers) / sizeof(loggers[0]),
        .enable_validation = P_TRUE,
    };

    pigment = init_pigment(&app_info, &config);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
        goto FREE;
    }

    PCommandPoolDesc pool_desc = {
        .queue_flags = P_QUEUE_GRAPHICS_BIT,
        .flags       = P_COMMAND_POOL_FLAG_RESET_BUFFER,
        .name        = "primitives_pool",
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
        .width        = (uint32_t) win_w,
        .height       = (uint32_t) win_h,
        .present_mode = P_PRESENT_MODE_FIFO,
        .samples      = P_SAMPLE_COUNT_8,
    };

    renderer = pigment_renderer_create(pigment, pool, &handles, &swapchain_desc);
    if(renderer == NULL)
    {
        fprintf(stderr, "Failed to create renderer!\n");
        goto FREE;
    }

    bindless = pigment_std_create_bindless(pigment, PIGMENT_DEFAULT_MAX_IMAGES, PIGMENT_DEFAULT_MAX_SAMPLERS, PIGMENT_DEFAULT_MAX_CUBEMAPS, PIGMENT_DEFAULT_MAX_RENDER_TARGETS);
    if(bindless == NULL)
    {
        fprintf(stderr, "Failed to create bindless!\n");
        goto FREE;
    }

    ring = pigment_std_create_instance_ring(pigment, 4096);
    if(ring == NULL)
    {
        fprintf(stderr, "Failed to create instance ring!\n");
        goto FREE;
    }

    materials = pigment_std_create_materials(pigment, 64);
    if(materials == NULL)
    {
        fprintf(stderr, "Failed to create materials!\n");
        goto FREE;
    }

    lights = pigment_std_create_lights(pigment, 16);
    if(lights == NULL)
    {
        fprintf(stderr, "Failed to create lights!\n");
        goto FREE;
    }

    PLightDesc sun = {
        .type      = P_LIGHT_TYPE_DIRECTIONAL,
        .direction = {-0.4f, -1.0f, -0.3f},
        .color     = { 1.0f, 0.95f, 0.85f},
        .intensity = 10.0f,
    };
    pigment_std_light_create(pigment, lights, &sun);

    uint32_t mat_default = pigment_std_material_create(
        pigment,
        materials,
        &(PMaterialDesc) {
            .base_color_factor = {1.0f, 1.0f, 1.0f, 1.0f},
            .emissive_factor   = {0.0f, 0.0f, 0.0f, 0.0f},
            .metallic_factor   = 1.0f,
            .roughness_factor  = 1.0f,
            .normal_scale      = 1.0f,
            .occlusion_scale   = 1.0f,
    }
    );
    if(mat_default == UINT32_MAX)
    {
        fprintf(stderr, "Failed to create default material!\n");
        goto FREE;
    }

    PVec3 camera_position = {0.0f, 1.5f, 6.0f};
    camera               = pigment_std_create_camera(pigment);
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }

    FPSCameraState fps_state = fps_camera_state_init(camera, window, camera_position);
    SDL_SetWindowRelativeMouseMode(window, P_TRUE);

    PFormat color_format    = pigment_get_color_format(renderer);
    PFormat depth_format    = pigment_get_depth_format(renderer);
    PSampleCount rt_samples = P_SAMPLE_COUNT_4;

    {
        const uint32_t face_size              = 64;
        const uint32_t face_pixels            = face_size * face_size;
        const unsigned char face_colors[6][4] = {
            {255,   0,   0, 255},
            {  0, 255, 255, 255},
            {  0, 255,   0, 255},
            {255,   0, 255, 255},
            {  0,   0, 255, 255},
            {255, 255,   0, 255},
        };
        unsigned char* face_data[6] = {0};
        PBool ok                    = P_TRUE;
        for(uint32_t f = 0; f < 6 && ok; f++)
        {
            face_data[f] = malloc(face_pixels * 4);
            if(face_data[f] == NULL)
            {
                ok = P_FALSE;
                break;
            }
            for(uint32_t i = 0; i < face_pixels; i++)
            {
                memcpy(&face_data[f][i * 4], face_colors[f], 4);
            }
        }
        if(ok)
        {
            cubemap_slot = pigment_std_add_cubemap(pigment, bindless, (const unsigned char**) face_data, face_size, face_size, P_FORMAT_R8G8B8A8_UNORM);
        }
        for(uint32_t f = 0; f < 6; f++)
        {
            free(face_data[f]);
        }
    }

    rt = pigment_std_create_render_target(pigment, &(PRenderTargetDesc) {
                                                       .renderer    = renderer,
                                                       .colors      = (PAttachmentDesc[]) {{.format = color_format, .scale = 1.0f, .samples = rt_samples}},
                                                       .color_count = 1,
                                                       .depth       = {.format = depth_format, .scale = 1.0f, .samples = rt_samples},
    });
    if(rt == NULL)
    {
        fprintf(stderr, "Failed to create render targets!\n");
        goto FREE;
    }

    rt_slot = pigment_std_register_render_target(pigment, bindless, rt);

    PPipelineDesc desc = default_graphic_pipeline_desc(pigment, bindless, &color_format, 1, depth_format, rt_samples);
    if(pigment_create_graphic_pipelines(pigment, NULL, &desc, 1, &pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipeline!\n");
        goto FREE;
    }

    PPipelineDesc skybox_desc = default_skybox_pipeline_desc(pigment, bindless, &color_format, 1, depth_format, rt_samples);
    if(pigment_create_graphic_pipelines(pigment, NULL, &skybox_desc, 1, &skybox_pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create skybox pipeline!\n");
        goto FREE;
    }

    PPipelineDesc crt_desc = default_crt_pipeline_desc(pigment, bindless, &color_format, 1, depth_format, pigment_get_sample_count(renderer));
    if(pigment_create_graphic_pipelines(pigment, NULL, &crt_desc, 1, &crt_pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create CRT pipeline!\n");
        goto FREE;
    }

    PLayoutDesc wave_layout_desc = {
        .push_size   = sizeof(WavePushConstants),
        .push_stages = P_SHADER_STAGE_COMPUTE_BIT,
        .name        = "wave_layout",
    };
    wave_layout = pigment_create_layout(pigment, &wave_layout_desc);
    if(wave_layout == NULL)
    {
        fprintf(stderr, "Failed to create wave layout!\n");
        goto FREE;
    }

    PComputePipelineDesc wave_desc = {
        .layout           = wave_layout,
        .compute_shader      = (const uint32_t*) wave_comp_spv,
        .compute_shader_size = (uint32_t) sizeof(wave_comp_spv),
        .name             = "wave_compute",
    };
    if(pigment_create_compute_pipelines(pigment, NULL, &wave_desc, 1, &wave_pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create wave compute pipeline!\n");
        goto FREE;
    }

    PBufferDesc heights_desc = {
        .size   = sizeof(float) * SPHERE_INSTANCE_COUNT,
        .usage  = P_BUFFER_USAGE_STORAGE | P_BUFFER_USAGE_SHADER_ADDRESS,
        .memory = {.required = P_MEMORY_HOST_VISIBLE_BIT, .preferred = P_MEMORY_HOST_COHERENT_BIT | P_MEMORY_DEVICE_LOCAL_BIT},
        .name   = "heights_buffer",
    };
    heights_buffer = pigment_create_buffer(pigment, &heights_desc);
    if(heights_buffer == NULL)
    {
        fprintf(stderr, "Failed to create heights buffer!\n");
        goto FREE;
    }

    if(pigment_create_command_buffers(pigment, pool, P_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &compute_cmd) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create compute command buffer!\n");
        goto FREE;
    }

    canvas = pigment_std_create_canvas(pigment, color_format, rt_samples);
    if(canvas == NULL)
    {
        fprintf(stderr, "Failed to create canvas!\n");
        goto FREE;
    }

    PMeshData cube    = pigment_cube_mesh(pigment);
    uint32_t cube_idx = cube.index_count;
    gpu_cube          = pigment_upload_mesh_data(pigment, pool, &cube);
    pigment_free_mesh_data(pigment, &cube);

    PMeshData sphere    = pigment_sphere_mesh(pigment, 32, 32);
    uint32_t sphere_idx = sphere.index_count;
    gpu_sphere          = pigment_upload_mesh_data(pigment, pool, &sphere);
    pigment_free_mesh_data(pigment, &sphere);

    PMeshData plane    = pigment_plane_mesh(pigment, 8);
    uint32_t plane_idx = plane.index_count;
    gpu_plane          = pigment_upload_mesh_data(pigment, pool, &plane);
    pigment_free_mesh_data(pigment, &plane);

    PMeshData quad    = pigment_quad_mesh(pigment);
    uint32_t quad_idx = quad.index_count;
    gpu_quad          = pigment_upload_mesh_data(pigment, pool, &quad);
    pigment_free_mesh_data(pigment, &quad);

    if(gpu_cube == NULL || gpu_sphere == NULL || gpu_plane == NULL || gpu_quad == NULL)
    {
        fprintf(stderr, "Failed to upload primitives!\n");
        goto FREE;
    }

    mat4_identity(instances[0].transform);
    mat4_translate(instances[0].transform, (PVec3) {-3.0f, 0.0f, 0.0f});
    instances[0].material_id     = mat_default;
    draw_calls[0].mesh           = gpu_cube;
    draw_calls[0].instances      = &instances[0];
    draw_calls[0].instance_count = 1;
    draw_calls[0].index_count    = cube_idx;

    mat4_identity(instances[1].transform);
    mat4_translate(instances[1].transform, (PVec3) {-1.0f, 0.0f, 0.0f});
    instances[1].material_id     = mat_default;
    draw_calls[1].mesh           = gpu_sphere;
    draw_calls[1].instances      = &instances[1];
    draw_calls[1].instance_count = 1;
    draw_calls[1].index_count    = sphere_idx;

    mat4_identity(instances[2].transform);
    mat4_translate(instances[2].transform, (PVec3) {0.0f, -2.0f, 0.0f});
    mat4_scale(instances[2].transform, (PVec3) {10.0f, 1.0f, 10.0f});
    instances[2].material_id     = mat_default;
    draw_calls[2].mesh           = gpu_plane;
    draw_calls[2].instances      = &instances[2];
    draw_calls[2].instance_count = 1;
    draw_calls[2].index_count    = plane_idx;

    mat4_identity(instances[3].transform);
    mat4_translate(instances[3].transform, (PVec3) {2.0f, 0.0f, 0.0f});
    instances[3].material_id     = mat_default;
    draw_calls[3].mesh           = gpu_quad;
    draw_calls[3].instances      = &instances[3];
    draw_calls[3].instance_count = 1;
    draw_calls[3].index_count    = quad_idx;

    for(uint32_t i = 0; i < 100; i++)
    {
        sphere_instances[i].material_id = mat_default;
    }
    draw_calls[4].mesh           = gpu_sphere;
    draw_calls[4].instances      = sphere_instances;
    draw_calls[4].instance_count = 100;
    draw_calls[4].index_count    = sphere_idx;

    SDL_ShowWindow(window);

    Uint64 start_ticks = SDL_GetPerformanceCounter();
    double ticks_freq  = (double) SDL_GetPerformanceFrequency();

    PBool running = P_TRUE;
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
            fps_camera_handle_sdl_event(camera, &fps_state, &event);
        }

        fps_camera_update(camera, &fps_state);

        float t = (float) ((double) (SDL_GetPerformanceCounter() - start_ticks) / ticks_freq);

        pigment_begin_recording(pigment, compute_cmd, P_CMD_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
        pigment_bind_pipeline(pigment, compute_cmd, wave_pipeline);
        WavePushConstants push = {
            .heights_address = pigment_buffer_address(heights_buffer),
            .time            = t,
            .count           = SPHERE_INSTANCE_COUNT,
        };
        pigment_cmd_push_constants(pigment, compute_cmd, wave_pipeline, 0, sizeof(push), &push);
        pigment_cmd_dispatch(pigment, compute_cmd, (SPHERE_INSTANCE_COUNT + 63) / 64, 1, 1);

        PMemoryBarrier compute_to_host = {
            .src = {P_PIPELINE_STAGE_COMPUTE_SHADER_BIT, P_MEMORY_ACCESS_SHADER_STORAGE_WRITE_BIT},
            .dst = {          P_PIPELINE_STAGE_HOST_BIT,            P_MEMORY_ACCESS_HOST_READ_BIT},
        };
        pigment_cmd_memory_barriers(pigment, compute_cmd, &compute_to_host, 1);
        pigment_end_recording(pigment, compute_cmd);

        PSubmitHandle compute_handle = {0};
        if(pigment_queue_submit(pigment, &(PSubmit) {.cmds = &compute_cmd, .cmd_count = 1}, 1, &compute_handle) == PIGMENT_SUCCESS)
        {
            pigment_submit_wait(pigment, compute_handle);
        }

        const float* heights = (const float*) pigment_buffer_mapped(heights_buffer);
        for(uint32_t i = 0; i < 10; i++)
        {
            for(uint32_t j = 0; j < 10; j++)
            {
                uint32_t idx = i + 10 * j;
                mat4_identity(sphere_instances[idx].transform);
                mat4_translate(sphere_instances[idx].transform, (PVec3) {((float) i - 4.5f) * 2.0f, heights[idx], ((float) j - 4.5f) * 2.0f - 12.0f});
            }
        }

        pigment_wait_frame_ready(pigment, renderer);

        PCommandBuffer* cmd = pigment_begin_frame(pigment, renderer);
        if(cmd == NULL)
        {
            pigment_recreate_swapchain(pigment, renderer);
            continue;
        }

        PAttachmentRef scene_colors[] = {pigment_std_render_target_color_ref(rt, 0)};
        scene_colors[0].store_op      = P_STORE_OP_STORE;
        scene_colors[0].final_layout  = P_IMAGE_LAYOUT_COLOR_ATTACHMENT;

        PRenderPassDesc scene_pass = {
            .color_attachments = scene_colors,
            .color_count       = 1,
            .depth_attachment  = pigment_std_render_target_depth_ref(rt),
            .clear_color       = {0.0f, 0.0f, 0.0f, 1.0f},
            .depth_clear_value = 0.0f,
        };
        pigment_begin_render_pass(pigment, cmd, &scene_pass);

        pigment_bind_pipeline(pigment, cmd, pipeline);
        pigment_draw(pigment, renderer, bindless, ring, materials, lights, camera, pipeline, draw_calls, 5);

        pigment_bind_pipeline(pigment, cmd, skybox_pipeline);
        pigment_std_draw_skybox(pigment, renderer, bindless, skybox_pipeline, camera, cubemap_slot, 0);

        pigment_end_render_pass(pigment, cmd, &scene_pass);

        PAttachmentRef hud_colors[] = {pigment_std_render_target_color_ref(rt, 0)};
        hud_colors[0].load_op       = P_LOAD_OP_LOAD;

        PRenderPassDesc hud_pass = {
            .color_attachments = hud_colors,
            .color_count       = 1,
        };
        pigment_begin_render_pass(pigment, cmd, &hud_pass);

        pigment_std_canvas_begin(pigment, canvas, cmd);
        float crosshair_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        pigment_std_canvas_rect_anchor(pigment, canvas, cmd, renderer, P_STD_CANVAS_ANCHOR_CENTER, 0, 0, 24, 2, crosshair_color);
        pigment_std_canvas_rect_anchor(pigment, canvas, cmd, renderer, P_STD_CANVAS_ANCHOR_CENTER, 0, 0, 2, 24, crosshair_color);

        pigment_end_render_pass(pigment, cmd, &hud_pass);

        pigment_begin_swapchain_pass(pigment, renderer, NULL);

        pigment_bind_pipeline(pigment, cmd, crt_pipeline);
        pigment_std_draw_crt(pigment, renderer, bindless, crt_pipeline, rt_slot, 1, t);

        pigment_end_swapchain_pass(renderer);

        pigment_end_recording_frame(pigment, renderer);
        pigment_queue_submit_frame(pigment, renderer, NULL);
        pigment_present(pigment, renderer);
    }

    error_code = 0;

FREE:
    if(pigment != NULL)
    {
        pigment_wait_idle(pigment);
        if(gpu_cube != NULL)
        {
            pigment_std_destroy_mesh(pigment, gpu_cube);
        }
        if(gpu_sphere != NULL)
        {
            pigment_std_destroy_mesh(pigment, gpu_sphere);
        }
        if(gpu_plane != NULL)
        {
            pigment_std_destroy_mesh(pigment, gpu_plane);
        }
        if(gpu_quad != NULL)
        {
            pigment_std_destroy_mesh(pigment, gpu_quad);
        }
    }
    pigment_destroy_pipeline(pigment, pipeline);
    pigment_destroy_pipeline(pigment, skybox_pipeline);
    pigment_destroy_pipeline(pigment, crt_pipeline);
    if(compute_cmd != NULL)
    {
        pigment_destroy_command_buffers(pigment, &compute_cmd, 1);
    }
    pigment_destroy_pipeline(pigment, wave_pipeline);
    pigment_destroy_layout(pigment, wave_layout);
    pigment_destroy_buffer(pigment, heights_buffer);
    pigment_std_destroy_canvas(pigment, canvas);
    pigment_renderer_destroy(pigment, renderer);
    pigment_destroy_command_pool(pigment, pool);
    pigment_std_destroy_render_target(pigment, rt);
    pigment_std_destroy_camera(pigment, camera);
    pigment_std_destroy_instance_ring(pigment, ring);
    pigment_std_destroy_lights(pigment, lights);
    pigment_std_destroy_materials(pigment, materials);
    pigment_std_destroy_bindless(pigment, bindless);
    destroy_pigment(pigment);

    if(window != NULL)
    {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();

    return error_code;
}
