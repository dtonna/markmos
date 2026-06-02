// Copyright (c) 2025 Markmos
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

// =============================================================================
// Platform Detection
// =============================================================================
#if defined(__ANDROID__)
#    undef MM_PLATFORM_ANDROID
#    define MM_PLATFORM_ANDROID 1
#elif defined(_WIN32)
#    undef MM_PLATFORM_WINDOWS
#    define MM_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#    undef MM_PLATFORM_LINUX
#    define MM_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#    undef MM_PLATFORM_APPLE
#    define MM_PLATFORM_APPLE 1
#    include <AvailabilityMacros.h>
#    ifndef __has_extension
#        define __has_extension(x) 0
#        include <TargetConditionals.h>
#        undef __has_extension
#    else
#        include <TargetConditionals.h>
#    endif /* __has_extension(x) */
#    ifndef TARGET_OS_MACCATALYST
#        define TARGET_OS_MACCATALYST 0
#    endif
#    ifndef TARGET_OS_IOS
#        define TARGET_OS_IOS 0
#    endif
#    ifndef TARGET_OS_IPHONE
#        define TARGET_OS_IPHONE 0
#    endif
#    ifndef TARGET_OS_TV
#        define TARGET_OS_TV 0
#    endif
#    ifndef TARGET_OS_SIMULATOR
#        define TARGET_OS_SIMULATOR 0
#    endif
#    ifndef TARGET_OS_VISION
#        define TARGET_OS_VISION 0
#    endif
#    if TARGET_OS_OSX
#        undef MM_PLATFORM_MACOS
#        define MM_PLATFORM_MACOS 1
#    endif
#    if TARGET_OS_IPHONE
#        undef MM_PLATFORM_IPHONE
#        define MM_PLATFORM_IPHONE 1
#    endif
#elif defined(__EMSCRIPTEN__)
#    undef MM_PLATFORM_EMSCRIPTEN
#    define MM_PLATFORM_EMSCRIPTEN 1
#else
#    error "Unsupported platform"
#endif

// =============================================================================
// Include platform-specific headers
// =============================================================================
#if MM_PLATFORM_WINDOWS
#    include <windows.h>
#endif

// =============================================================================
// Architecture Detection
// =============================================================================
#if defined(_M_X64) || defined(__amd64__) || defined(__x86_64__)
#    undef MM_ARCH_X86_64
#    define MM_ARCH_X86_64 1
#elif defined(_M_IX86) || defined(__i386__)
#    undef MM_ARCH_X86
#    define MM_ARCH_X86 1
#elif defined(__aarch64__)
#    undef MM_ARCH_ARM64
#    define MM_ARCH_ARM64 1
#elif defined(__arm__)
#    undef MM_ARCH_ARM
#    define MM_ARCH_ARM 1
#else
#    error "Unsupported architecture"
#endif

/// =============================================================================
/// Compiler Detection
/// =============================================================================
#if defined(__clang__)
#    undef MM_COMPILER_CLANG
#    define MM_COMPILER_CLANG 1
#elif defined(__GNUC__) || defined(__GNUG__)
#    undef MM_COMPILER_GCC
#    define MM_COMPILER_GCC 1
#elif defined(_MSC_VER)
#    undef MM_COMPILER_MSVC
#    define MM_COMPILER_MSVC 1
#else
#    error "Unsupported compiler"
#endif

// =============================================================================
// Force inline macro
// =============================================================================
#if MM_COMPILER_MSVC
#    define MM_FORCE_INLINE __forceinline
#    define MM_NOINLINE     __declspec(noinline)
#    define MM_EXPORT       __declspec(dllexport)
#    define MM_PACKED       __pragma(pack(push, 1))
#elif MM_COMPILER_GCC || MM_COMPILER_CLANG
#    define MM_FORCE_INLINE inline __attribute__((always_inline))
#    define MM_NOINLINE     __attribute__((noinline))
#    define MM_EXPORT       __attribute__((visibility("default")))
#    define MM_PACKED       __attribute__((packed))
#else
#    define MM_FORCE_INLINE inline
#    define MM_NOINLINE
#    define MM_EXPORT
#    define MM_PACKED
#endif

