#ifndef EXAMPLES_COMMON_FPS_CAMERA_H
#define EXAMPLES_COMMON_FPS_CAMERA_H

#include "example_math.h"

#include <pigment/pigment.h>
#include <pigment/std/camera.h>
#include <pigment/std/camera_math.h>
#include <SDL3/SDL.h>

#include <math.h>

typedef struct FPSCameraState {
    PVec3 position;
    PVec3 front;
    PVec3 up;
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
    PMat4 projection;
    pigment_perspective(state->fov_rad, (float) w / (float) h, state->near, projection);
    pigment_std_camera_set_projection(camera, projection);
}

FPSCameraState fps_camera_state_init(PCamera* camera, SDL_Window* window, PVec3 position)
{
    FPSCameraState state = {
        .speed             = 5.0f,
        .yaw               = -90.0f,
        .pitch             = 0.0f,
        .fov_rad           = deg_to_rad(45.0f),
        .near              = 0.1f,
        .last_frame_time   = 0.0f,
        .mouse_offset_x    = 0.0f,
        .mouse_offset_y    = 0.0f,
        .mouse_sensitivity = 0.05f,
    };

    vec3_copy(position, state.position);

    PVec3 default_front = {0.0f, 0.0f, -1.0f};
    PVec3 default_up    = {0.0f, 1.0f, 0.0f};
    vec3_copy(default_front, state.front);
    vec3_copy(default_up, state.up);

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
        vec3_muladds(state->front, speed, state->position);
    }
    if(keys[SDL_SCANCODE_S])
    {
        vec3_muladds(state->front, -speed, state->position);
    }
    if(keys[SDL_SCANCODE_A])
    {
        PVec3 right;
        vec3_cross(state->front, state->up, right);
        vec3_normalize(right);
        vec3_muladds(right, -speed, state->position);
    }
    if(keys[SDL_SCANCODE_D])
    {
        PVec3 right;
        vec3_cross(state->front, state->up, right);
        vec3_normalize(right);
        vec3_muladds(right, speed, state->position);
    }
    if(keys[SDL_SCANCODE_SPACE])
    {
        vec3_muladds(state->up, speed, state->position);
    }
    if(keys[SDL_SCANCODE_LSHIFT])
    {
        vec3_muladds(state->up, -speed, state->position);
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

        PVec3 front = {
            cosf(deg_to_rad(state->yaw)) * cosf(deg_to_rad(state->pitch)),
            sinf(deg_to_rad(state->pitch)),
            sinf(deg_to_rad(state->yaw)) * cosf(deg_to_rad(state->pitch)),
        };
        vec3_normalize(front);
        vec3_copy(front, state->front);
    }

    PVec3 target;
    vec3_add(state->position, state->front, target);

    PMat4 view;
    mat4_look_at(state->position, target, state->up, view);

    pigment_std_camera_set_view(camera, view);
}

#endif
