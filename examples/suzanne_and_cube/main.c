#include <pigment_std.h>
#include <pigment_sdl.h>

#include "../common/fps_camera.h"

#define EXAMPLE_NAME "suzanne_and_cube"

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Suzanne and Cube",
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
    PPipeline* pipeline              = NULL;
    PPipeline* gizmo_pipeline        = NULL;
    PMeshBuffers* gizmo_sphere       = NULL;
    uint32_t gizmo_sphere_indices    = 0;
    PMeshBuffers* gpu_mesh           = NULL;
    MeshAsset* asset                 = NULL;
    PMeshBuffers* gpu_mesh2          = NULL;
    MeshAsset* asset2                = NULL;
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

    window = SDL_CreateWindow("Suzanne and Cube", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
        .name        = "suzanne_pool",
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

    ring = pigment_std_create_instance_ring(pigment, 4096);
    if(ring == NULL)
    {
        fprintf(stderr, "Failed to create instance ring!\n");
        goto FREE;
    }

    materials = pigment_std_create_materials(pigment, 2048);
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

    PLightDesc light_point = {
        .type      = P_LIGHT_TYPE_POINT,
        .position  = {0.0f, 0.0f, 2.0f},
        .color     = {1.0f, 0.2f, 0.2f},
        .intensity = 20.0f,
        .range     = 3.0f,
    };
    pigment_std_light_create(pigment, lights, &light_point);

    vec3 ambient_color = {0.0f, 0.0f, 0.0f};
    pigment_std_set_ambient(lights, ambient_color);

    vec3 camera_position = {1.5f, 0.0f, 5.0f};
    camera               = pigment_std_create_camera(pigment);
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }

    FPSCameraState fps_state = fps_camera_state_init(camera, window, camera_position);
    SDL_SetWindowRelativeMouseMode(window, P_TRUE);

    PFormat color_format        = pigment_get_color_format(renderer);
    PSampleCount samples        = pigment_get_sample_count(renderer);
    PPipelineDesc pipeline_desc = default_graphic_pipeline_desc(pigment, bindless, &color_format, 1, pigment_get_depth_format(renderer), samples);
    PPipelineBuild* build       = pigment_pipeline_build_from_desc(pigment, &pipeline_desc);
    if(build == NULL)
    {
        fprintf(stderr, "Failed to build pipeline!\n");
        goto FREE;
    }
    if(pigment_create_graphic_pipelines(pigment, &build, 1, &pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipelines!\n");
        goto FREE;
    }

    PPipelineDesc gizmo_desc    = default_light_gizmo_pipeline_desc(pigment, bindless, &color_format, 1, pigment_get_depth_format(renderer), samples);
    PPipelineBuild* gizmo_build = pigment_pipeline_build_from_desc(pigment, &gizmo_desc);
    if(gizmo_build == NULL || pigment_create_graphic_pipelines(pigment, &gizmo_build, 1, &gizmo_pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create gizmo pipeline!\n");
        goto FREE;
    }

    PMeshData sphere_data = pigment_sphere_mesh(16, 16);
    gizmo_sphere_indices  = sphere_data.index_count;
    gizmo_sphere          = pigment_upload_mesh_data(pigment, pool, &sphere_data);
    pigment_free_mesh_data(&sphere_data);
    if(gizmo_sphere == NULL)
    {
        fprintf(stderr, "Failed to upload gizmo sphere!\n");
        goto FREE;
    }

    // SUZANNE

    asset = load_gltf_mesh(pigment, "examples/" EXAMPLE_NAME "/models/Suzanne.gltf");
    if(asset == NULL)
    {
        fprintf(stderr, "Failed to load glTF!\n");
        goto FREE;
    }

    if(upload_mesh_textures(pigment, bindless, asset, materials) != PIGMENT_SUCCESS)
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
        glm_mat4_copy(asset->node_transforms[s->node_index], instance_storage[i].transform);
        instance_storage[i].material_id = s->material_id;
        draw_calls[i].mesh              = gpu_mesh;
        draw_calls[i].instances         = &instance_storage[i];
        draw_calls[i].instance_count    = 1;
        draw_calls[i].first_index       = s->start_index;
        draw_calls[i].index_count       = s->index_count;
    }
    free_mesh_asset(asset);
    asset = NULL;

    // CUBE

    uint32_t mat_white = pigment_std_material_create(
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

    PMeshData cube_data = pigment_cube_mesh();
    uint32_t cube_idx   = cube_data.index_count;
    gpu_mesh2           = pigment_upload_mesh_data(pigment, pool, &cube_data);
    pigment_free_mesh_data(&cube_data);
    if(gpu_mesh2 == NULL)
    {
        fprintf(stderr, "Failed to upload primitive cube!\n");
        goto FREE;
    }

    uint32_t draw_count2 = 1;
    draw_calls2          = calloc(1, sizeof(PDrawCall));
    instance_storage2    = calloc(1, sizeof(PInstanceData));
    if(draw_calls2 == NULL || instance_storage2 == NULL)
    {
        fprintf(stderr, "Failed to allocate draw_calls2 or instance_storage2!\n");
        goto FREE;
    }

    glm_mat4_identity(instance_storage2[0].transform);
    glm_translate(instance_storage2[0].transform, (vec3) {2.0f, 0.0f, 0.0f});
    instance_storage2[0].material_id = mat_white;
    draw_calls2[0].mesh              = gpu_mesh2;
    draw_calls2[0].instances         = &instance_storage2[0];
    draw_calls2[0].instance_count    = 1;
    draw_calls2[0].first_index       = 0;
    draw_calls2[0].index_count       = cube_idx;

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
            continue;
        }

        pigment_begin_swapchain_pass(pigment, renderer);

        pigment_bind_pipeline(pigment, cmd, pipeline);
        pigment_draw(pigment, renderer, bindless, ring, materials, lights, camera, pipeline, draw_calls, draw_count);
        pigment_draw(pigment, renderer, bindless, ring, materials, lights, camera, pipeline, draw_calls2, draw_count2);

        pigment_bind_pipeline(pigment, cmd, gizmo_pipeline);
        pigment_std_draw_light_gizmos(pigment, renderer, gizmo_pipeline, camera, lights, gizmo_sphere, gizmo_sphere_indices, 0.15f);

        pigment_end_swapchain_pass(renderer);

        pigment_end_recording_frame(pigment, renderer);
        pigment_queue_submit_frame(pigment, renderer);
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
        if(gizmo_sphere != NULL)
        {
            pigment_std_destroy_mesh(pigment, gizmo_sphere);
        }
    }
    free(draw_calls);
    free(draw_calls2);
    free(instance_storage);
    free(instance_storage2);
    free_mesh_asset(asset);
    free_mesh_asset(asset2);
    pigment_destroy_pipeline(pigment, pipeline);
    pigment_destroy_pipeline(pigment, gizmo_pipeline);
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