#ifndef MM_PRIVATE
#    if defined(__GNUC__) || defined(__clang__)
#        define MM_PRIVATE __attribute__((unused)) static
#    else
#        define MM_PRIVATE static
#    endif
#endif

#ifndef MM_UNUSED
#    define MM_UNUSED(x) (void)(x)
#endif

#ifndef MM_ASSERT
#    include <cassert>
#    define MM_ASSERT(c) assert(c)
#endif

// =============================================================================
// Likely / Unlikely macros
// =============================================================================
#if MM_COMPILER_GCC || MM_COMPILER_CLANG
#    define MM_LIKELY(x)   __builtin_expect(!!(x), 1)
#    define MM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#    define MM_LIKELY(x)   (x)
#    define MM_UNLIKELY(x) (x)
#endif

// =============================================================================
// Alignment macros
// =============================================================================
#if MM_COMPILER_MSVC
#    define MM_ALIGNAS(x) __declspec(align(x))
#elif MM_COMPILER_GCC || MM_COMPILER_CLANG
#    define MM_ALIGNAS(x) __attribute__((aligned(x)))
#else
#    define MM_ALIGNAS(x)
#endif

// =============================================================================
// Thread-local storage macro
// =============================================================================
#if MM_COMPILER_MSVC
#    define MM_THREAD_LOCAL __declspec(thread)
#elif MM_COMPILER_GCC || MM_COMPILER_CLANG
#    define MM_THREAD_LOCAL __thread
#else
#    define MM_THREAD_LOCAL
#endif

// =============================================================================
// Debug break macro
// =============================================================================
#if MM_COMPILER_MSVC
#    define MM_DEBUG_BREAK() __debugbreak()
#elif MM_COMPILER_GCC || MM_COMPILER_CLANG
#    define MM_DEBUG_BREAK() __builtin_trap()
#else
#    define MM_DEBUG_BREAK() ((void)0)
#endif

// =============================================================================
// Unreachable code macro
// =============================================================================
#if MM_COMPILER_MSVC
#    define MM_UNREACHABLE() __assume(0)
#elif MM_COMPILER_GCC || MM_COMPILER_CLANG
#    define MM_UNREACHABLE() __builtin_unreachable()
#else
#    define MM_UNREACHABLE() ((void)0)
#endif

// =============================================================================
// Endianness Detection
// =============================================================================
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    undef MM_ENDIAN_BIG
#    define MM_ENDIAN_BIG 1
#else
#    undef MM_ENDIAN_LITTLE
#    define MM_ENDIAN_LITTLE 1
#endif

// =============================================================================
// Graphics API Convention (user-defined defines)
//   MM_VULKAN   → NDC z∈[0,1], Y-down
//   MM_METAL    → NDC z∈[0,1], Y-up
//   MM_OPENGLES → NDC z∈[-1,1], Y-up  (default)
// Define one before including this header.
// =============================================================================
#if !defined(MM_VULKAN) && !defined(MM_METAL) && !defined(MM_OPENGLES)
#    undef MM_OPENGLES
#    define MM_OPENGLES 1
#endif

typedef int8_t    i8;
typedef uint8_t   u8;
typedef int16_t   i16;
typedef uint16_t  u16;
typedef int32_t   i32;
typedef uint32_t  u32;
typedef int64_t   i64;
typedef uint64_t  u64;
typedef float     f32;
typedef double    f64;
typedef ptrdiff_t iptr;
typedef size_t    uptr;

#define MM_MAX_PATH           260
#define MM_MAX_NAME           64

#define MM_MEM_DEFAULT_ALIGN  16

// =============================================================================
// Array count macro
// =============================================================================
#define MM_ARRAY_COUNT(arr)   (sizeof(arr) / sizeof((arr)[0]))

