cmake_minimum_required(VERSION 3.31.10)
project(TinyMeEngine VERSION 0.5.0 LANGUAGES CXX)
include(cmake/Dxc.cmake)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "TinyMeEngine currently supports Windows x64 only.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        OR NOT CMAKE_GENERATOR STREQUAL "Visual Studio 17 2022"
        OR NOT CMAKE_VS_PLATFORM_TOOLSET STREQUAL "v143")
    message(FATAL_ERROR "Use the windows-vs2022 preset and MSVC v143.")
endif()
if(NOT CMAKE_GENERATOR_PLATFORM MATCHES "^x64,version=([0-9.]+)$")
    message(FATAL_ERROR "Use the windows-vs2022 preset with an explicitly pinned Windows SDK.")
endif()
set(tinyme_expected_sdk "${CMAKE_MATCH_1}")
if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL tinyme_expected_sdk)
    message(FATAL_ERROR "Selected Windows SDK does not match the platform specification.")
endif()
message(STATUS "TinyMeEngine Windows SDK: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
option(TINYME_BUILD_EDITOR "Build the editor entry point" ON)

function(tinyme_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
endfunction()

configure_file(
    engine/core/include/tinyme/core/Version.h.in
    generated/tinyme/core/Version.h
    @ONLY
)

add_library(tinyme_core STATIC engine/core/src/LaunchOptions.cpp engine/core/src/TimingStatistics.cpp
    engine/core/src/Camera.cpp)
add_library(TinyMe::Core ALIAS tinyme_core)
target_include_directories(tinyme_core PUBLIC
    engine/core/include
    "${CMAKE_BINARY_DIR}/generated"
)
tinyme_configure_target(tinyme_core)

add_library(tinyme_runtime STATIC engine/runtime/src/Session.cpp)
add_library(TinyMe::Runtime ALIAS tinyme_runtime)
target_include_directories(tinyme_runtime PUBLIC engine/runtime/include)
tinyme_configure_target(tinyme_runtime)

add_library(tinyme_windows STATIC engine/platform/windows/src/Window.cpp)
target_include_directories(tinyme_windows PUBLIC engine/platform/windows/include)
target_compile_definitions(tinyme_windows PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(tinyme_windows PRIVATE user32)
tinyme_configure_target(tinyme_windows)

add_library(tinyme_d3d12 STATIC
    engine/render/d3d12/src/ClearRenderer.cpp
    engine/render/d3d12/src/ShaderCompiler.cpp
    engine/render/d3d12/src/GpuTimer.cpp)
target_include_directories(tinyme_d3d12 PUBLIC engine/render/d3d12/include
    PRIVATE "${tinyme_dxc_root}/build/native/include")
target_link_libraries(tinyme_d3d12 PUBLIC tinyme_windows TinyMe::Core PRIVATE d3d12 dxgi dxguid)
tinyme_configure_target(tinyme_d3d12)

add_library(tinyme_windowed_runtime STATIC engine/runtime/src/WindowedSession.cpp engine/runtime/src/ProfileReport.cpp)
target_include_directories(tinyme_windowed_runtime PUBLIC engine/runtime/include)
target_link_libraries(tinyme_windowed_runtime PUBLIC TinyMe::Core PRIVATE tinyme_d3d12 TinyMe::Runtime)
tinyme_configure_target(tinyme_windowed_runtime)

add_library(tinyme_app_host STATIC apps/common/Application.cpp)
target_include_directories(tinyme_app_host PUBLIC apps/common)
target_link_libraries(tinyme_app_host PRIVATE TinyMe::Core TinyMe::Runtime tinyme_windowed_runtime)
tinyme_configure_target(tinyme_app_host)

add_executable(tinyme_runtime_app apps/runtime/Main.cpp)
set_target_properties(tinyme_runtime_app PROPERTIES OUTPUT_NAME tinyme_runtime)
target_link_libraries(tinyme_runtime_app PRIVATE tinyme_app_host)
tinyme_configure_target(tinyme_runtime_app)

if(TINYME_BUILD_EDITOR)
    add_executable(tinyme_editor apps/editor/Main.cpp)
    target_link_libraries(tinyme_editor PRIVATE tinyme_app_host)
    tinyme_configure_target(tinyme_editor)
endif()

add_custom_target(tinyme_shader_asset ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/shaders/Triangle.hlsl"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/Triangle.hlsl" VERBATIM)
function(tinyme_deploy_shader target)
    tinyme_deploy_dxc(${target})
    add_dependencies(${target} tinyme_shader_asset)
endfunction()
tinyme_deploy_shader(tinyme_runtime_app)
if(TINYME_BUILD_EDITOR)
    tinyme_deploy_shader(tinyme_editor)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(tinyme_tests tests/CoreTests.cpp)
    target_link_libraries(tinyme_tests PRIVATE TinyMe::Core TinyMe::Runtime)
    tinyme_configure_target(tinyme_tests)
    add_test(NAME core.contracts COMMAND tinyme_tests)
    set_tests_properties(core.contracts PROPERTIES TIMEOUT 15)

    add_executable(tinyme_camera_tests tests/CameraTests.cpp)
    target_link_libraries(tinyme_camera_tests PRIVATE TinyMe::Core)
    tinyme_configure_target(tinyme_camera_tests)
    add_test(NAME camera.contracts COMMAND tinyme_camera_tests)
    set_tests_properties(camera.contracts PROPERTIES TIMEOUT 15)

    add_executable(tinyme_shader_tests tests/ShaderTests.cpp)
    target_include_directories(tinyme_shader_tests PRIVATE engine/render/d3d12/src)
    target_link_libraries(tinyme_shader_tests PRIVATE tinyme_d3d12)
    tinyme_configure_target(tinyme_shader_tests)
    tinyme_deploy_shader(tinyme_shader_tests)
    add_test(NAME shader.compiler COMMAND tinyme_shader_tests)
    set_tests_properties(shader.compiler PROPERTIES TIMEOUT 30)

    add_executable(tinyme_profile_tests tests/ProfileTests.cpp)
    target_include_directories(tinyme_profile_tests PRIVATE engine/runtime/src)
    target_link_libraries(tinyme_profile_tests PRIVATE tinyme_windowed_runtime tinyme_d3d12)
    tinyme_configure_target(tinyme_profile_tests)
    add_test(NAME profile.contracts COMMAND tinyme_profile_tests)
    set_tests_properties(profile.contracts PROPERTIES TIMEOUT 15)

    add_executable(tinyme_gpu_tests tests/GpuTests.cpp)
    target_link_libraries(tinyme_gpu_tests PRIVATE tinyme_d3d12 tinyme_windowed_runtime gdi32 dwmapi)
    tinyme_configure_target(tinyme_gpu_tests)
    tinyme_deploy_shader(tinyme_gpu_tests)
    option(TINYME_ENABLE_GPU_TESTS "Run interactive RTX 5080 GPU tests in CTest" OFF)
    if(TINYME_ENABLE_GPU_TESTS)
        add_test(NAME gpu.lifecycle COMMAND tinyme_gpu_tests)
        add_test(NAME gpu.lifecycle.gbv COMMAND tinyme_gpu_tests --gpu-validation)
        set_tests_properties(gpu.lifecycle gpu.lifecycle.gbv PROPERTIES
            LABELS "GPU" RUN_SERIAL TRUE TIMEOUT 120)
    endif()

    set(cli_targets tinyme_runtime_app)
    if(TINYME_BUILD_EDITOR)
        list(APPEND cli_targets tinyme_editor)
    endif()
    foreach(app IN LISTS cli_targets)
        foreach(case IN ITEMS default help version headless default_frames unknown missing_frames
                zero_frames negative_frames overflow_frames trailing_frames missing_headless
                duplicate_headless duplicate_frames conflicting_help conflicting_modes
                duplicate_windowed headless_debug headless_gbv duplicate_debug
                duplicate_gbv windowed_zero windowed_unknown headless_experiment
                headless_shader unknown_experiment missing_shader duplicate_shader clear_shader
                headless_profile headless_scale invalid_scale invalid_tint missing_tint
                duplicate_scale clear_parameters samples_without_profile zero_samples
                excessive_warmup insufficient_profile_frames duplicate_profile missing_report
                duplicate_report headless_camera clear_camera invalid_fov missing_camera
                duplicate_camera degenerate_camera)
            set(test_name "${app}.${case}")
            add_test(NAME "${test_name}" COMMAND "${CMAKE_COMMAND}"
                "-DPROGRAM=$<TARGET_FILE:${app}>"
                "-DCASE=${case}"
                "-DEXPECTED_VERSION=${PROJECT_VERSION}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckCli.cmake"
            )
            set_tests_properties("${test_name}" PROPERTIES TIMEOUT 15)
        endforeach()
        if(TINYME_ENABLE_GPU_TESTS)
            add_test(NAME "${app}.windowed" COMMAND "${CMAKE_COMMAND}"
                "-DPROGRAM=$<TARGET_FILE:${app}>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckWindowCli.cmake")
            set_tests_properties("${app}.windowed" PROPERTIES LABELS "GPU" RUN_SERIAL TRUE TIMEOUT 45)
            add_test(NAME "${app}.profile" COMMAND "${CMAKE_COMMAND}"
                "-DPROGRAM=$<TARGET_FILE:${app}>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CheckProfileCli.cmake")
            set_tests_properties("${app}.profile" PROPERTIES LABELS "GPU" RUN_SERIAL TRUE TIMEOUT 90)
        endif()
    endforeach()
endif()
