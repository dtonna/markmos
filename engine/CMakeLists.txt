cmake_minimum_required(VERSION 3.24)
project(markmos VERSION 1.0.0 LANGUAGES CXX C)

# ─── C++ Standard ───────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Policies ───────────────────────────────────────────────────
cmake_policy(SET CMP0140 NEW)  # Apple system frameworks

# ─── Options ────────────────────────────────────────────────────
option(USE_TRACY "Enable Tracy profiling" OFF)
option(USE_IMGUI "Include debug ImGui" OFF)
option(ENGINE_USE_STD_EXPECTED "Use std::expected instead of local fallback" ON)
option(ENGINE_FORCE_LOCAL_EXPECTED "Force local Expected<T,E> fallback even if std::expected available" OFF)
option(ENGINE_ENABLE_ASSERT "Enable runtime assertions and cache instrumentation" OFF)

# ─── Platform Detection ──────────────────────────────────────────
if(APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL iOS)
        set(PLATFORM_IOS ON)
    else()
        set(PLATFORM_MACOS ON)
    endif()
elseif(ANDROID)
    set(PLATFORM_ANDROID ON)
    set(MM_ANDROID_API_LEVEL "${CMAKE_SYSTEM_VERSION}")
    if(ANDROID_PLATFORM MATCHES "^android-([0-9]+)$")
        set(MM_ANDROID_API_LEVEL "${CMAKE_MATCH_1}")
    elseif(ANDROID_NATIVE_API_LEVEL MATCHES "^android-([0-9]+)$")
        set(MM_ANDROID_API_LEVEL "${CMAKE_MATCH_1}")
    elseif(ANDROID_NATIVE_API_LEVEL MATCHES "^[0-9]+$")
        set(MM_ANDROID_API_LEVEL "${ANDROID_NATIVE_API_LEVEL}")
    endif()
    if(MM_ANDROID_API_LEVEL VERSION_LESS 26)
        message(FATAL_ERROR
            "Android Vulkan build needs ANDROID_PLATFORM=android-26 or newer "
            "because libnativewindow is unavailable for lower API levels. "
            "Run ./build-android.sh or configure with -DANDROID_PLATFORM=android-30.")
    endif()
endif()

# ─── Thirdparty Includes ────────────────────────────────────────
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")

# rpmalloc — compiled as part of engine, not a separate target
set(RPMALLOC_SRCS
    "${THIRDPARTY_DIR}/rpmalloc/rpmalloc/rpmalloc.c"
)
set(RPMALLOC_DEFS ENABLE_PRELOAD=0 ENABLE_OVERRIDE=0 RPMALLOC_CONFIGURED=1)

# miniaudio — compiled as part of engine (miniaudio.c = implementation)
set(MINIAUDIO_SRCS
    "${THIRDPARTY_DIR}/miniaudio/miniaudio.c"
)
if(PLATFORM_IOS)
    # CoreAudio headers on iOS use Objective-C types (NSString etc.)
    set_source_files_properties(${MINIAUDIO_SRCS} PROPERTIES COMPILE_FLAGS "-x objective-c")
endif()
# simdjson — header-only
# stb — header-only
# metal-cpp — header-only
# VMA — included from vulkan_backend.hpp
# vk-bootstrap — .h + .cpp compiled directly

# ─── Shader Compilation ──────────────────────────────────────────
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glsl")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# Find shader compiler tool
find_program(GLSLAngValidator_EXECUTABLE glslangValidator
    HINTS /usr/local/bin /opt/homebrew/bin
          "$ENV{VULKAN_SDK}/bin")

# ─── SPIR-V Embedding ─────────────────────────────────────────────
# GLSL → SPIR-V via glslangValidator (for Vulkan backend)
# Metal backend uses inline MSL strings in mm_shaders.hpp
set(SPV_FILES "")
set(SHADER_EMBED_SRCS "")