// =============================================================================
// Min / Max / Clamp macros
// =============================================================================
#define MM_MIN(a, b)          ((a) < (b) ? (a) : (b))
#define MM_MAX(a, b)          ((a) > (b) ? (a) : (b))
#define MM_CLAMP(x, min, max) (MM_MAX((min), MM_MIN((x), (max))))

// =============================================================================
// Swap macro
// =============================================================================
#define MM_SWAP(a, b)     \
    do {                  \
        auto temp = (a);  \
        (a)       = (b);  \
        (b)       = temp; \
    } while (0)

// =============================================================================
// Stringify macros
// =============================================================================
#define MM_STRINGIFY(x)      #x
#define MM_TOSTRING(x)       MM_STRINGIFY(x)

// =============================================================================
// Concatenate macros
// =============================================================================
#define MM_CONCATENATE(a, b) a##b
#define CONCATENATE(a, b)    MM_CONCATENATE(a, b)

// =============================================================================
// Zero Memory macro
// =============================================================================
#include <string.h>
#define MM_ZERO_MEMORY(ptr)                memset((ptr), 0, sizeof(*(ptr)))

// =============================================================================
// Offset of a member in a struct
// =============================================================================
#define MM_OFFSET_OF(type, member)         ((size_t)&(((type *)0)->member))

// =============================================================================
// Container of macro
// =============================================================================
#define MM_CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - MM_OFFSET_OF(type, member)))

// =============================================================================
// Defer (scope guard) macro
// =============================================================================
// #ifdef __cplusplus
// template <typename F> struct Defer {
//     F func;
//     Defer(F func) : func(func) {}
//     ~Defer() { func(); }
// };

// #    define MM_DEFER(func)                                              \
//         auto CONCATENATE(_defer_, __COUNTER__) = Defer([&]() { func; })
// #endif

typedef enum mm_render_api {
    MM_RENDER_API_NONE = 0,
    MM_RENDER_API_METAL,
    MM_RENDER_API_VULKAN,
    MM_RENDER_API_OPENGLES,
    MM_RENDER_API_DIRECT3D11,
    MM_RENDER_API_DIRECT3D12,
    MM_RENDER_API_OPENGL,
    MM_RENDER_API_COUNT
} mm_render_api;

// typedef enum mm_result {
//     MM_RESULT_OK = 0,
//     MM_RESULT_ERROR,
//     MM_RESULT_INVALID_PARAMETER,
//     MM_RESULT_OUT_OF_MEMORY,
//     MM_RESULT_NOT_SUPPORTED,
//     MM_RESULT_NOT_FOUND,
//     MM_RESULT_TIMEOUT,
// } mm_result;

#if defined(__clang__)
#    if __has_attribute(ext_vector_type)
typedef int   int2 __attribute__((ext_vector_type(2)));
typedef float float2 __attribute__((ext_vector_type(2)));
typedef float float3 __attribute__((ext_vector_type(4)));
typedef float float4 __attribute__((ext_vector_type(4)));

#    else

struct alignas(8) int2 {
    int x, y;
};

struct alignas(8) float2 {
    f32 x, y;
};

struct alignas(16) float3 {
    f32 x, y, z, w;
};

struct alignas(16) float4 {
    f32 x, y, z, w;
};

#    endif
#endif

// typedef struct mm_platform_caps {
//     bool supports_vulkan;
//     bool supports_metal;
//     bool supports_opengles;
//     bool supports_direct3d11;
//     bool supports_direct3d12;
//     u32  max_texture_size;
//     u32  max_batch_size;
//     bool has_clipboard;
//     bool has_haptics;
//     bool has_gamepad;
// } mm_platform_caps;

typedef void (*mm_update_callback)(f32 delta_time);
typedef void (*mm_render_callback)();
typedef void (*mm_input_callback)(const void *event);
typedef void (*mm_cleanup_callback)();
