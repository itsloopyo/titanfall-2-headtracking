cmake_minimum_required(VERSION 3.20)
project(Titanfall2HeadTracking VERSION 0.0.0 LANGUAGES CXX)

# C++20 for designated initialisers. The build profile table is a dozen-plus
# uint32_t fields written out positionally per game build, where transposing two
# is invisible to the compiler and presents as a feature that silently does
# nothing - which is exactly what happened when the view-angles slot and the
# pause flag were swapped. Naming the fields at the initialiser makes that
# impossible. The shared core is C++17 and compiles unchanged under C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Titanfall 2 is 64-bit. Configure with -A x64.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MinHook (vendored in cameraunlock-core/vendor/minhook when present).
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/cameraunlock-core/vendor/minhook")
if(EXISTS "${MINHOOK_DIR}/CMakeLists.txt")
    add_subdirectory(${MINHOOK_DIR} minhook)
else()
    include(FetchContent)
    # Commit SHA, not the v1.3.4 tag it points at. A git tag is mutable: whoever
    # controls the upstream repository can move it to different code at any time,
    # and every build from then on - including the release build CI publishes -
    # would fetch that instead, silently, into a DLL that runs inside the game
    # process. The SHA cannot be repointed. Same reasoning the workflows pin
    # their actions by SHA; the tag stays in the comment so the version is still
    # readable at a glance.
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537  # v1.3.4
    )
    FetchContent_MakeAvailable(minhook)
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

add_library(Titanfall2HeadTracking SHARED
    src/pch.cpp
    src/dllmain.cpp
    src/config.cpp
    src/plugin.cpp
    src/hotkey_handler.cpp
    src/camera_hook.cpp
    src/view_angles_hook.cpp
    src/cockpit_hook.cpp
    src/fov_control.cpp
    src/crosshair_hook.cpp
    src/hit_indicator.cpp
    src/world_marker_hook.cpp
    src/ads_state.cpp
    src/ads_marker.cpp
    src/aim_trace.cpp
    src/build_profile.cpp
    src/game_state.cpp
)

target_precompile_headers(Titanfall2HeadTracking PRIVATE src/pch.h)

set_target_properties(Titanfall2HeadTracking PROPERTIES
    OUTPUT_NAME "Titanfall2HeadTracking"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_include_directories(Titanfall2HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Titanfall2HeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    minhook
    ws2_32
)

# Characterization tests for the header-only maths every rendered frame passes
# through. Deliberately a normal target rather than an opt-in one: `pixi run
# build` builds it, so a change that breaks the arithmetic cannot reach a
# release without someone seeing it. It links nothing - no game, no engine, no
# hooks - so it runs anywhere the mod compiles. `pixi run test` runs it.
add_executable(Titanfall2HeadTrackingTests tests/test_main.cpp)

target_include_directories(Titanfall2HeadTrackingTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # The shared ADS module is header-only, so the tests reach it by include path
    # rather than by linking the core library - this target still links nothing.
    ${CMAKE_CURRENT_SOURCE_DIR}/cameraunlock-core/cpp/include
)

set_target_properties(Titanfall2HeadTrackingTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
