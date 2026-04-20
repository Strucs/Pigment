#include <pigment.h>
#include <loader/gltf_loader.h>
#include <loader/pipeline_loader.h>

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
        .preferred_present_mode = P_PRESENT_MODE_DEFAULT
    };

    Pigment* pigment        = NULL;
    PCamera* camera         = NULL;
    PPipelines* pipelines   = NULL;
    PMeshBuffers* gpu_mesh  = NULL;
    MeshAsset* asset        = NULL;
    PMeshBuffers* gpu_mesh2 = NULL;
    MeshAsset* asset2       = NULL;
    PDrawCall* draw_calls   = NULL;
    PDrawCall* draw_calls2  = NULL;
    PPipelineBuild* builds[2] = {NULL, NULL};
    int error_code          = 1;

    pigment = init_pigment(&app_info, &window_info, NULL);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
        goto FREE;
    }

    vec3 camera_position = {1.5f, 0.0f, 5.0f};
    camera = pigment_create_camera(camera_position);
    if(camera == NULL)
    {
        fprintf(stderr, "Failed to create camera!\n");
        goto FREE;
    }
    add_camera_to_window(camera, pigment);
    set_mouse_handler(pigment);

    PWindowRenderer* renderer = pigment_get_window_renderer(pigment);
    PFormat color_format      = pigment_get_color_format(renderer);
    PFormat depth_format      = pigment_get_depth_format(renderer);

    PPipelineDesc desc_opaque   = default_graphic_pipeline_desc(color_format, depth_format);
    PPipelineDesc desc_additive = default_graphic_pipeline_desc(color_format, depth_format);
    desc_additive.blend_mode    = P_BLEND_MODE_ADDITIVE;
    desc_additive.depth_write   = false;

    builds[0] = pigment_pipeline_build_from_desc(pigment, &desc_opaque);
    builds[1] = pigment_pipeline_build_from_desc(pigment, &desc_additive);

    free((void*) desc_opaque.vertex_spv);
    free((void*) desc_opaque.fragment_spv);
    free((void*) desc_additive.vertex_spv);
    free((void*) desc_additive.fragment_spv);

    if(builds[0] == NULL || builds[1] == NULL)
    {
        fprintf(stderr, "Failed to build pipelines!\n");
        goto FREE;
    }

    pipelines = pigment_create_graphic_pipelines(pigment, builds, 2);
    if(pipelines == NULL)
    {
        fprintf(stderr, "Failed to create pipelines!\n");
        goto FREE;
    }

    // SUZANNE (opaque)

    asset = load_gltf_mesh(MODELS_DIR "/Suzanne.gltf");
    if(asset == NULL)
    {
        fprintf(stderr, "Failed to load Suzanne!\n");
        goto FREE;
    }

    upload_mesh_textures(pigment, asset, 0);

    gpu_mesh = pigment_upload_mesh(
        pigment,
        asset->vertices,
        asset->vertex_count * sizeof(PVertex),
        asset->indices,
        asset->index_count,
        0
    );

    uint32_t draw_count = asset->surface_count;
    draw_calls          = calloc(draw_count, sizeof(PDrawCall));
    for(uint32_t i = 0; i < draw_count; i++)
    {
        PRawSurface* s = &asset->surfaces[i];
        glm_mat4_copy(asset->node_transforms[s->node_index], draw_calls[i].transform);
        draw_calls[i].mesh          = gpu_mesh;
        draw_calls[i].first_index   = s->start_index;
        draw_calls[i].index_count   = s->index_count;
        draw_calls[i].image_index   = s->image_index;
        draw_calls[i].sampler_index = s->sampler_index;
    }
    free_mesh_asset(asset);
    asset = NULL;

    // CUBE (additive blend)

    asset2 = load_gltf_mesh(MODELS_DIR "/BoxVertexColors.glb");
    if(asset2 == NULL)
    {
        fprintf(stderr, "Failed to load cube!\n");
        goto FREE;
    }

    upload_mesh_textures(pigment, asset2, 0);

    gpu_mesh2 = pigment_upload_mesh(
        pigment,
        asset2->vertices,
        asset2->vertex_count * sizeof(PVertex),
        asset2->indices,
        asset2->index_count,
        0
    );

    uint32_t draw_count2 = asset2->surface_count;
    draw_calls2          = calloc(draw_count2, sizeof(PDrawCall));
    for(uint32_t i = 0; i < draw_count2; i++)
    {
        PRawSurface* s = &asset2->surfaces[i];
        glm_mat4_copy(asset2->node_transforms[s->node_index], draw_calls2[i].transform);
        vec3 translation = {2.f, 0.0f, 0.0f};
        glm_translate(draw_calls2[i].transform, translation);
        draw_calls2[i].mesh          = gpu_mesh2;
        draw_calls2[i].first_index   = s->start_index;
        draw_calls2[i].index_count   = s->index_count;
        draw_calls2[i].image_index   = s->image_index;
        draw_calls2[i].sampler_index = s->sampler_index;
    }
    free_mesh_asset(asset2);
    asset2 = NULL;

    pigment_show_window(pigment);

    while(pigment_should_run(pigment))
    {
        pigment_poll_events();
        pigment_handle_inputs(pigment);

        if(!pigment_begin_frame(pigment, camera))
        {
            continue;
        }

        pigment_bind_pipeline(pigment, pipelines, 0);
        pigment_draw(pigment, pipelines, 0, draw_calls, draw_count);

        pigment_bind_pipeline(pigment, pipelines, 1);
        pigment_draw(pigment, pipelines, 1, draw_calls2, draw_count2);

        pigment_end_frame(pigment);
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
        pigment_destroy_pipelines(pigment, pipelines);
    }
    free(draw_calls);
    free(draw_calls2);
    free_mesh_asset(asset);
    free_mesh_asset(asset2);
    pigment_destroy_camera(camera);
    destroy_pigment(pigment);

    return error_code;
}