if(GLSLAngValidator_EXECUTABLE)
    file(GLOB GLSL_SOURCES "${SHADER_SRC_DIR}/*.vert" "${SHADER_SRC_DIR}/*.frag")

    foreach(GLSL ${GLSL_SOURCES})
        get_filename_component(SHADER_NAME ${GLSL} NAME_WE)
        get_filename_component(SHADER_EXT ${GLSL} LAST_EXT)
        # stage name for SPIR-V: vert → vertex, frag → fragment
        if(SHADER_EXT STREQUAL ".vert")
            set(SHADER_STAGE "vert")
        elseif(SHADER_EXT STREQUAL ".frag")
            set(SHADER_STAGE "frag")
        endif()
        set(SPV "${SHADER_OUT_DIR}/${SHADER_NAME}_${SHADER_STAGE}.spv")

        add_custom_command(
            OUTPUT ${SPV}
            DEPENDS ${GLSL}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
            COMMAND ${GLSLAngValidator_EXECUTABLE}
                -V
                -o "${SPV}"
                "${GLSL}"
            COMMENT "Compiling ${SHADER_NAME}.${SHADER_STAGE} → spv"
        )
        list(APPEND SPV_FILES ${SPV})

        set(EMBED_CPP "${SHADER_OUT_DIR}/${SHADER_NAME}_${SHADER_STAGE}_spv.cpp")
        add_custom_command(
            OUTPUT ${EMBED_CPP}
            DEPENDS ${SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
            COMMAND python3 "${SHADER_SRC_DIR}/../embed_spv.py" "${SPV}" "${EMBED_CPP}"
            COMMENT "Embedding ${SHADER_NAME}.${SHADER_STAGE}.spv"
        )
        list(APPEND SHADER_EMBED_SRCS ${EMBED_CPP})
    endforeach()

    add_custom_target(shader_embed ALL DEPENDS ${SHADER_EMBED_SRCS} ${SPV_FILES})
    set_target_properties(shader_embed PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# ─── Engine Sources ──────────────────────────────────────────────
set(ENGINE_SOURCES
    ${RPMALLOC_SRCS}
    ${MINIAUDIO_SRCS}

    # Core
    core/mm_handle.hpp
    core/mm_slotmap.hpp
    core/mm_arena.hpp
    core/mm_ring_buffer.hpp
    core/mm_save_data.hpp
    core/mm_save_data.cpp
    core/mm_expected.hpp
    core/mm_span2d.hpp
    core/mm_cache_metrics.hpp
    core/mm_tracy.hpp
    core/mm_pool.hpp
    core/mm_pool.cpp
    core/mm_job_system.hpp
    core/mm_job_system.cpp
    core/mm_vfs.hpp
    core/mm_vfs.cpp

    # Game
    game/mm_board_grid.hpp
    game/mm_game_state.hpp
    game/mm_scene.hpp
    game/mm_tween_pool.hpp
    game/mm_particle_pool.hpp
    game/mm_text_popup_pool.hpp
    game/mm_camera_trauma.hpp

    # Input
    input/mm_touch_gesture.hpp
    input/mm_input_event.hpp
    input/mm_input_state.hpp

    # UI
    ui/mm_ui.hpp
    ui/mm_ui.cpp

    # App
    app/mm_app.hpp
    audio/mm_audio_system.hpp
    audio/mm_audio_system.cpp

    # Render
    render/mm_sprite_batch.hpp
    render/mm_sort_key.hpp
    render/mm_render_graph.hpp
    render/mm_text_renderer.hpp
    render/mm_shaders.hpp
    render/mm_font_atlas.hpp
    render/mm_font_atlas.cpp
    render/mm_font_data.hpp
    render/mm_font_data.cpp
    render/mm_font_ttf_data.hpp
    render/mm_renderer.hpp
    render/mm_material.hpp
    render/mm_stb_image.cpp
    render/mm_texture_loader.hpp
)

# ─── Platform-Specific Sources ───────────────────────────────────
if(PLATFORM_IOS)
    list(APPEND ENGINE_SOURCES
        rhi/mm_rhi_concept.hpp
        rhi/mm_metal_backend.hpp
        rhi/mm_metal_backend_priv.mm
        app/mm_app_ios.mm
    )
    set_source_files_properties(app/mm_app_ios.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

elseif(PLATFORM_MACOS)
    list(APPEND ENGINE_SOURCES
        rhi/mm_rhi_concept.hpp
        rhi/mm_metal_backend.hpp
        rhi/mm_metal_backend_priv.mm
        app/mm_app_mac.mm
    )

elseif(PLATFORM_ANDROID)
    list(APPEND ENGINE_SOURCES
        rhi/mm_rhi_concept.hpp
        rhi/mm_vulkan_backend.hpp
        rhi/mm_vma_impl.cpp
        app/mm_app_android.cpp
        thirdparty/vk-bootstrap/VkBootstrap.h
        thirdparty/vk-bootstrap/VkBootstrap.cpp
    )
endif()

# ─── Examples ────────────────────────────────────────────────────
option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    set(EXAMPLE_SOURCES
        examples/mm_01_sprite_10k.cpp
        examples/mm_02_cube_100.cpp
        examples/mm_03_match3_board.cpp
        examples/mm_04_block_puzzle.cpp
        examples/mm_05_particle_stress.cpp
    )
endif()

# ─── Main Library ────────────────────────────────────────────────
add_library(markmos STATIC ${ENGINE_SOURCES})
target_include_directories(markmos SYSTEM PUBLIC
    "${THIRDPARTY_DIR}/stb"
    "${THIRDPARTY_DIR}/miniaudio"
    "${THIRDPARTY_DIR}/simdjson/include"
    "${THIRDPARTY_DIR}/rpmalloc/rpmalloc"   # rpmalloc.h
    "${THIRDPARTY_DIR}/metal-cpp"
    "${THIRDPARTY_DIR}/VMA/include"
    "${THIRDPARTY_DIR}/vk-bootstrap"
)

target_include_directories(markmos PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/math"
    "${SHADER_OUT_DIR}"
)

target_compile_definitions(markmos PRIVATE ${RPMALLOC_DEFS})

if(PLATFORM_MACOS)
    target_compile_definitions(markmos PUBLIC MM_PLATFORM_MACOS=1 MM_PLATFORM_APPLE=1)
elseif(PLATFORM_IOS)
    target_compile_definitions(markmos PUBLIC MM_PLATFORM_IPHONE=1 MM_PLATFORM_APPLE=1)
elseif(PLATFORM_ANDROID)
    target_compile_definitions(markmos PUBLIC MM_PLATFORM_ANDROID=1)
endif()

if(ENGINE_USE_STD_EXPECTED AND NOT ENGINE_FORCE_LOCAL_EXPECTED)
    target_compile_definitions(markmos PUBLIC ENGINE_USE_STD_EXPECTED=1)
else()
    target_compile_definitions(markmos PUBLIC ENGINE_USE_STD_EXPECTED=0)
endif()
if(ENGINE_ENABLE_ASSERT)
    target_compile_definitions(markmos PUBLIC ENGINE_ENABLE_ASSERT=1)
endif()

# Platform defines
if(PLATFORM_IOS)
    target_compile_definitions(markmos PUBLIC TARGET_IOS USE_METAL_BACKEND)
    set_target_properties(markmos PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    )
    target_link_libraries(markmos PUBLIC
        "-framework Foundation"
        "-framework UIKit"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework CoreHaptics"
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AVFoundation"
    )
elseif(PLATFORM_MACOS)
    target_compile_definitions(markmos PUBLIC TARGET_MACOS USE_METAL_BACKEND)
    target_link_libraries(markmos PUBLIC
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework CoreVideo"
    )
elseif(PLATFORM_ANDROID)
    target_compile_definitions(markmos PUBLIC TARGET_ANDROID USE_VULKAN_BACKEND VK_USE_PLATFORM_ANDROID_KHR)
    target_include_directories(markmos PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(markmos PUBLIC
        android
        nativewindow
        EGL
        vulkan
        log
    )
endif()

# Generic compile options
target_compile_options(markmos PRIVATE
    -Wall -Wextra -Wpedantic
    -Wconversion -Wshadow
    -Wno-c++98-compat-pedantic
    -Wno-padded
    -Wno-weak-vtables
    -Wno-float-equal
    -Wno-shadow-field-in-constructor
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
    -Wno-extern-initializer
    -fno-exceptions  # Expected<T,E> fallback, no try/catch in engine
    -fno-rtti        # no virtual, no RTTI
)

if(USE_TRACY)
    target_compile_definitions(markmos PUBLIC TRACY_ENABLE)
    target_compile_definitions(markmos PRIVATE TRACY_NO_EXCEPTIONS)
    target_include_directories(markmos PUBLIC "${THIRDPARTY_DIR}/tracy")
    target_sources(markmos PRIVATE "${THIRDPARTY_DIR}/tracy/TracyClient.cpp")
endif()

if(USE_IMGUI)
    target_compile_definitions(markmos PUBLIC USE_IMGUI)
    target_include_directories(markmos PUBLIC "${THIRDPARTY_DIR}/imgui")
    target_sources(markmos PRIVATE
        thirdparty/imgui/imgui.cpp
        thirdparty/imgui/imgui_draw.cpp
        thirdparty/imgui/imgui_widgets.cpp
    )
endif()

# ─── Renderer Header (includes platform-specific sources)
if(PLATFORM_ANDROID)
    # Vulkan backend - shaders needed
    set_source_files_properties(${SHADER_EMBED_SRCS} PROPERTIES GENERATED TRUE)
    target_sources(markmos PRIVATE ${SHADER_EMBED_SRCS})
endif()

# ─── Examples (executables) ─────────────────────────────────────
if(BUILD_EXAMPLES)
    foreach(EXAMPLE ${EXAMPLE_SOURCES})
        get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
        if(PLATFORM_ANDROID)
            add_library(${EXAMPLE_NAME} SHARED
                ${EXAMPLE}
                ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
            target_link_options(${EXAMPLE_NAME} PRIVATE "-Wl,-z,max-page-size=16384")
        else()
            add_executable(${EXAMPLE_NAME} ${EXAMPLE})
        endif()
        target_link_libraries(${EXAMPLE_NAME} PRIVATE markmos)
    endforeach()

    # Examples include Metal backend header (ObjC) via mm_renderer.hpp
    if(PLATFORM_MACOS OR PLATFORM_IOS)
        set_source_files_properties(${EXAMPLE_SOURCES} PROPERTIES COMPILE_FLAGS "-x objective-c++")
    endif()

    # iOS example targets need UIApplication — wrap as app bundles
    if(PLATFORM_IOS)
        foreach(EXAMPLE ${EXAMPLE_SOURCES})
            get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
            set_target_properties(${EXAMPLE_NAME} PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.markmos.${EXAMPLE_NAME}"
                MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
            )
        endforeach()
    endif()
endif()

# ─── Production Game Entry ──────────────────────────────────────
option(BUILD_GAME "Build production game entry" ON)
if(BUILD_GAME)
    file(GLOB_RECURSE GAME_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")

    if(PLATFORM_ANDROID)
        # Android: NativeActivity requires a shared library, not an executable
        add_library(markmos_game SHARED
            entry/mm_game_entry.cpp
            ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
            ${GAME_RESOURCES})
    else()
        add_executable(markmos_game entry/mm_game_entry.cpp ${GAME_RESOURCES})
    endif()

    if(GLSLAngValidator_EXECUTABLE AND PLATFORM_ANDROID)
        add_dependencies(markmos_game shader_embed)
    endif()

    # Mark assets as bundle resources for macOS/iOS — preserve subdirectory structure
    if(PLATFORM_MACOS OR PLATFORM_IOS)
        foreach(RESOURCE ${GAME_RESOURCES})
            string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/assets/" "" REL "${RESOURCE}")
            get_filename_component(DIR "${REL}" DIRECTORY)
            if(DIR)
                set_source_files_properties("${RESOURCE}" PROPERTIES
                    MACOSX_PACKAGE_LOCATION "Resources/${DIR}")
            else()
                set_source_files_properties("${RESOURCE}" PROPERTIES
                    MACOSX_PACKAGE_LOCATION "Resources")
            endif()
        endforeach()
    endif()

    target_link_libraries(markmos_game PRIVATE markmos)

    if(PLATFORM_ANDROID)
        # Fix 16 KB page size compatibility warning for Android 15
        target_link_options(markmos_game PRIVATE "-Wl,-z,max-page-size=16384")
    endif()

    if(PLATFORM_MACOS OR PLATFORM_IOS)
        set_source_files_properties(entry/mm_game_entry.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
    endif()
    if(PLATFORM_MACOS)
        set_target_properties(markmos_game PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.markmos.game"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        )
    endif()
    if(PLATFORM_IOS)
        set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.markmos.game")
        set(MACOSX_BUNDLE_NAME "Markmos")
        set(MACOSX_BUNDLE_DISPLAY_NAME "Markmos")
        set(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
        set(MACOSX_BUNDLE_BUNDLE_VERSION "1.0")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/app/Info.plist.in" "${CMAKE_CURRENT_BINARY_DIR}/markmos_game.dir/Info.plist" @ONLY)
        set_target_properties(markmos_game PROPERTIES
            MACOSX_BUNDLE TRUE
            XCODE_ATTRIBUTE_INFOPLIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/markmos_game.dir/Info.plist"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.markmos.game"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        )
        # Link frameworks directly on the executable target (simulator needs explicit linkage)
        target_link_libraries(markmos_game PRIVATE
            "-framework Foundation"
            "-framework UIKit"
            "-framework Metal"
            "-framework MetalKit"
            "-framework QuartzCore"
            "-framework CoreGraphics"
            "-framework CoreHaptics"
            "-framework CoreFoundation"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AVFoundation"
        )
    endif()
endif()

# ─── Tests ──────────────────────────────────────────────────────
option(BUILD_TESTS "Build test targets" ON)
if(BUILD_TESTS)
    enable_testing()

    add_executable(backend_concept_tests
        tests/backend_concept_tests.cpp
    )
    target_link_libraries(backend_concept_tests PRIVATE markmos)
    add_test(NAME backend_concept_tests COMMAND backend_concept_tests)
endif()

# ─── Dependency Summary ─────────────────────────────────────────
message(STATUS "─── Markmos Build ───")
message(STATUS "Platform:    ${CMAKE_SYSTEM_NAME}")
if(GLSLAngValidator_EXECUTABLE)
    message(STATUS "glslangValidator: ${GLSLAngValidator_EXECUTABLE}")
else()
    message(STATUS "glslangValidator: not found")
endif()
message(STATUS "Tracy:       ${USE_TRACY}")
message(STATUS "ImGui:       ${USE_IMGUI}")
message(STATUS "Examples:    ${BUILD_EXAMPLES}")
