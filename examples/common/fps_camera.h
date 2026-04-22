#ifndef EXAMPLES_COMMON_FPS_CAMERA_H
#define EXAMPLES_COMMON_FPS_CAMERA_H

#include <pigment.h>
#include <SDL3/SDL.h>

#include <math.h>

typedef struct FPSCameraState {
    float last_frame_time;
    float mouse_offset_x;
    float mouse_offset_y;
    float mouse_sensitivity;
} FPSCameraState;

FPSCameraState fps_camera_state_init(void)
{
    FPSCameraState state = {
        .last_frame_time   = 0.0f,
        .mouse_offset_x    = 0.0f,
        .mouse_offset_y    = 0.0f,
        .mouse_sensitivity = 0.05f,
    };
    return state;
}

void fps_camera_handle_sdl_event(FPSCameraState* state, const SDL_Event* event)
{
    if(event->type == SDL_EVENT_MOUSE_MOTION)
    {
        state->mouse_offset_x += event->motion.xrel;
        state->mouse_offset_y -= event->motion.yrel;
    }
}

void fps_camera_update(PCamera* camera, FPSCameraState* state)
{
    float current_time = (float) ((double) SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency());
    if(state->last_frame_time == 0.0f)
    {
        state->last_frame_time = current_time;
    }
    float delta_time       = current_time - state->last_frame_time;
    state->last_frame_time = current_time;
    float speed            = camera->speed * delta_time;

    const bool* keys = SDL_GetKeyboardState(NULL);

    if(keys[SDL_SCANCODE_W])
    {
        glm_vec3_muladds(camera->front, speed, camera->position);
    }
    if(keys[SDL_SCANCODE_S])
    {
        glm_vec3_muladds(camera->front, -speed, camera->position);
    }
    if(keys[SDL_SCANCODE_A])
    {
        vec3 right;
        glm_vec3_cross(camera->front, camera->up, right);
        glm_vec3_normalize(right);
        glm_vec3_muladds(right, -speed, camera->position);
    }
    if(keys[SDL_SCANCODE_D])
    {
        vec3 right;
        glm_vec3_cross(camera->front, camera->up, right);
        glm_vec3_normalize(right);
        glm_vec3_muladds(right, speed, camera->position);
    }
    if(keys[SDL_SCANCODE_SPACE])
    {
        glm_vec3_muladds(camera->up, speed, camera->position);
    }
    if(keys[SDL_SCANCODE_LSHIFT])
    {
        glm_vec3_muladds(camera->up, -speed, camera->position);
    }

    float xoffset         = state->mouse_offset_x * state->mouse_sensitivity;
    float yoffset         = state->mouse_offset_y * state->mouse_sensitivity;
    state->mouse_offset_x = 0.0f;
    state->mouse_offset_y = 0.0f;

    if(xoffset != 0.0f || yoffset != 0.0f)
    {
        camera->yaw += xoffset;
        camera->pitch += yoffset;

        if(camera->pitch > 89.0f)
        {
            camera->pitch = 89.0f;
        }
        if(camera->pitch < -89.0f)
        {
            camera->pitch = -89.0f;
        }

        vec3 front = {
            (float) (cos(glm_rad(camera->yaw)) * cos(glm_rad(camera->pitch))),
            (float) (sin(glm_rad(camera->pitch))),
            (float) (sin(glm_rad(camera->yaw)) * cos(glm_rad(camera->pitch))),
        };
        glm_vec3_normalize(front);
        memcpy(camera->front, front, sizeof(front));
    }
}

#endif
