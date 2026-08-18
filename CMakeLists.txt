cmake_minimum_required(VERSION 3.20)
project(ControlHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MinHook (vendored). Built as its own target so cameraunlock_hooks can link
# against it; the objects end up statically linked into the .asi.
add_library(minhook STATIC
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/pch.cpp
    src/dllmain.cpp
    src/core/mod.cpp
    src/core/config.cpp
    src/core/hotkeys.cpp
    src/core/path_utils.cpp
    src/hooks/engine_camera_hook.cpp
    src/hooks/build_profile.cpp
    src/hooks/scripted_camera.cpp
    src/hooks/reticle_projection.cpp
    src/hooks/coherent_reticle.cpp
    src/hooks/fov_override.cpp
)

add_library(ControlHeadTracking SHARED ${MOD_SOURCES})

target_precompile_headers(ControlHeadTracking PRIVATE src/pch.h)

set_target_properties(ControlHeadTracking PROPERTIES
    OUTPUT_NAME "ControlHeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(ControlHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
)

target_include_directories(ControlHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(ControlHeadTracking PRIVATE
        ws2_32
        Psapi
    )
endif()

# Headless unit tests for the pure injection math (no game/Windows deps).
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_executable(test_camera_injection tests/test_camera_injection.cpp)
    set_target_properties(test_camera_injection PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(test_camera_injection PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include)
    add_test(NAME camera_injection COMMAND test_camera_injection)

    # The scripted-shot suppression ramp. Pure state machine over camera
    # ownership, so it runs headless alongside the injection math.
    add_executable(test_tracking_ramp tests/test_tracking_ramp.cpp)
    set_target_properties(test_tracking_ramp PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(test_tracking_ramp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME tracking_ramp COMMAND test_tracking_ramp)

    # The Coherent View slot table. A pure container over plain values (the
    # caller passes the tick), so it runs headless like the rest.
    add_executable(test_view_slot_table tests/test_view_slot_table.cpp)
    set_target_properties(test_view_slot_table PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(test_view_slot_table PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME view_slot_table COMMAND test_view_slot_table)

    # HeadTracking.ini parsing: the file-system boundary. Writes real INI files
    # to the temp directory and reads them back through the game's own reader.
    add_executable(test_config_validation
        tests/test_config_validation.cpp
        src/core/config.cpp)
    set_target_properties(test_config_validation PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(test_config_validation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_config_validation PRIVATE cameraunlock)
    add_test(NAME config_validation COMMAND test_config_validation)

    # Exercises the real socket path: holds the port, starts the receiver,
    # frees the port, and times how long the supervisor takes to reclaim it.
    add_executable(test_udp_port_reclaim tests/test_udp_port_reclaim.cpp)
    set_target_properties(test_udp_port_reclaim PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_link_libraries(test_udp_port_reclaim PRIVATE cameraunlock)
    add_test(NAME udp_port_reclaim COMMAND test_udp_port_reclaim)
endif()
