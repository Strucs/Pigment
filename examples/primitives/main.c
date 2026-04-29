#include <pigment.h>
#include <pigment_sdl.h>
#include <std/pipeline_loader.h>
#include <std/primitives.h>

#include "../common/fps_camera.h"

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Primitives",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PWindowInfo window_info = {
        .width                  = 1280,
        .height                 = 720,
        .title                  = "Primitives (cube + sphere + plane + quad)",
        .preferred_present_mode = P_PRESENT_MODE_DEFAULT,
        .flags                  = P_WINDOW_FLAGS_DEFAULT,
    };

    Pigment* pigment         = NULL;
    PCamera* camera          = NULL;
    PPipeline* pipeline      = NULL;
    PPipelineBuild* build    = NULL;
    PMeshBuffers* gpu_cube   = NULL;
    PMeshBuffers* gpu_sphere = NULL;
    PMeshBuffers* gpu_plane  = NULL;
    PMeshBuffers* gpu_quad   = NULL;
    PDrawCall draw_calls[4]  = {0};
    int error_code           = 1;

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

    vec3 camera_position = {0.0f, 1.5f, 6.0f};
    camera               = pigment_create_camera();
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }

    FPSCameraState fps_state = fps_camera_state_init(camera, pigment_get_sdl_window(pigment, 0), camera_position);
    SDL_SetWindowRelativeMouseMode(pigment_get_sdl_window(pigment, 0), true);

    PWindowRenderer* renderer = pigment_get_window_renderer(pigment, 0);
    PFormat color_format      = pigment_get_color_format(renderer);
    PFormat depth_format      = pigment_get_depth_format(renderer);

    PPipelineDesc desc = default_graphic_pipeline_desc(pigment, &color_format, 1, depth_format);
    build              = pigment_pipeline_build_from_desc(pigment, &desc);
    free((void*) desc.vertex_spv);
    free((void*) desc.fragment_spv);
    if(build == NULL)
    {
        fprintf(stderr, "Failed to build pipeline!\n");
        goto FREE;
    }
    if(pigment_create_graphic_pipelines(pigment, &build, 1, &pipeline) != PIGMENT_SUCCESS)
    {
        fprintf(stderr, "Failed to create pipeline!\n");
        goto FREE;
    }

    PMeshData cube    = pigment_cube_mesh();
    uint32_t cube_idx = cube.index_count;
    gpu_cube          = pigment_upload_mesh_data(pigment, &cube);
    pigment_free_mesh_data(&cube);

    PMeshData sphere    = pigment_sphere_mesh(32, 32);
    uint32_t sphere_idx = sphere.index_count;
    gpu_sphere          = pigment_upload_mesh_data(pigment, &sphere);
    pigment_free_mesh_data(&sphere);

    PMeshData plane    = pigment_plane_mesh(8);
    uint32_t plane_idx = plane.index_count;
    gpu_plane          = pigment_upload_mesh_data(pigment, &plane);
    pigment_free_mesh_data(&plane);

    PMeshData quad    = pigment_quad_mesh();
    uint32_t quad_idx = quad.index_count;
    gpu_quad          = pigment_upload_mesh_data(pigment, &quad);
    pigment_free_mesh_data(&quad);

    if(gpu_cube == NULL || gpu_sphere == NULL || gpu_plane == NULL || gpu_quad == NULL)
    {
        fprintf(stderr, "Failed to upload primitives!\n");
        goto FREE;
    }

    glm_mat4_identity(draw_calls[0].transform);
    glm_translate(draw_calls[0].transform, (vec3) {-3.0f, 0.0f, 0.0f});
    draw_calls[0].mesh          = gpu_cube;
    draw_calls[0].index_count   = cube_idx;
    draw_calls[0].image_index   = (uint32_t) -1;
    draw_calls[0].sampler_index = 0;

    glm_mat4_identity(draw_calls[1].transform);
    glm_translate(draw_calls[1].transform, (vec3) {-1.0f, 0.0f, 0.0f});
    draw_calls[1].mesh          = gpu_sphere;
    draw_calls[1].index_count   = sphere_idx;
    draw_calls[1].image_index   = (uint32_t) -1;
    draw_calls[1].sampler_index = 0;

    glm_mat4_identity(draw_calls[2].transform);
    glm_translate(draw_calls[2].transform, (vec3) {0.0f, -0.6f, 0.0f});
    glm_scale(draw_calls[2].transform, (vec3) {10.0f, 1.0f, 10.0f});
    draw_calls[2].mesh          = gpu_plane;
    draw_calls[2].index_count   = plane_idx;
    draw_calls[2].image_index   = (uint32_t) -1;
    draw_calls[2].sampler_index = 0;

    glm_mat4_identity(draw_calls[3].transform);
    glm_translate(draw_calls[3].transform, (vec3) {2.0f, 0.0f, 0.0f});
    draw_calls[3].mesh          = gpu_quad;
    draw_calls[3].index_count   = quad_idx;
    draw_calls[3].image_index   = (uint32_t) -1;
    draw_calls[3].sampler_index = 0;

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
        pigment_draw(pigment, 0, pipeline, draw_calls, 4);
        pigment_end_swapchain_pass(pigment, 0);

        pigment_end_frame(pigment, 0);
    }

    error_code = 0;

FREE:
    if(pigment != NULL)
    {
        pigment_wait_idle(pigment);
        if(gpu_cube != NULL)
        {
            pigment_destroy_mesh(pigment, gpu_cube);
        }
        if(gpu_sphere != NULL)
        {
            pigment_destroy_mesh(pigment, gpu_sphere);
        }
        if(gpu_plane != NULL)
        {
            pigment_destroy_mesh(pigment, gpu_plane);
        }
        if(gpu_quad != NULL)
        {
            pigment_destroy_mesh(pigment, gpu_quad);
        }
    }
    pigment_destroy_camera(camera);
    destroy_pigment(pigment);

    return error_code;
}
