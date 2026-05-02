#ifndef EXAMPLES_COMMON_FPS_CAMERA_H
#define EXAMPLES_COMMON_FPS_CAMERA_H

#include <pigment.h>
#include <std/camera.h>
#include <std/camera_math.h>
#include <SDL3/SDL.h>

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <math.h>

typedef struct FPSCameraState {
    vec3 position;
    vec3 front;
    vec3 up;
    float speed;
    float yaw;
    float pitch;

    float fov_rad;
    float near;

    float last_frame_time;
    float mouse_offset_x;
    float mouse_offset_y;
    float mouse_sensitivity;
} FPSCameraState;

void fps_camera_apply_size(PCamera* camera, FPSCameraState* state, int w, int h)
{
    mat4 projection;
    pigment_perspective(state->fov_rad, (float) w / (float) h, state->near, projection);
    pigment_std_camera_set_projection(camera, projection);
}

FPSCameraState fps_camera_state_init(PCamera* camera, SDL_Window* window, vec3 position)
{
    FPSCameraState state = {
        .speed             = 5.0f,
        .yaw               = -90.0f,
        .pitch             = 0.0f,
        .fov_rad           = glm_rad(45.0f),
        .near              = 0.1f,
        .last_frame_time   = 0.0f,
        .mouse_offset_x    = 0.0f,
        .mouse_offset_y    = 0.0f,
        .mouse_sensitivity = 0.05f,
    };

    glm_vec3_copy(position, state.position);

    vec3 default_front = {0.0f, 0.0f, -1.0f};
    vec3 default_up    = {0.0f, 1.0f, 0.0f};
    glm_vec3_copy(default_front, state.front);
    glm_vec3_copy(default_up, state.up);

    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    fps_camera_apply_size(camera, &state, w, h);

    return state;
}

void fps_camera_handle_sdl_event(PCamera* camera, FPSCameraState* state, const SDL_Event* event)
{
    if(event->type == SDL_EVENT_MOUSE_MOTION)
    {
        state->mouse_offset_x += event->motion.xrel;
        state->mouse_offset_y -= event->motion.yrel;
    }
    else if(event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        fps_camera_apply_size(camera, state, event->window.data1, event->window.data2);
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
    float speed            = state->speed * delta_time;

    const bool* keys = SDL_GetKeyboardState(NULL);

    if(keys[SDL_SCANCODE_W])
    {
        glm_vec3_muladds(state->front, speed, state->position);
    }
    if(keys[SDL_SCANCODE_S])
    {
        glm_vec3_muladds(state->front, -speed, state->position);
    }
    if(keys[SDL_SCANCODE_A])
    {
        vec3 right;
        glm_vec3_cross(state->front, state->up, right);
        glm_vec3_normalize(right);
        glm_vec3_muladds(right, -speed, state->position);
    }
    if(keys[SDL_SCANCODE_D])
    {
        vec3 right;
        glm_vec3_cross(state->front, state->up, right);
        glm_vec3_normalize(right);
        glm_vec3_muladds(right, speed, state->position);
    }
    if(keys[SDL_SCANCODE_SPACE])
    {
        glm_vec3_muladds(state->up, speed, state->position);
    }
    if(keys[SDL_SCANCODE_LSHIFT])
    {
        glm_vec3_muladds(state->up, -speed, state->position);
    }

    float xoffset         = state->mouse_offset_x * state->mouse_sensitivity;
    float yoffset         = state->mouse_offset_y * state->mouse_sensitivity;
    state->mouse_offset_x = 0.0f;
    state->mouse_offset_y = 0.0f;

    if(xoffset != 0.0f || yoffset != 0.0f)
    {
        state->yaw += xoffset;
        state->pitch += yoffset;

        if(state->pitch > 89.0f)
        {
            state->pitch = 89.0f;
        }
        if(state->pitch < -89.0f)
        {
            state->pitch = -89.0f;
        }

        vec3 front = {
            (float) (cos(glm_rad(state->yaw)) * cos(glm_rad(state->pitch))),
            (float) (sin(glm_rad(state->pitch))),
            (float) (sin(glm_rad(state->yaw)) * cos(glm_rad(state->pitch))),
        };
        glm_vec3_normalize(front);
        memcpy(state->front, front, sizeof(front));
    }

    vec3 target;
    glm_vec3_add(state->position, state->front, target);

    mat4 view;
    glm_lookat(state->position, target, state->up, view);

    pigment_std_camera_set_view(camera, view);
}

#endif
