/**
 * Copyright 2026 Angel-Leduc TA
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

#ifndef PIGMENT_ATTRIBUTES_H
#define PIGMENT_ATTRIBUTES_H

#if defined(__GNUC__) || defined(__clang__)
    #define PIGMENT_ALIGN(n) __attribute__((aligned(n)))
    #define PIGMENT_PRINTF_FORMAT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#elif defined(_MSC_VER)
    #define PIGMENT_ALIGN(n) __declspec(align(n))
    #define PIGMENT_PRINTF_FORMAT(fmt_idx, args_idx)
#else
    #define PIGMENT_ALIGN(n)
    #define PIGMENT_PRINTF_FORMAT(fmt_idx, args_idx)
#endif

#if defined(_WIN32)
    #ifdef PIGMENT_EXPORTS
        #define PIGMENT_API __declspec(dllexport)
    #else
        #define PIGMENT_API
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #ifdef PIGMENT_EXPORTS
        #define PIGMENT_API __attribute__((visibility("default")))
    #else
        #define PIGMENT_API
    #endif
#else
    #define PIGMENT_API
#endif

#endif
