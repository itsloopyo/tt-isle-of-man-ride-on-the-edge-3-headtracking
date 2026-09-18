cmake_minimum_required(VERSION 3.20)
project(TT3HeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/permissive- /Zc:__cplusplus /MP /W4)
endif()

# MinHook is vendored rather than fetched: `pixi run package` has to build from
# a clean checkout with no network, and the source compiled into the binary
# users install should be the source in this repo.
add_library(minhook STATIC
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include)
if(MSVC)
    target_compile_options(minhook PRIVATE /W0)
endif()

# HOOKS carries hooks/hook_manager.cpp, the MinHook wrapper the camera hook
# places its detour through. DISCOVERY stays off: this mod reaches the camera
# through the RVA its build profile pins, not by RTTI vtable resolution.
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_DISCOVERY OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

set(MOD_SOURCES
    src/dllmain.cpp
    src/headtracking_mod.cpp
    src/config.cpp
    src/camera_hook.cpp
    src/camera_transform.cpp
    src/game_state.cpp
    src/hotkey_names.cpp
    src/hotkeys.cpp
    src/render_diagnostics.cpp
    src/builds/build_registry.cpp
    src/builds/steam_offsets.cpp
)

add_library(TT3HeadTracking SHARED ${MOD_SOURCES})

# The same /WX the tests carry. The three files that appear in no test target -
# dllmain, headtracking_mod and camera_hook - are the ones doing reinterpret_cast
# pointer arithmetic and raw memcpy on the render path, which is where a
# truncation or sign-conversion warning is worth stopping the build for. The
# tree builds warning-free today, so this costs nothing to turn on.
if(MSVC)
    target_compile_options(TT3HeadTracking PRIVATE /WX)
endif()

target_include_directories(TT3HeadTracking PRIVATE
    src
    cameraunlock-core/cpp/include
)

target_link_libraries(TT3HeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
    psapi
)

# Emit a linker map so a crash offset (asi+0xNNNN) resolves to a symbol.
if(MSVC)
    target_link_options(TT3HeadTracking PRIVATE /MAP /MAPINFO:EXPORTS)
endif()

# Output as .asi for Ultimate ASI Loader.
set_target_properties(TT3HeadTracking PROPERTIES
    SUFFIX ".asi"
    PREFIX ""
)



# Off by default so `pixi run package` never builds a test binary into build/.
# scripts/run-tests.ps1 turns it on in its own build-tests/ tree.
option(TT3_BUILD_TESTS "Build the unit tests" OFF)
if(TT3_BUILD_TESTS)
    enable_testing()

    add_executable(camera_transform_tests
        tests/camera_transform_tests.cpp
        src/camera_transform.cpp
    )
    add_executable(game_state_tests
        tests/game_state_tests.cpp
        src/game_state.cpp
        src/builds/build_registry.cpp
        src/builds/steam_offsets.cpp
    )
    target_link_libraries(game_state_tests PRIVATE cameraunlock)

    foreach(t camera_transform_tests game_state_tests)
        target_include_directories(${t} PRIVATE src tests cameraunlock-core/cpp/include)
        if(MSVC)
            target_compile_options(${t} PRIVATE /WX)
        endif()
        add_test(NAME ${t} COMMAND ${t})
    endforeach()

    add_custom_target(tt3_tests DEPENDS camera_transform_tests game_state_tests)
endif()
