#include <pigment/pigment_std.h>
#include <pigment/pigment_sdl.h>
#include <pigment/pigment_gltf.h>

#include <stdio.h>
#include <stdlib.h>

#include "../common/fps_camera.h"

#define MODELS_DIR "examples/suzanne_and_cube/models"

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Multi Pipeline",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    SDL_Window* window               = NULL;
    Pigment* pigment                 = NULL;
    PCommandPool* pool               = NULL;
    PWindowRenderer* renderer        = NULL;
    PStdBindless* bindless           = NULL;
    PCamera* camera                  = NULL;
    PInstanceRing* ring              = NULL;
    PMaterials* materials            = NULL;
    PLights* lights                  = NULL;
    PPipeline* pipelines[2]          = {NULL, NULL};
    PMeshBuffers* gpu_mesh           = NULL;
    PGltfMesh* asset                 = NULL;
    PMeshBuffers* gpu_mesh2          = NULL;
    PGltfMesh* asset2                = NULL;
    PDrawCall* draw_calls            = NULL;
    PDrawCall* draw_calls2           = NULL;
    PInstanceData* instance_storage  = NULL;
    PInstanceData* instance_storage2 = NULL;
    int error_code                   = 1;

    if(!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Suzanne and Cube (opaque + additive)", 1280, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
        .queue_family = pigment_queue_family(pigment_get_queue(pigment, P_QUEUE_GRAPHICS_BIT)),
        .flags        = P_COMMAND_POOL_FLAG_RESET_BUFFER,
        .name         = "multi_pipeline_pool",
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

    ring = pigment_std_create_instance_ring(pigment, sizeof(PInstanceData), 4096);
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
        .intensity = 1.0f,
    };
    pigment_std_light_create(pigment, lights, &sun);

    PVec3 camera_position = {1.5f, 0.0f, 5.0f};
    camera                = pigment_std_create_camera(pigment);
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }

    FPSCameraState fps_state = fps_camera_state_init(camera, window, camera_position);
    SDL_SetWindowRelativeMouseMode(window, P_TRUE);

    PFormat color_format = pigment_get_color_format(renderer);
    PFormat depth_format = pigment_get_depth_format(renderer);
    PSampleCount samples = pigment_get_sample_count(renderer);

    PPipelineDesc desc_opaque   = default_graphic_pipeline_desc(pigment, bindless, &color_format, 1, depth_format, samples);
    PPipelineDesc desc_additive = default_graphic_pipeline_desc(pigment, bindless, &color_format, 1, depth_format, samples);

    PBlendMode additive_blend      = P_BLEND_MODE_ADDITIVE;
    desc_additive.blend_modes      = &additive_blend;
    desc_additive.blend_mode_count = 1;

    PPipelineDesc descs[2] = {desc_opaque, desc_additive};
    if(pigment_create_graphic_pipelines(pigment, NULL, descs, 2, pipelines) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipelines!\n");
        goto FREE;
    }

    // SUZANNE (opaque)

    asset = pigment_gltf_load_mesh(pigment, NULL, MODELS_DIR "/Suzanne.gltf");
    if(asset == NULL)
    {
        fprintf(stderr, "Failed to load Suzanne!\n");
        goto FREE;
    }

    if(pigment_gltf_upload_textures(pigment, bindless, asset, materials) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to upload mesh textures/materials!\n");
        goto FREE;
    }

    gpu_mesh = pigment_std_upload_mesh(
        pigment,
        pool,
        asset->vertices,
        asset->vertex_count * sizeof(PVertex),
        asset->indices,
        asset->index_count
    );

    uint32_t draw_count = asset->surface_count;
    draw_calls          = calloc(draw_count, sizeof(PDrawCall));
    instance_storage    = calloc(draw_count, sizeof(PInstanceData));
    if(draw_calls == NULL || instance_storage == NULL)
    {
        fprintf(stderr, "Failed to allocate draw_calls or instance_storage!\n");
        goto FREE;
    }

    for(uint32_t i = 0; i < draw_count; i++)
    {
        PRawSurface* s = &asset->surfaces[i];
        mat4_copy(asset->node_transforms[s->node_index], instance_storage[i].transform);
        instance_storage[i].material_id = s->material_id;
        draw_calls[i].mesh              = gpu_mesh;
        draw_calls[i].instances         = &instance_storage[i];
        draw_calls[i].instance_count    = 1;
        draw_calls[i].first_index       = s->start_index;
        draw_calls[i].index_count       = s->index_count;
    }
    pigment_gltf_free_mesh(pigment, asset);
    asset = NULL;

    // CUBE (additive blend)

    asset2 = pigment_gltf_load_mesh(pigment, NULL, MODELS_DIR "/BoxVertexColors.glb");
    if(asset2 == NULL)
    {
        fprintf(stderr, "Failed to load cube!\n");
        goto FREE;
    }

    if(pigment_gltf_upload_textures(pigment, bindless, asset2, materials) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to upload mesh textures/materials!\n");
        goto FREE;
    }

    gpu_mesh2 = pigment_std_upload_mesh(
        pigment,
        pool,
        asset2->vertices,
        asset2->vertex_count * sizeof(PVertex),
        asset2->indices,
        asset2->index_count
    );

    uint32_t draw_count2 = asset2->surface_count;
    draw_calls2          = calloc(draw_count2, sizeof(PDrawCall));
    instance_storage2    = calloc(draw_count2, sizeof(PInstanceData));
    if(draw_calls2 == NULL || instance_storage2 == NULL)
    {
        fprintf(stderr, "Failed to allocate draw_calls2 or instance_storage2!\n");
        goto FREE;
    }

    for(uint32_t i = 0; i < draw_count2; i++)
    {
        PRawSurface* s = &asset2->surfaces[i];
        mat4_copy(asset2->node_transforms[s->node_index], instance_storage2[i].transform);
        PVec3 translation = {2.f, 0.0f, 0.0f};
        mat4_translate(instance_storage2[i].transform, translation);
        instance_storage2[i].material_id = s->material_id;
        draw_calls2[i].mesh              = gpu_mesh2;
        draw_calls2[i].instances         = &instance_storage2[i];
        draw_calls2[i].instance_count    = 1;
        draw_calls2[i].first_index       = s->start_index;
        draw_calls2[i].index_count       = s->index_count;
    }
    pigment_gltf_free_mesh(pigment, asset2);
    asset2 = NULL;

    SDL_ShowWindow(window);

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

        pigment_wait_frame_ready(pigment, renderer);

        PCommandBuffer* cmd = pigment_begin_frame(pigment, renderer);
        if(cmd == NULL)
        {
            pigment_recreate_swapchain(pigment, renderer);
            continue;
        }

        pigment_begin_swapchain_pass(pigment, renderer, NULL);

        pigment_bind_pipeline(pigment, cmd, pipelines[0]);
        pigment_draw(pigment, renderer, bindless, ring, materials, lights, camera, pipelines[0], draw_calls, draw_count);

        pigment_bind_pipeline(pigment, cmd, pipelines[1]);
        pigment_cmd_set_depth(pigment, cmd, P_TRUE, P_FALSE, P_COMPARE_OP_GREATER);
        pigment_draw(pigment, renderer, bindless, ring, materials, lights, camera, pipelines[1], draw_calls2, draw_count2);

        pigment_end_swapchain_pass(renderer);

        pigment_end_recording_frame(pigment, renderer);
        pigment_queue_submit_frame(pigment, renderer, NULL, NULL, 0);
        pigment_present(pigment, renderer);
    }

    error_code = 0;

FREE:
    if(pigment != NULL)
    {
        pigment_wait_idle(pigment);
        if(gpu_mesh != NULL)
        {
            pigment_std_destroy_mesh(pigment, gpu_mesh);
        }
        if(gpu_mesh2 != NULL)
        {
            pigment_std_destroy_mesh(pigment, gpu_mesh2);
        }
    }
    free(draw_calls);
    free(draw_calls2);
    free(instance_storage);
    free(instance_storage2);
    pigment_gltf_free_mesh(pigment, asset);
    pigment_gltf_free_mesh(pigment, asset2);
    pigment_destroy_pipeline(pigment, pipelines[0]);
    pigment_destroy_pipeline(pigment, pipelines[1]);
    pigment_renderer_destroy(pigment, renderer);
    pigment_destroy_command_pool(pigment, pool);
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
