cmake_minimum_required(VERSION 3.24)
project(mmx_engine VERSION 1.0.0 LANGUAGES CXX C OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Platform Detection ──────────────────────────────────────────
if(APPLE)
    if(CMAKE_SYSTEM_NAME STREQUAL iOS)
        set(PLATFORM_IOS ON CACHE INTERNAL "Target iOS")
    else()
        set(PLATFORM_MACOS ON CACHE INTERNAL "Target macOS")
    endif()
elseif(ANDROID)
    set(PLATFORM_ANDROID ON CACHE INTERNAL "Target Android")
endif()

# ─── Options ────────────────────────────────────────────────────
option(USE_TRACY "Enable Tracy profiling" OFF)
option(USE_IMGUI "Include debug ImGui" OFF)
option(ENGINE_USE_STD_EXPECTED "Use std::expected instead of local fallback" ON)
option(ENGINE_FORCE_LOCAL_EXPECTED "Force local Expected<T,E> fallback even if std::expected available" OFF)
option(ENGINE_ENABLE_ASSERT "Enable runtime assertions and cache instrumentation" OFF)

# Android API level check
if(PLATFORM_ANDROID)
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

set(RPMALLOC_SRCS
    "${THIRDPARTY_DIR}/rpmalloc/rpmalloc/rpmalloc.c"
)
set(RPMALLOC_DEFS ENABLE_PRELOAD=0 ENABLE_OVERRIDE=0 RPMALLOC_CONFIGURED=1)

set(MINIAUDIO_SRCS
    "${THIRDPARTY_DIR}/miniaudio/miniaudio.c"
)
if(PLATFORM_IOS)
    set_source_files_properties(${MINIAUDIO_SRCS} PROPERTIES COMPILE_FLAGS "-x objective-c")
endif()

# ─── Shader Compilation ──────────────────────────────────────────
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glsl")
set(SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

find_program(GLSLAngValidator_EXECUTABLE glslangValidator
    HINTS /usr/local/bin /opt/homebrew/bin "$ENV{VULKAN_SDK}/bin")

set(SPV_FILES "")
set(SHADER_EMBED_SRCS "")

if(GLSLAngValidator_EXECUTABLE)
    file(GLOB GLSL_SOURCES "${SHADER_SRC_DIR}/*.vert" "${SHADER_SRC_DIR}/*.frag")
    foreach(GLSL ${GLSL_SOURCES})
        get_filename_component(SHADER_NAME ${GLSL} NAME_WE)
        get_filename_component(SHADER_EXT ${GLSL} LAST_EXT)
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
            COMMAND ${GLSLAngValidator_EXECUTABLE} -V -o "${SPV}" "${GLSL}"
            COMMENT "Compiling ${SHADER_NAME}.${SHADER_STAGE} → spv")
        list(APPEND SPV_FILES ${SPV})
        set(EMBED_CPP "${SHADER_OUT_DIR}/${SHADER_NAME}_${SHADER_STAGE}_spv.cpp")
        add_custom_command(
            OUTPUT ${EMBED_CPP}
            DEPENDS ${SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_OUT_DIR}"
            COMMAND python3 "${SHADER_SRC_DIR}/../embed_spv.py" "${SPV}" "${EMBED_CPP}"
            COMMENT "Embedding ${SHADER_NAME}.${SHADER_STAGE}.spv")
        list(APPEND SHADER_EMBED_SRCS ${EMBED_CPP})
    endforeach()
    add_custom_target(shader_embed ALL DEPENDS ${SHADER_EMBED_SRCS} ${SPV_FILES})
    set_target_properties(shader_embed PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# ─── Engine Sources ──────────────────────────────────────────────
set(ENGINE_SOURCES
    ${RPMALLOC_SRCS}
    ${MINIAUDIO_SRCS}

    core/mm_log.hpp
    core/mm_handle.hpp
    core/mm_slotmap.hpp
    core/mm_expected.hpp
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

    math/mm_math.h
    math/mm_vec2.h
    math/mm_vec3.h
    math/mm_vec4.h
    math/mm_mat3.h
    math/mm_mat4.h
    math/mm_color.h

    game/mm_board_grid.hpp
    game/mm_game_state.hpp
    game/mm_scene.hpp
    game/mm_tween_pool.hpp
    game/mm_particle_pool.hpp
    game/mm_text_popup_pool.hpp
    game/mm_camera_trauma.hpp
    game/mm_event_bus.hpp
    game/mm_event_bus.cpp
    game/mm_ember_pool.hpp

    input/mm_touch_gesture.hpp
    input/mm_input_event.hpp
    input/mm_input_state.hpp

    ui/mm_ui.hpp
    ui/mm_ui.cpp

    app/mm_app.hpp
    
    audio/mm_audio_system.hpp
    audio/mm_audio_system.cpp

    render/mm_sprite_batch.hpp
    render/mm_sort_key.hpp
    render/mm_render_graph.hpp
    render/mm_text_renderer.hpp
    render/mm_shaders.hpp
    render/mm_shader_registry.hpp
    render/mm_font_atlas.hpp
    render/mm_font_atlas.cpp
    render/mm_font_data.hpp
    render/mm_font_data.cpp
    render/mm_font_sym_data.hpp
    render/mm_font_sym_data.cpp
    render/mm_font_th_data.hpp
    render/mm_font_ttf_data.hpp
    render/mm_renderer.hpp
    render/mm_material.hpp
    render/mm_stb_image.cpp
    render/mm_texture_loader.hpp
    render/mm_sprite_loader.hpp

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
        examples/mm_06_shader_lab.cpp
        examples/mm_07_ui_lab.cpp
        entry/mm_game_entry.cpp
    )
endif()

# ─── MMX Engine Library (static) ─────────────────────────────────
add_library(mmx_engine STATIC ${ENGINE_SOURCES})
target_include_directories(mmx_engine SYSTEM PUBLIC
    "${THIRDPARTY_DIR}/stb"
    "${THIRDPARTY_DIR}/miniaudio"
    "${THIRDPARTY_DIR}/simdjson/include"
    "${THIRDPARTY_DIR}/rpmalloc/rpmalloc"
    "${THIRDPARTY_DIR}/metal-cpp"
    "${THIRDPARTY_DIR}/VMA/include"
    "${THIRDPARTY_DIR}/vk-bootstrap"
)
target_include_directories(mmx_engine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/math"
    "${SHADER_OUT_DIR}"
)
target_compile_definitions(mmx_engine PRIVATE ${RPMALLOC_DEFS})

if(PLATFORM_MACOS)
    target_compile_definitions(mmx_engine PUBLIC MM_PLATFORM_MACOS=1 MM_PLATFORM_APPLE=1)
elseif(PLATFORM_IOS)
    target_compile_definitions(mmx_engine PUBLIC MM_PLATFORM_IPHONE=1 MM_PLATFORM_APPLE=1)
elseif(PLATFORM_ANDROID)
    target_compile_definitions(mmx_engine PUBLIC MM_PLATFORM_ANDROID=1)
endif()

if(ENGINE_USE_STD_EXPECTED AND NOT ENGINE_FORCE_LOCAL_EXPECTED)
    target_compile_definitions(mmx_engine PUBLIC ENGINE_USE_STD_EXPECTED=1)
else()
    target_compile_definitions(mmx_engine PUBLIC ENGINE_USE_STD_EXPECTED=0)
endif()
if(ENGINE_ENABLE_ASSERT)
    target_compile_definitions(mmx_engine PUBLIC ENGINE_ENABLE_ASSERT=1)
endif()

# Platform framework links
if(PLATFORM_IOS)
    target_compile_definitions(mmx_engine PUBLIC TARGET_IOS USE_METAL_BACKEND)
    set_target_properties(mmx_engine PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
    target_link_libraries(mmx_engine PUBLIC
        "-framework Foundation" "-framework UIKit" "-framework Metal"
        "-framework MetalKit" "-framework QuartzCore" "-framework CoreGraphics"
        "-framework CoreHaptics" "-framework CoreFoundation" "-framework CoreAudio"
        "-framework AudioToolbox" "-framework AVFoundation")
elseif(PLATFORM_MACOS)
    target_compile_definitions(mmx_engine PUBLIC TARGET_MACOS USE_METAL_BACKEND)
    target_link_libraries(mmx_engine PUBLIC
        "-framework Cocoa" "-framework Metal" "-framework MetalKit"
        "-framework QuartzCore" "-framework CoreVideo")
elseif(PLATFORM_ANDROID)
    target_compile_definitions(mmx_engine PUBLIC TARGET_ANDROID USE_VULKAN_BACKEND VK_USE_PLATFORM_ANDROID_KHR)
    target_include_directories(mmx_engine PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(mmx_engine PUBLIC
        android nativewindow EGL vulkan log)
endif()

target_compile_options(mmx_engine PRIVATE
    -Wall -Wextra -Wpedantic
    -Wconversion -Wshadow
    -Wno-c++98-compat-pedantic -Wno-padded -Wno-weak-vtables
    -Wno-float-equal -Wno-shadow-field-in-constructor
    -Wno-gnu-anonymous-struct -Wno-nested-anon-types
    -Wno-extern-initializer
    -fno-exceptions -fno-rtti)

if(USE_TRACY)
    target_compile_definitions(mmx_engine PUBLIC TRACY_ENABLE)
    target_compile_definitions(mmx_engine PRIVATE TRACY_NO_EXCEPTIONS)
    target_include_directories(mmx_engine PUBLIC "${THIRDPARTY_DIR}/tracy")
    target_sources(mmx_engine PRIVATE "${THIRDPARTY_DIR}/tracy/TracyClient.cpp")
endif()

if(USE_IMGUI)
    target_compile_definitions(mmx_engine PUBLIC USE_IMGUI)
    target_include_directories(mmx_engine PUBLIC "${THIRDPARTY_DIR}/imgui")
    target_sources(mmx_engine PRIVATE
        thirdparty/imgui/imgui.cpp
        thirdparty/imgui/imgui_draw.cpp
        thirdparty/imgui/imgui_widgets.cpp)
endif()

# Android: shader embedding
if(PLATFORM_ANDROID)
    set_source_files_properties(${SHADER_EMBED_SRCS} PROPERTIES GENERATED TRUE)
    target_sources(mmx_engine PRIVATE ${SHADER_EMBED_SRCS})
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
        target_link_libraries(${EXAMPLE_NAME} PRIVATE mmx_engine)
        if(PLATFORM_MACOS)
            target_link_libraries(${EXAMPLE_NAME} PRIVATE "-framework Foundation" "-framework Cocoa" "-framework Metal" "-framework MetalKit" "-framework QuartzCore" "-framework CoreVideo")
        endif()
    endforeach()
    if(PLATFORM_MACOS OR PLATFORM_IOS)
        set_source_files_properties(${EXAMPLE_SOURCES} PROPERTIES COMPILE_FLAGS "-x objective-c++")
    endif()
    if(PLATFORM_IOS)
        foreach(EXAMPLE ${EXAMPLE_SOURCES})
            get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
            set_target_properties(${EXAMPLE_NAME} PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.mmx.${EXAMPLE_NAME}"
                MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
        endforeach()
    endif()
endif()

# ─── Tests ──────────────────────────────────────────────────────
option(BUILD_TESTS "Build test targets" ON)
if(BUILD_TESTS)
    enable_testing()
    # One loop, not 17 copies: every engine test is headless CPU and belongs in
    # the fast lane. `ctest -L quick` used to skip ALL of them (freecell was the
    # only CMakeLists that set LABELS), so the documented fast path silently
    # covered zero UI tests. Keep LABELS here or the suite rots again.
    set(ENGINE_TESTS
        backend_concept_tests
        keymap_tests
        layout_tests
        listbox_tests
        grid_tests
        combobox_tests
        radiobox_tests
        checkbox_tests
        toggle_tests
        button_tests
        slider_tests
        textfield_tests
        image_tests
        mipmap_tests
        snap_tests
        event_tests
        event_bus_tests
        ui_update_tests
        text_tests
        progressbar_tests
        tabbar_tests
        scrollview_tests
        accordion_tests
        label_tests
        touch_discard_tests
        ember_tests
        vec2_tests
        vec3_tests
        vec4_tests
        mat3_tests
        mat4_tests
        math_props_tests
        rendergraph_tests
        color_tests
        rect_tests
    )
    foreach(TEST_NAME ${ENGINE_TESTS})
        add_executable(${TEST_NAME} tests/${TEST_NAME}.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE mmx_engine)
        if(PLATFORM_MACOS)
            target_link_libraries(${TEST_NAME} PRIVATE "-framework Foundation" "-framework Cocoa" "-framework Metal" "-framework MetalKit" "-framework QuartzCore" "-framework CoreVideo")
        endif()
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
        set_tests_properties(${TEST_NAME} PROPERTIES LABELS "quick")
    endforeach()
endif()

message(STATUS "─── MMX Engine Build ───")
message(STATUS "Platform:    ${CMAKE_SYSTEM_NAME}")
if(GLSLAngValidator_EXECUTABLE)
    message(STATUS "glslangValidator: ${GLSLAngValidator_EXECUTABLE}")
else()
    message(STATUS "glslangValidator: not found")
endif()
message(STATUS "Tracy:       ${USE_TRACY}")
message(STATUS "ImGui:       ${USE_IMGUI}")
message(STATUS "Examples:    ${BUILD_EXAMPLES}")
