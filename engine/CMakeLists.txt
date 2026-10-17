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
option(ENGINE_USE_STD_EXPECTED "Use std::expected instead of local fallback" ON)
option(ENGINE_FORCE_LOCAL_EXPECTED "Force local Expected<T,E> fallback even if std::expected available" OFF)
option(ENGINE_ENABLE_ASSERT "Enable runtime assertions and cache instrumentation" OFF)

# ─── Embedded font sets ───────────────────────────────────────────
# Karla (ASCII) is always linked: every app draws text. The rest are
# whole TTFs baked into the binary for glyphs that most apps never draw,
# so each one is opt-in and the default is OFF. See AGENTS.md
# "Feature selection" for the rule this follows.
option(ENGINE_FONT_TH   "Bake Sarabun Thai glyphs (+94KB)"                 OFF)
option(ENGINE_FONT_SYM  "Bake NotoSansSymbols2 suit glyphs (+1.2MB)"      OFF)
option(ENGINE_FONT_SYM1 "Bake the undo glyph U+238C from NotoSansSymbols1 (+181KB)" OFF)

# Read by the shader list below, before the examples themselves are added.
option(BUILD_EXAMPLES "Build example programs" ON)

# miniaudio is a 1.1 MB translation unit linked into every binary the moment the
# host touches g_audio_system. OFF compiles the stub instead — see
# engine/audio/mm_audio_system_stub.cpp.
option(ENGINE_AUDIO "Audio playback via miniaudio (+1.1MB)" ON)
# Theme::load is the only thing in engine/ui that reads a file, and it uses
# simdjson to do it. Measured 118.6 KB of simdjson in libfreecell.so for one
# flat object of colours — and only markmos in this repo calls it at all.
option(ENGINE_THEME_JSON "Parse UI themes from JSON at runtime (simdjson, +119KB)" OFF)
# stb_image compiles a decoder per format. Every first-party image in this repo
# is a PNG (893 files, zero of anything else), so the other seven decoders are
# 55.7 KB of libfreecell.so that nothing can reach. PUBLIC, not PRIVATE: only
# mm_stb_image.cpp has STB_IMAGE_IMPLEMENTATION, but mm_card_atlas.hpp includes
# the same header WITHOUT the macros, and the two must agree on what exists.
option(ENGINE_IMAGE_PNG_ONLY "Compile stb_image with the PNG decoder only (-56KB)" ON)
# miniaudio builds a decoder for every format whether or not anything plays it.
# Measured in libfreecell.so on Android: FLAC 200 KB + MP3 60 KB, while the
# only sound files in this repo are .wav. WAV and raw PCM are not optional —
# ma_decoder itself is only excluded by MA_NO_DECODING.
option(ENGINE_AUDIO_MP3  "Keep the MP3 decoder (+60KB)"  OFF)
option(ENGINE_AUDIO_FLAC "Keep the FLAC decoder (+200KB)" OFF)

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

  # Submodules that are not initialised leave an empty directory (or no
  # directory at all), and the failure that follows is a wall of "cannot open
  # source file" from deep inside a library's own headers — which says nothing
  # about the cause. A plain `git clone` without --recursive lands here.
  #
  # The check is per-directory, non-recursive on purpose: a GLOB_RECURSE would
  # also fire on a directory that exists but was emptied, and the same missing
  # sentinel catches a stale checkout at the wrong commit.
  set(MMX_SUBMODULES
      miniaudio          # miniaudio/miniaudio.h
      rpmalloc           # rpmalloc/rpmalloc/rpmalloc.c
      simdjson           # simdjson/include/simdjson.h
      VMA                # VMA/include/vk_mem_alloc.h
      vk-bootstrap       # vendored: no upstream tag compiles against NDK vulkan 275
      unordered_dense    # unordered_dense/include/ankerl/unordered_dense.h
      metal-cpp          # metal-cpp/Metal/Metal.hpp
      tracy              # tracy/public/tracy/Tracy.hpp
      stb                # stb/stb.h
  )
  set(MMX_MISSING_SUBMODULES "")
  foreach(_lib IN LISTS MMX_SUBMODULES)
      file(GLOB _found "${THIRDPARTY_DIR}/${_lib}/*")
      if(_found STREQUAL "")
          list(APPEND MMX_MISSING_SUBMODULES "${_lib}")
      endif()
  endforeach()
  if(MMX_MISSING_SUBMODULES)
      string(REPLACE ";" "\n  - " _pretty "${MMX_MISSING_SUBMODULES}")
      message(FATAL_ERROR
          "These thirdparty directories are empty, which usually means their git "
          "submodule was never initialised:\n  - ${_pretty}\n\n"
          "Run:\n  git submodule update --init --recursive\n\n"
          "or clone with:\n  git clone --recursive")
  endif()

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
    # Explicit list, NOT file(GLOB). A glob picks up every shader anyone drops
    # in the folder, which is how nine pipelines ended up with no caller: the
    # shader looked built and plausible, so nobody asked who used it. Adding a
    # shader here means answering "which target calls this" — see AGENTS.md.
    #
    # These are the shaders the ENGINE's default pipelines compile. Examples
    # that build their own pipelines from shader bytes (mm_02 cube, mm_06's
    # outline/dissolve/grayscale/color-grade modes) also need theirs, so those
    # are appended when BUILD_EXAMPLES is on.
    set(SHADERS_ENGINE
        sprite.vert          sprite.frag
        sdf.vert             sdf.frag
        rounded_sprite.vert  rounded_sprite.frag
        particle.vert        particle.frag
        glow_pulse.frag
        gold_border.frag
        gold_stay.frag
        ring.frag
        normal_derive.frag
        normal_map.frag
        cartoon.frag
        plastic.frag
    )
    if(BUILD_EXAMPLES)
        list(APPEND SHADERS_ENGINE
            cube.vert           cube.frag
            sprite_outline.frag
            dissolve.frag
            grayscale.frag
            color_grade.frag
        )
    endif()

    foreach(SHADER_REL ${SHADERS_ENGINE})
        set(GLSL "${SHADER_SRC_DIR}/${SHADER_REL}")
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

    # A shader on disk that nothing lists above is dead weight: it will not be
    # compiled here, and on Metal nothing references it. Say so at configure
    # time rather than letting the folder and the build drift apart.
    file(GLOB GLSL_ON_DISK "${SHADER_SRC_DIR}/*.vert" "${SHADER_SRC_DIR}/*.frag")
    set(GLSL_UNLISTED "")
    foreach(GLSL ${GLSL_ON_DISK})
        get_filename_component(REL "${GLSL}" NAME)
        if(NOT REL IN_LIST SHADERS_ENGINE)
            list(APPEND GLSL_UNLISTED ${REL})
        endif()
    endforeach()
    if(GLSL_UNLISTED)
        message(WARNING
            "Shader(s) present in ${SHADER_SRC_DIR} but not in SHADERS_ENGINE "
            "(or its BUILD_EXAMPLES block): ${GLSL_UNLISTED}. They will not be "
            "compiled. Add them and name the target that uses them, or delete them.")
    endif()

    add_custom_target(shader_embed ALL DEPENDS ${SHADER_EMBED_SRCS} ${SPV_FILES})
    set_target_properties(shader_embed PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# ─── Engine Sources ──────────────────────────────────────────────
set(ENGINE_SOURCES
    ${RPMALLOC_SRCS}

    core/mm_log.hpp
    core/mm_handle.hpp
    core/mm_slotmap.hpp
    core/mm_expected.hpp
    core/mm_arena.hpp
    core/mm_save_data.hpp
    core/mm_save_data.cpp
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
    render/mm_font_ttf_data.hpp
    render/mm_renderer.hpp
    render/mm_material.hpp
    render/mm_stb_image.cpp
    render/mm_texture_loader.hpp
    render/mm_sprite_loader.hpp

)

# ─── Audio ───────────────────────────────────────────────────────
# Exactly one of these two is compiled. The stub keeps the API identical, so
# nothing above this point has to know which one it got.
if(ENGINE_AUDIO)
    list(APPEND ENGINE_SOURCES
        ${MINIAUDIO_SRCS}
        audio/mm_audio_system.cpp
    )
    # BOTH translation units must agree: miniaudio guards struct members on these
    # macros (ma_wav carries a decoder member only when MA_NO_WAV is unset), so
    # defining them for the implementation alone would leave the header parsed
    # two different ways in the same binary.
    set(_AUDIO_FORMAT_DEFS "")
    if(NOT ENGINE_AUDIO_MP3)
        list(APPEND _AUDIO_FORMAT_DEFS MA_NO_MP3)
    endif()
    if(NOT ENGINE_AUDIO_FLAC)
        list(APPEND _AUDIO_FORMAT_DEFS MA_NO_FLAC)
    endif()
    if(_AUDIO_FORMAT_DEFS)
        set_source_files_properties(
            ${MINIAUDIO_SRCS} audio/mm_audio_system.cpp
            PROPERTIES COMPILE_DEFINITIONS "${_AUDIO_FORMAT_DEFS}")
    endif()
    set(_AUDIO_KEPT "wav pcm")
    if(ENGINE_AUDIO_MP3)
        list(APPEND _AUDIO_KEPT mp3)
    endif()
    if(ENGINE_AUDIO_FLAC)
        list(APPEND _AUDIO_KEPT flac)
    endif()
    message(STATUS "Audio:         miniaudio, decoders kept: ${_AUDIO_KEPT}")
else()
    list(APPEND ENGINE_SOURCES audio/mm_audio_system_stub.cpp)
endif()

# ─── Embedded Font Sets ───────────────────────────────────────────
# Each optional font set is a whole TTF linked into every binary that
# enables it. The data TU is the only thing that has to be conditional —
# the headers are reached through mm_font_data.hpp, which gates them with
# the same MM_ENGINE_FONT_* macros defined below.
if(ENGINE_FONT_TH)
    list(APPEND ENGINE_SOURCES render/mm_font_th_data.hpp)
endif()
if(ENGINE_FONT_SYM)
    list(APPEND ENGINE_SOURCES
        render/mm_font_sym_data.hpp
        render/mm_font_sym_data.cpp
    )
endif()

# ─── Platform-Specific Sources ───────────────────────────────────
if(PLATFORM_IOS)
    list(APPEND ENGINE_SOURCES
        rhi/mm_rhi_concept.hpp
        rhi/mm_metal_backend.hpp
        rhi/mm_metal_backend_priv.mm
        app/mm_app_globals.cpp
        app/mm_app_ios.mm
    )
    set_source_files_properties(app/mm_app_ios.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(PLATFORM_MACOS)
    list(APPEND ENGINE_SOURCES
        rhi/mm_rhi_concept.hpp
        rhi/mm_metal_backend.hpp
        rhi/mm_metal_backend_priv.mm
        app/mm_app_globals.cpp
        app/mm_app_mac.mm
    )
elseif(PLATFORM_ANDROID)
    list(APPEND ENGINE_SOURCES
        rhi/mm_rhi_concept.hpp
        rhi/mm_vulkan_backend.hpp
        rhi/mm_vma_impl.cpp
        app/mm_app_globals.cpp
        app/mm_app_android.cpp
          thirdparty/vk-bootstrap/VkBootstrap.h
          thirdparty/vk-bootstrap/VkBootstrap.cpp
    )
endif()

# ─── Examples ────────────────────────────────────────────────────
# Which of them, by target name. Empty means all seven.
#
# This exists for the Android labs APK, which packages two of them: the other
# five would be a link apiece for a library nothing loads, and — the reason that
# matters more — "all seven" is a default that quietly builds a sixth one nobody
# looks at. A name in this list that does not match a source is an error rather
# than a silence, so a typo cannot leave you with an APK whose launcher icon does
# nothing.
set(MM_EXAMPLES "" CACHE STRING "Example targets to build when BUILD_EXAMPLES=ON (empty = all)")
if(BUILD_EXAMPLES)
    set(_ALL_EXAMPLE_SOURCES
        examples/mm_01_sprite_10k.cpp
        examples/mm_02_cube_100.cpp
        examples/mm_03_match3_board.cpp
        examples/mm_04_block_puzzle.cpp
        examples/mm_05_particle_stress.cpp
        examples/mm_06_shader_lab.cpp
        examples/mm_07_ui_lab.cpp
    )
    if(MM_EXAMPLES)
        set(EXAMPLE_SOURCES "")
        foreach(_want IN LISTS MM_EXAMPLES)
            set(_found FALSE)
            foreach(_src IN LISTS _ALL_EXAMPLE_SOURCES)
                get_filename_component(_name ${_src} NAME_WE)
                if(_name STREQUAL ${_want})
                    list(APPEND EXAMPLE_SOURCES ${_src})
                    set(_found TRUE)
                endif()
            endforeach()
            if(NOT _found)
                message(FATAL_ERROR "MM_EXAMPLES names '${_want}', which is not one of the examples")
            endif()
        endforeach()
    else()
        set(EXAMPLE_SOURCES ${_ALL_EXAMPLE_SOURCES})
    endif()
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

# Font sets are PUBLIC: mm_font_data.hpp and mm_renderer.hpp gate their
# declarations and their bake calls on these, and apps include both.
foreach(_font_set TH SYM SYM1)
    if(ENGINE_FONT_${_font_set})
        target_compile_definitions(mmx_engine PUBLIC MM_ENGINE_FONT_${_font_set}=1)
    else()
        target_compile_definitions(mmx_engine PUBLIC MM_ENGINE_FONT_${_font_set}=0)
    endif()
endforeach()

if(ENGINE_THEME_JSON)
    target_compile_definitions(mmx_engine PUBLIC MM_ENGINE_THEME_JSON=1)
else()
    target_compile_definitions(mmx_engine PUBLIC MM_ENGINE_THEME_JSON=0)
endif()

if(ENGINE_IMAGE_PNG_ONLY)
    target_compile_definitions(mmx_engine PUBLIC
        STBI_NO_JPEG STBI_NO_BMP STBI_NO_GIF
        STBI_NO_PSD  STBI_NO_PIC STBI_NO_HDR STBI_NO_PNM)
endif()

# Android only. android.app.NativeActivity dlopen()s each of our shared
# libraries and dlsym()s exactly one name; a default-visibility build was
# publishing 2849 of them — 368 KB of .dynstr and 130 KB of .dynsym for code
# no other process can call, and the extra symbols kept their sections alive.
#
# Attached to mmx_engine rather than to the app directories because the shared
# libraries do not all come from one place: the examples live under engine/
# and are added before any app subdirectory, so a directory-level option set
# in the root CMakeLists reached freecell and markmos but missed mm_06/mm_07.
# INTERFACE link options follow the engine everywhere it is actually linked.
if(ANDROID)
    target_link_options(mmx_engine INTERFACE
        "LINKER:--version-script=${CMAKE_SOURCE_DIR}/engine/android/export.map")
endif()

if(ENGINE_AUDIO)
    target_compile_definitions(mmx_engine PUBLIC MM_ENGINE_AUDIO=1)
else()
    target_compile_definitions(mmx_engine PUBLIC MM_ENGINE_AUDIO=0)
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
          size_budget_tests
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
        font_atlas_tests
        arena_tests
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
        bind_cache_tests
        color_tests
        rect_tests
    )
    # audio_tests drives a real ma_engine. With ENGINE_AUDIO=OFF it would run
    # against the no-op stub, where every init() legitimately "fails" and the
    # idempotency contract it exists to cover is never exercised — it would pass
    # while covering nothing. Register it only when there is audio to test.
    if(ENGINE_AUDIO)
        list(APPEND ENGINE_TESTS audio_tests)
    endif()

    foreach(TEST_NAME ${ENGINE_TESTS})
        add_executable(${TEST_NAME} tests/${TEST_NAME}.cpp)
        target_link_libraries(${TEST_NAME} PRIVATE mmx_engine)
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
message(STATUS "Examples:    ${BUILD_EXAMPLES}")
