#include <pigment.h>

#include <models.h>
#include <mesh.h>
#include <device.h>
#include <structs.h>
#include <lib/loader.h>

#define MODEL_NAME "lost_empire"

#define MESH_COUNT 2

int main(void)
{
    PAppInfo app_info = {
        .app_name    = "Lost Empire",
        .app_version = PIGMENT_MAKE_VERSION(1, 0, 0)
    };

    PWindowInfo window_info = {
        .width  = 1280,
        .height = 720,
        .title  = "Lost Empire"
    };

    Pigment* pigment = NULL;

    StringArray* paths               = NULL;
    TexturesToLoad* textures_to_load = NULL;

    PMeshBuffers* meshes[MESH_COUNT]  = {0};
    uint32_t index_counts[MESH_COUNT] = {0};

    const uint32_t max_frame_int_flight = 2;
    int error_code                      = 1;

    paths = create_string_array();
    if(paths == NULL)
    {
        goto FREE;
    }

    add_path(paths, "examples/" MODEL_NAME "/textures");

    textures_to_load = init_textures_to_load();
    if(textures_to_load == NULL)
    {
        goto FREE;
    }
    add_textures_dir_to_load(textures_to_load, "examples/" MODEL_NAME "/textures");

    pigment = init_pigment(&app_info, &window_info, textures_to_load, paths, max_frame_int_flight);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
        goto FREE;
    }

    // Load cube mesh
    PModel* model_0 = create_model();
    load_cube(0.5f, 0.0f, 0.0f, 0.0f, 0, model_0);
    meshes[0]       = pigment_upload_mesh(pigment, model_0->vertices, sizeof(PVertex) * model_0->vertices_number, model_0->indices, model_0->indices_number);
    index_counts[0] = model_0->indices_number;
    destroy_model(model_0);

    // Load OBJ mesh
    PModel* model_1 = create_model();
    load_model_multi_textures("examples/" MODEL_NAME "/models/" MODEL_NAME ".obj", 0.0f, 0.0f, 0.0f, 0.1f, textures_to_load, model_1);
    meshes[1]       = pigment_upload_mesh(pigment, model_1->vertices, sizeof(PVertex) * model_1->vertices_number, model_1->indices, model_1->indices_number);
    index_counts[1] = model_1->indices_number;
    destroy_model(model_1);

    pigment_show_window(pigment);

    while(pigment_should_run(pigment))
    {
        pigment_poll_events();
        pigment_handle_inputs(pigment);

        if(!pigment_begin_frame(pigment))
        {
            continue;
        }

        PDrawCall draw_calls[MESH_COUNT] = {0};
        for(int i = 0; i < MESH_COUNT - 1; i++)
        {
            glm_mat4_identity(draw_calls[i].transform);
            draw_calls[i].mesh        = meshes[i];
            draw_calls[i].first_index = 0;
            draw_calls[i].index_count = index_counts[i];
            draw_calls[i].pipeline_id = 0;
        }

        pigment_draw(pigment, draw_calls, MESH_COUNT - 1);

        pigment_end_frame(pigment);
    }

    error_code = 0;

FREE:
    if(pigment != NULL)
    {
        device_wait_idle(pigment->device);
        for(int i = 0; i < MESH_COUNT; i++)
        {
            if(meshes[i] != NULL)
            {
                pigment_destroy_mesh(pigment, meshes[i]);
            }
        }
    }
    destroy_pigment(pigment);

    return error_code;
}
