#include "pigment.h"

#include "models.h"
#include "lib/loader.h"

#define MODEL_NAME "lost_empire"

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
    PModel* model = NULL;

    StringArray* paths = NULL;
    TexturesToLoad* textures_to_load = NULL;

    const uint32_t max_frame_int_flight = 2;
    int error_code = 1;

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

    model = create_model();
    if(model == NULL)
    {
        goto FREE;
    }

    load_cube(0.5f, 0.0f, 0.0f, 0.0f, 0, model);
    load_model_multi_textures("examples/" MODEL_NAME "/models/" MODEL_NAME ".obj", 0.0f, 0.0f, 0.0f, 0.1f, textures_to_load, model);

    pigment = init_pigment(&app_info, &window_info, model, textures_to_load, paths, max_frame_int_flight);
    if(pigment == NULL)
    {
        fprintf(stderr, "Failed to initialize Pigment!\n");
        goto FREE;
    }

    pigment_run(pigment);

    error_code = 0;

FREE:
    destroy_pigment(pigment);

    return error_code;
}
