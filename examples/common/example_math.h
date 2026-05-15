#ifndef EXAMPLES_COMMON_EXAMPLE_MATH_H
#define EXAMPLES_COMMON_EXAMPLE_MATH_H

#include <pigment/std/types.h>

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>


static inline float deg_to_rad(float deg)
{
    return deg * (M_PI / 180.0f);
}

static inline void vec3_copy(const PVec3 src, PVec3 dst)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static inline void vec3_add(const PVec3 a, const PVec3 b, PVec3 dst)
{
    dst[0] = a[0] + b[0];
    dst[1] = a[1] + b[1];
    dst[2] = a[2] + b[2];
}

static inline void vec3_muladds(const PVec3 a, float s, PVec3 dst)
{
    dst[0] += a[0] * s;
    dst[1] += a[1] * s;
    dst[2] += a[2] * s;
}

static inline void vec3_cross(const PVec3 a, const PVec3 b, PVec3 dst)
{
    PVec3 result = {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
    vec3_copy(result, dst);
}

static inline float vec3_dot(const PVec3 a, const PVec3 b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void vec3_normalize(PVec3 v)
{
    float len = sqrtf(vec3_dot(v, v));
    if(len == 0.0f)
    {
        return;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

static inline void mat4_copy(const PMat4 src, PMat4 dst)
{
    memcpy(dst, src, sizeof(PMat4));
}

static inline void mat4_identity(PMat4 m)
{
    PMat4 identity = P_MAT4_IDENTITY;
    memcpy(m, identity, sizeof(PMat4));
}

static inline void mat4_translate(PMat4 m, const PVec3 v)
{
    for(int j = 0; j < 4; j++)
    {
        m[3][j] += m[0][j] * v[0] + m[1][j] * v[1] + m[2][j] * v[2];
    }
}

static inline void mat4_scale(PMat4 m, const PVec3 v)
{
    for(int j = 0; j < 4; j++)
    {
        m[0][j] *= v[0];
        m[1][j] *= v[1];
        m[2][j] *= v[2];
    }
}

static inline void mat4_look_at(const PVec3 eye, const PVec3 center, const PVec3 up, PMat4 dst)
{
    PVec3 f = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    vec3_normalize(f);

    PVec3 s;
    vec3_cross(f, up, s);
    vec3_normalize(s);

    PVec3 u;
    vec3_cross(s, f, u);

    dst[0][0] = s[0];
    dst[0][1] = u[0];
    dst[0][2] = -f[0];
    dst[0][3] = 0.0f;

    dst[1][0] = s[1];
    dst[1][1] = u[1];
    dst[1][2] = -f[1];
    dst[1][3] = 0.0f;

    dst[2][0] = s[2];
    dst[2][1] = u[2];
    dst[2][2] = -f[2];
    dst[2][3] = 0.0f;

    dst[3][0] = -vec3_dot(s, eye);
    dst[3][1] = -vec3_dot(u, eye);
    dst[3][2] = vec3_dot(f, eye);
    dst[3][3] = 1.0f;
}

#endif
