/**
 * Copyright 2025 Angel-Leduc TA
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     https://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "window.h"
#include "structs.h"

static void framebuffer_resize_callback(GLFWwindow* window, int, int);

PWindow* create_window(PWindowInfo* window_info)
{
    PWindow* window = malloc(sizeof(*window));
    if(window == NULL)
    {
        perror("malloc");
        goto ERROR;
    }

    if (window_info->title == NULL)
    {
        fprintf(stderr, "Window title cannot be NULL!\n");
        goto ERROR;
    }

    window->framebuffer_resized = false;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window->window = glfwCreateWindow(window_info->width, window_info->height, window_info->title, NULL, NULL);
    if (window->window == NULL)
    {
        goto ERROR;
    }

    glfwSetWindowUserPointer(window->window, window);

    glfwSetFramebufferSizeCallback(window->window, framebuffer_resize_callback);

    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    if (mode)
    {
        int xpos = (mode->width  - window_info->width)  / 2;
        int ypos = (mode->height - window_info->height) / 2;
        glfwSetWindowPos(window->window, xpos, ypos);
    }

    window->last_frame_time  = 0.0f;
    window->first_time_mouse = true;

    glfwShowWindow(window->window);

    window->info = window_info;

    return window;

ERROR:
    fprintf(stderr, "Failed to create window!\n");
    free(window);
    return NULL;
}

void destroy_window(PWindow* window)
{
    if(window == NULL)
    {
        return;
    }
    glfwDestroyWindow(window->window);
    glfwTerminate();
    free(window);
}

bool window_should_close(PWindow* window)
{
    return glfwWindowShouldClose(window->window);
}

void poll_events(void)
{
    glfwPollEvents();
}

static void framebuffer_resize_callback(GLFWwindow* window, int width __attribute__((unused)), int height __attribute__((unused)))
{
    PWindow* pigment_window             = glfwGetWindowUserPointer(window);
    pigment_window->framebuffer_resized = true;
    pigment_window->info->width          = width;
    pigment_window->info->height         = height;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    PWindow* pigment_window = glfwGetWindowUserPointer(window);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    if(pigment_window->first_time_mouse)
    {
        pigment_window->mouse_last_x     = (float) width / 2.0f;
        pigment_window->mouse_last_y     = (float) height / 2.0f;
        pigment_window->first_time_mouse = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    float xoffset                = (float) xpos - (float) pigment_window->mouse_last_x;
    float yoffset                = (float) pigment_window->mouse_last_y - (float) ypos;
    pigment_window->mouse_last_x = (float) xpos;
    pigment_window->mouse_last_y = (float) ypos;

    float sensitivity = 0.05f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    pigment_window->camera->yaw += xoffset;
    pigment_window->camera->pitch += yoffset;

    if(pigment_window->camera->pitch > 89.0f)
    {
        pigment_window->camera->pitch = 89.0f;
    }
    if(pigment_window->camera->pitch < -89.0f)
    {
        pigment_window->camera->pitch = -89.0f;
    }

    vec3 front = {
        (float)(cos(glm_rad(pigment_window->camera->pitch)) * sin(glm_rad(pigment_window->camera->yaw))),
        (float)(cos(glm_rad(pigment_window->camera->pitch)) * cos(glm_rad(pigment_window->camera->yaw))),
        (float)(sin(glm_rad(pigment_window->camera->pitch)))
    };

    glm_vec3_normalize(front);  

    memcpy(pigment_window->camera->front, front, sizeof(front));
}

void handle_inputs(PWindow* window)
{
    float current_time      = (float) glfwGetTime();
    float delta_time        = current_time - window->last_frame_time;
    window->last_frame_time = current_time;
    float speed             = window->camera->speed * delta_time;

    if(glfwGetKey(window->window, GLFW_KEY_W) == GLFW_PRESS)
    {
        glm_vec3_muladds(window->camera->front, speed, window->camera->position);
    }

    if(glfwGetKey(window->window, GLFW_KEY_S) == GLFW_PRESS)
    {
        glm_vec3_muladds(window->camera->front, -speed, window->camera->position);
    }

    if(glfwGetKey(window->window, GLFW_KEY_A) == GLFW_PRESS)
    {
        vec3 right;
        glm_vec3_cross(window->camera->front, window->camera->up, right);
        glm_vec3_normalize(right);
        glm_vec3_muladds(right, -speed, window->camera->position);
    }

    if(glfwGetKey(window->window, GLFW_KEY_D) == GLFW_PRESS)
    {
        vec3 right;
        glm_vec3_cross(window->camera->front, window->camera->up, right);
        glm_vec3_normalize(right);
        glm_vec3_muladds(right, speed, window->camera->position);
    }

    if(glfwGetKey(window->window, GLFW_KEY_SPACE) == GLFW_PRESS)
    {
        glm_vec3_muladds(window->camera->up, speed, window->camera->position);
    }

    if(glfwGetKey(window->window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    {
        glm_vec3_muladds(window->camera->up, -speed, window->camera->position);
    }
}

void set_mouse_handler(PWindow* window)
{
    glfwSetCursorPosCallback(window->window, mouse_callback);
}
