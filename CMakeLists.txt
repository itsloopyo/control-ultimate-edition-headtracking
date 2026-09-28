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
    src/legacy_config/legacy_config.cpp
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

    # The frozen HeadTracking.ini reader: the file-system boundary. Writes real
    # INI files to the temp directory and reads them back through it.
    add_executable(test_config_validation
        tests/config_differential/legacy_reader_tests.cpp
        src/legacy_config/legacy_config.cpp)
    set_target_properties(test_config_validation PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(test_config_validation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_config_validation PRIVATE cameraunlock)
    add_test(NAME config_validation COMMAND test_config_validation)

    # CameraUnlock.ini: the committed file, a first launch, Defaults.ini and the
    # toggles' saves. Also the renderer behind `pixi run render-config`
    # (--render-config <path>).
    add_executable(test_config
        tests/config_tests.cpp
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp)
    set_target_properties(test_config PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(test_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(test_config PRIVATE CONTROL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(test_config PRIVATE cameraunlock)
    add_test(NAME config COMMAND test_config)

    # Exercises the real socket path: holds the port, starts the receiver,
    # frees the port, and times how long the supervisor takes to reclaim it.
    add_executable(test_udp_port_reclaim tests/test_udp_port_reclaim.cpp)
    set_target_properties(test_udp_port_reclaim PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON)
    target_link_libraries(test_udp_port_reclaim PRIVATE cameraunlock)
    add_test(NAME udp_port_reclaim COMMAND test_udp_port_reclaim)

    # The canonical config conversion's differential test: the dev
    # pre-release's reader (byte copies in tests/config_differential/oracle/src)
    # and startup code against the frozen reader in src/legacy_config/, and that
    # reader against the config owner's migration into CameraUnlock.ini. The
    # oracle is its own library so its pch.h and constants.h never meet the
    # mod's.
    set(CONTROL_DIFFERENTIAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/config_differential)
    add_library(control_config_oracle STATIC ${CONTROL_DIFFERENTIAL_DIR}/oracle/oracle_reader.cpp)
    set_target_properties(control_config_oracle PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(control_config_oracle PRIVATE ${CONTROL_DIFFERENTIAL_DIR}/oracle/src)
    target_link_libraries(control_config_oracle PRIVATE cameraunlock)

    add_executable(control_config_differential
        ${CONTROL_DIFFERENTIAL_DIR}/differential_tests.cpp
        src/core/config.cpp
        src/legacy_config/legacy_config.cpp)
    set_target_properties(control_config_differential PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(control_config_differential PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CONTROL_DIFFERENTIAL_DIR})
    target_compile_definitions(control_config_differential PRIVATE
        CONTROL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(control_config_differential PRIVATE control_config_oracle cameraunlock)
    add_test(NAME config_differential COMMAND control_config_differential)

    # Core's canonical config lint over the committed file and every file the
    # differential test migrated, which it writes beside itself under migrated\.
    add_test(NAME config_lint COMMAND node
        ${CONTROL_DIFFERENTIAL_DIR}/lint-migrated.mjs
        $<TARGET_FILE_DIR:control_config_differential>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)
    set_tests_properties(config_differential config_lint PROPERTIES LABELS differential)

    # What the differential test compiles is pinned by hash, line endings aside:
    # the oracle's byte copies must stay the dev build's (git show
    # 2696dd1:src/<file>), the core sources both readers compile must hold the
    # code the dev build was built with (core 3465659), and the frozen reader is
    # never edited.
    function(control_require_hash path expected)
        file(READ "${path}" text)
        string(REPLACE "\r" "" text "${text}")
        string(SHA256 actual "${text}")
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "${path} has changed (SHA-256 of its text ${actual}, pinned "
                                "${expected}). The config differential test compares against "
                                "exactly this file; see CMakeLists.txt.")
        endif()
    endfunction()
    set(CONTROL_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp)
    foreach(pinned
            "${CONTROL_DIFFERENTIAL_DIR}/oracle/src/core/config.cpp|0ff027e545449241b968d216145d24776c1b737139b1b7e5fa7abada8cc67a9c"
            "${CONTROL_DIFFERENTIAL_DIR}/oracle/src/core/config.h|e35ce7f726f4f9361398f34c2a5df70ff5748dc2b9acefffb578110ea804944e"
            "${CONTROL_DIFFERENTIAL_DIR}/oracle/src/core/logging.h|5019755c9aa7abc09621f4f16c97abaafff0c6248dab7838d75492cc16447bc6"
            "${CONTROL_DIFFERENTIAL_DIR}/oracle/src/core/constants.h|b819d0fec476e18b23e720c5173a8c37773d1b7d3506fe5665207a1769a7c6d2"
            "${CONTROL_DIFFERENTIAL_DIR}/oracle/src/pch.h|fef0bfb1cf12f6b2fbd1b7b9448cf8d57e3c9cf9e8360c832b3942f929eb5046"
            "${CONTROL_CORE_DIR}/include/cameraunlock/config/ini_reader.h|a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"
            "${CONTROL_CORE_DIR}/src/config/ini_reader.cpp|e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"
            "${CONTROL_CORE_DIR}/include/cameraunlock/logging/file_log.h|43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"
            "${CONTROL_CORE_DIR}/src/logging/file_log.cpp|73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.h|cecb2592f111031c5b9e4a648e84370d52ff2661d6ae13ffd617ac811d09d8f3"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/legacy_config/legacy_config.cpp|3e71f63f2729f88f20f9f0c9c2c035d0e9cd94e4c168d569275d1ba9eae415ee")
        string(REPLACE "|" ";" pair "${pinned}")
        list(GET pair 0 pinned_path)
        list(GET pair 1 pinned_hash)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${pinned_path}")
        control_require_hash("${pinned_path}" "${pinned_hash}")
    endforeach()
endif()
