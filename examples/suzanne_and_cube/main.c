#include <pigment.h>
#include <pigment_sdl.h>
#include <std/draw.h>
#include <std/gltf_loader.h>
#include <std/pipeline_loader.h>

#include "../common/fps_camera.h"

#define EXAMPLE_NAME "suzanne_and_cube"

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Suzanne and Cube",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PWindowInfo window_info = {
        .width                  = 1280,
        .height                 = 720,
        .title                  = "Suzanne and Cube",
        .preferred_present_mode = P_PRESENT_MODE_DEFAULT,
        .flags                  = P_WINDOW_FLAGS_DEFAULT,
    };

    Pigment* pigment          = NULL;
    PCamera* camera           = NULL;
    PStdDrawState* draw_state = NULL;
    PPipeline* pipeline       = NULL;
    PMeshBuffers* gpu_mesh    = NULL;
    MeshAsset* asset          = NULL;
    PMeshBuffers* gpu_mesh2   = NULL;
    MeshAsset* asset2         = NULL;
    PDrawCall* draw_calls     = NULL;
    PDrawCall* draw_calls2    = NULL;
    mat4* transform_storage   = NULL;
    mat4* transform_storage2  = NULL;
    int error_code            = 1;

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
        .enable_validation = true,
    };

    pigment = init_pigment(&app_info, &window_info, &config);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
        goto FREE;
    }

    draw_state = pigment_std_draw_init(pigment, 4096);
    if(draw_state == NULL)
    {
        fprintf(stderr, "Failed to init draw state!\n");
        goto FREE;
    }

    vec3 camera_position = {1.5f, 0.0f, 5.0f};
    camera               = pigment_create_camera();
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }

    FPSCameraState fps_state = fps_camera_state_init(camera, pigment_get_sdl_window(pigment, 0), camera_position);
    SDL_SetWindowRelativeMouseMode(pigment_get_sdl_window(pigment, 0), true);

    PWindowRenderer* renderer   = pigment_get_window_renderer(pigment, 0);
    PFormat color_format        = pigment_get_color_format(renderer);
    PPipelineDesc pipeline_desc = default_graphic_pipeline_desc(pigment, &color_format, 1, pigment_get_depth_format(renderer));
    PPipelineBuild* build       = pigment_pipeline_build_from_desc(pigment, &pipeline_desc);
    free((void*) pipeline_desc.vertex_spv);
    free((void*) pipeline_desc.fragment_spv);
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

    // SUZANNE

    asset = load_gltf_mesh(pigment, "examples/" EXAMPLE_NAME "/models/Suzanne.gltf");
    if(asset == NULL)
    {
        fprintf(stderr, "Failed to load glTF!\n");
        goto FREE;
    }

    upload_mesh_textures(pigment, asset);

    gpu_mesh = pigment_upload_mesh(
        pigment,
        asset->vertices,
        asset->vertex_count * sizeof(PVertex),
        asset->indices,
        asset->index_count
    );

    uint32_t draw_count = asset->surface_count;
    draw_calls          = calloc(draw_count, sizeof(PDrawCall));
    transform_storage   = calloc(draw_count, sizeof(mat4));
    for(uint32_t i = 0; i < draw_count; i++)
    {
        PRawSurface* s = &asset->surfaces[i];
        glm_mat4_copy(asset->node_transforms[s->node_index], transform_storage[i]);
        draw_calls[i].mesh           = gpu_mesh;
        draw_calls[i].transforms     = &transform_storage[i];
        draw_calls[i].instance_count = 1;
        draw_calls[i].first_index    = s->start_index;
        draw_calls[i].index_count    = s->index_count;
        draw_calls[i].image_index    = s->image_index;
        draw_calls[i].sampler_index  = s->sampler_index;
    }
    free_mesh_asset(asset);
    asset = NULL;

    // CUBE

    asset2 = load_gltf_mesh(pigment, "examples/" EXAMPLE_NAME "/models/BoxVertexColors.glb");
    if(asset2 == NULL)
    {
        fprintf(stderr, "Failed to load glTF!\n");
        goto FREE;
    }

    upload_mesh_textures(pigment, asset2);

    gpu_mesh2 = pigment_upload_mesh(
        pigment,
        asset2->vertices,
        asset2->vertex_count * sizeof(PVertex),
        asset2->indices,
        asset2->index_count
    );

    uint32_t draw_count2 = asset2->surface_count;
    draw_calls2          = calloc(draw_count2, sizeof(PDrawCall));
    transform_storage2   = calloc(draw_count2, sizeof(mat4));
    for(uint32_t i = 0; i < draw_count2; i++)
    {
        PRawSurface* s = &asset2->surfaces[i];
        glm_mat4_copy(asset2->node_transforms[s->node_index], transform_storage2[i]);
        vec3 translation = {2.f, 0.0f, 0.0f};
        glm_translate(transform_storage2[i], translation);
        draw_calls2[i].mesh           = gpu_mesh2;
        draw_calls2[i].transforms     = &transform_storage2[i];
        draw_calls2[i].instance_count = 1;
        draw_calls2[i].first_index    = s->start_index;
        draw_calls2[i].index_count    = s->index_count;
        draw_calls2[i].image_index    = s->image_index;
        draw_calls2[i].sampler_index  = s->sampler_index;
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

        if(!pigment_begin_frame(pigment, 0, camera))
        {
            continue;
        }

        pigment_begin_swapchain_pass(pigment, 0);

        pigment_bind_pipeline(pigment, 0, pipeline);
        pigment_draw(pigment, draw_state, 0, pipeline, draw_calls, draw_count);
        pigment_draw(pigment, draw_state, 0, pipeline, draw_calls2, draw_count2);

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
            pigment_destroy_mesh(pigment, gpu_mesh);
        }
        if(gpu_mesh2 != NULL)
        {
            pigment_destroy_mesh(pigment, gpu_mesh2);
        }
    }
    free(draw_calls);
    free(draw_calls2);
    free(transform_storage);
    free(transform_storage2);
    free_mesh_asset(asset);
    free_mesh_asset(asset2);
    pigment_destroy_camera(camera);
    pigment_std_draw_shutdown(pigment, draw_state);
    destroy_pigment(pigment);

    return error_code;
}
