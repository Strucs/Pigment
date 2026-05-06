#include <pigment_std.h>
#include <pigment_sdl.h>

#include "../common/fps_camera.h"

#define MODELS_DIR "examples/suzanne_and_cube/models"

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Multi Pipeline",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PWindowInfo window_info = {
        .width                  = 1280,
        .height                 = 720,
        .title                  = "Suzanne and Cube (opaque + additive)",
        .preferred_present_mode = P_PRESENT_MODE_DEFAULT,
        .flags                  = P_WINDOW_FLAGS_DEFAULT,
    };

    Pigment* pigment                 = NULL;
    PStdBindless* bindless           = NULL;
    PCamera* camera                  = NULL;
    PInstanceRing* ring              = NULL;
    PMaterials* materials            = NULL;
    PLights* lights                  = NULL;
    PPipeline* pipelines[2]          = {NULL, NULL};
    PMeshBuffers* gpu_mesh           = NULL;
    MeshAsset* asset                 = NULL;
    PMeshBuffers* gpu_mesh2          = NULL;
    MeshAsset* asset2                = NULL;
    PDrawCall* draw_calls            = NULL;
    PDrawCall* draw_calls2           = NULL;
    PInstanceData* instance_storage  = NULL;
    PInstanceData* instance_storage2 = NULL;
    PPipelineBuild* builds[2]        = {NULL, NULL};
    int error_code                   = 1;

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

    pigment = init_pigment(&app_info, &window_info, &config);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
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
        .intensity = 1.0f,
    };
    pigment_std_light_create(pigment, lights, &sun);

    vec3 camera_position = {1.5f, 0.0f, 5.0f};
    camera               = pigment_std_create_camera(pigment);
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }

    FPSCameraState fps_state = fps_camera_state_init(camera, pigment_get_sdl_window(pigment, 0), camera_position);
    SDL_SetWindowRelativeMouseMode(pigment_get_sdl_window(pigment, 0), P_TRUE);

    PWindowRenderer* renderer = pigment_get_window_renderer(pigment, 0);
    PFormat color_format      = pigment_get_color_format(renderer);
    PFormat depth_format      = pigment_get_depth_format(renderer);

    PPipelineDesc desc_opaque   = default_graphic_pipeline_desc(pigment, bindless, &color_format, 1, depth_format);
    PPipelineDesc desc_additive = default_graphic_pipeline_desc(pigment, bindless, &color_format, 1, depth_format);

    PBlendMode additive_blend      = P_BLEND_MODE_ADDITIVE;
    desc_additive.blend_modes      = &additive_blend;
    desc_additive.blend_mode_count = 1;

    builds[0] = pigment_pipeline_build_from_desc(pigment, &desc_opaque);
    builds[1] = pigment_pipeline_build_from_desc(pigment, &desc_additive);

    if(builds[0] == NULL || builds[1] == NULL)
    {
        fprintf(stderr, "Failed to build pipelines!\n");
        goto FREE;
    }

    if(pigment_create_graphic_pipelines(pigment, builds, 2, pipelines) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipelines!\n");
        goto FREE;
    }

    // SUZANNE (opaque)

    asset = load_gltf_mesh(pigment, MODELS_DIR "/Suzanne.gltf");
    if(asset == NULL)
    {
        fprintf(stderr, "Failed to load Suzanne!\n");
        goto FREE;
    }

    if(upload_mesh_textures(pigment, bindless, asset, materials) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to upload mesh textures/materials!\n");
        goto FREE;
    }

    gpu_mesh = pigment_std_upload_mesh(
        pigment,
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

    // CUBE (additive blend)

    asset2 = load_gltf_mesh(pigment, MODELS_DIR "/BoxVertexColors.glb");
    if(asset2 == NULL)
    {
        fprintf(stderr, "Failed to load cube!\n");
        goto FREE;
    }

    if(upload_mesh_textures(pigment, bindless, asset2, materials) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to upload mesh textures/materials!\n");
        goto FREE;
    }

    gpu_mesh2 = pigment_std_upload_mesh(
        pigment,
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
        glm_mat4_copy(asset2->node_transforms[s->node_index], instance_storage2[i].transform);
        vec3 translation = {2.f, 0.0f, 0.0f};
        glm_translate(instance_storage2[i].transform, translation);
        instance_storage2[i].material_id = s->material_id;
        draw_calls2[i].mesh              = gpu_mesh2;
        draw_calls2[i].instances         = &instance_storage2[i];
        draw_calls2[i].instance_count    = 1;
        draw_calls2[i].first_index       = s->start_index;
        draw_calls2[i].index_count       = s->index_count;
    }
    free_mesh_asset(asset2);
    asset2 = NULL;

    pigment_show_window(pigment, 0);

    while(pigment_should_run(pigment))
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            pigment_handle_sdl_event(pigment, &event);
            fps_camera_handle_sdl_event(camera, &fps_state, &event);
        }

        fps_camera_update(camera, &fps_state);

        pigment_wait_frame_ready(pigment, 0);

        PCommandBuffer* cmd = pigment_begin_frame(pigment, 0);
        if(cmd == NULL)
        {
            continue;
        }

        pigment_begin_swapchain_pass(pigment, 0);

        pigment_bind_pipeline(pigment, cmd, pipelines[0]);
        pigment_draw(pigment, 0, bindless, ring, materials, lights, camera, pipelines[0], draw_calls, draw_count);

        pigment_bind_pipeline(pigment, cmd, pipelines[1]);
        pigment_cmd_set_depth(pigment, cmd, P_TRUE, P_FALSE, P_COMPARE_OP_GREATER);
        pigment_draw(pigment, 0, bindless, ring, materials, lights, camera, pipelines[1], draw_calls2, draw_count2);

        pigment_end_swapchain_pass(pigment, 0);

        pigment_end_frame(pigment, 0);
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
    free_mesh_asset(asset);
    free_mesh_asset(asset2);
    pigment_std_destroy_camera(pigment, camera);
    pigment_std_destroy_instance_ring(pigment, ring);
    pigment_std_destroy_lights(pigment, lights);
    pigment_std_destroy_materials(pigment, materials);
    pigment_std_destroy_bindless(pigment, bindless);
    destroy_pigment(pigment);

    return error_code;
}
