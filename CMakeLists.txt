cmake_minimum_required(VERSION 3.20)

project(Mirage LANGUAGES CXX C)
# ============================================================
# Language standards
# ============================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---- Build speed: ccache (if installed) caches object files across clean rebuilds.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache:             ${CCACHE_PROGRAM}")
endif()

# lld links a big executable much faster than ld. Needs ld.lld on PATH, so opt in:
#   cmake -DMIRAGE_USE_LLD=ON ...
option(MIRAGE_USE_LLD "Link with lld" OFF)

set(EXTERNAL_DIR
    ${CMAKE_SOURCE_DIR}/external
)

# ============================================================
# Dependencies
# ============================================================

# ------------------------------------------------------------
# box3d
# ------------------------------------------------------------

find_package(
    box3d
    0.1
    REQUIRED
    PATHS
        "C:/box3d"
)

# ------------------------------------------------------------
# box2d
# ------------------------------------------------------------

find_package(
    box2d
    3.2
    REQUIRED
    PATHS
        "C:/box2d"
)
# ------------------------------------------------------------
# Prebuilt MinGW raylib
#
# Headers:
#   C:/raylib/raylib/src
#
# Library:
#   C:/raylib/raylib/src/libraylib.a
#
# RUI::Rui has a PUBLIC dependency on the CMake target
# "raylib", so the target must exist before find_package(Rui).
# ------------------------------------------------------------

# raylib + Lua imported targets (shared with the player build)
include(${CMAKE_SOURCE_DIR}/cmake/MirageDeps.cmake)

# ------------------------------------------------------------
# RUI
# ------------------------------------------------------------

find_package(
    Rui
    CONFIG
    REQUIRED
    PATHS
        "C:/Libraries/rui"
)

# ============================================================
# Source discovery
# ============================================================
#
#   src/mirage_core/     shared by editor and player
#   src/mirage_editor/   editor only
#   src/mirage_player/   player only
#   anything else in src/ is treated as shared (core)
#
# The shared code is compiled ONCE into an object library and linked into both
# executables. (An OBJECT library, not STATIC, so static initialisers such as
# reflection registrations are never dropped by the linker.)

file(GLOB_RECURSE SRC_CPP CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE SRC_C   CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.c)
set(SRC_ALL ${SRC_CPP} ${SRC_C})

set(SRC_EDITOR ${SRC_ALL})
list(FILTER SRC_EDITOR INCLUDE REGEX ".*/src/mirage_editor/.*")

set(SRC_PLAYER ${SRC_ALL})
list(FILTER SRC_PLAYER INCLUDE REGEX ".*/src/mirage_player/.*")

set(SRC_CORE ${SRC_ALL})
list(FILTER SRC_CORE EXCLUDE REGEX ".*/src/mirage_(editor|player)/.*")

# glad.h (raw OpenGL calls) lives in raylib's external/ folder
set(RAYLIB_EXTERNAL_DIR "${RAYLIB_INCLUDE_DIR}/external")
if(NOT EXISTS "${RAYLIB_EXTERNAL_DIR}/glad.h")
    message(FATAL_ERROR
        "glad.h was not found at:\n"
        "  ${RAYLIB_EXTERNAL_DIR}/glad.h"
    )
endif()

# ============================================================
# Targets
# ============================================================

add_library(mirage_core OBJECT ${SRC_CORE})
add_executable(${PROJECT_NAME} ${SRC_EDITOR})
add_executable(${PROJECT_NAME}-Player ${SRC_PLAYER})

# Player goes where the editor's Play button looks for it.
set_target_properties(${PROJECT_NAME}-Player PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build-player")

# System libs must come AFTER raylib/Rui on the link line (static MinGW libs
# resolve left to right), so they are listed after them, as part of mirage_core.
set(WIN_LIBS gdi32 ole32 comdlg32 opengl32 shell32 winmm ws2_32)

# What the shared code needs (raylib, lua, box2d + their include paths).
target_link_libraries(mirage_core PUBLIC raylib lua box2d::box2d ${WIN_LIBS})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        mirage_core
        Rui::Rui
        box3d::box3d
)
target_link_libraries(${PROJECT_NAME}-Player
    PRIVATE
        mirage_core
)

foreach(t mirage_core ${PROJECT_NAME} ${PROJECT_NAME}-Player)

    target_include_directories(${t} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_include_directories(${t} SYSTEM PRIVATE
        ${EXTERNAL_DIR}/imgui
        ${EXTERNAL_DIR}/nlohmann
        ${RAYLIB_EXTERNAL_DIR}
    )

    target_compile_options(${t} PRIVATE
        -w
        $<$<CONFIG:Debug>:-Og>
        $<$<CONFIG:Debug>:-g1>
        $<$<CONFIG:Debug>:-fno-var-tracking>
        $<$<AND:$<COMPILE_LANGUAGE:C>,$<CONFIG:Debug>>:-fno-var-tracking-assignments>
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-DNDEBUG>
    )

    # Precompiled headers: parse the heavy third-party headers once per target
    # instead of once per .cpp. Deliberately NOT raylib.h (it clashes with
    # windows.h in files that include both). C files are left alone.
    target_precompile_headers(${t} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:<sol/sol.hpp$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<entt/entt.hpp$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<nlohmann/json.hpp$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<map$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<functional$<ANGLE-R>>"
        "$<$<COMPILE_LANGUAGE:CXX>:<memory$<ANGLE-R>>"
    )
endforeach()

if(MIRAGE_USE_LLD)
    target_link_options(${PROJECT_NAME}        PRIVATE -fuse-ld=lld)
    target_link_options(${PROJECT_NAME}-Player PRIVATE -fuse-ld=lld)
endif()

# ============================================================
# Windows / MinGW
# ============================================================

if(WIN32)

    target_link_options(
        ${PROJECT_NAME}
        PRIVATE

            $<$<CONFIG:Release>:-static-libgcc>
            $<$<CONFIG:Release>:-static-libstdc++>
            $<$<CONFIG:Release>:-mwindows>
    )

    # No -mwindows on the player: keep the console visible while you debug it.
    target_link_options(
        ${PROJECT_NAME}-Player
        PRIVATE
            $<$<CONFIG:Release>:-static-libgcc>
            $<$<CONFIG:Release>:-static-libstdc++>
    )

endif()

# ============================================================
# Distribution package
# ============================================================

set(
    RUNTIME_DIRS
    Ui
    shaders
    palletes
)

set(
    DIST_DIR
    ${CMAKE_BINARY_DIR}/dist/${PROJECT_NAME}
)

add_custom_target(
    dist

    COMMAND
        ${CMAKE_COMMAND}
        -E make_directory
        ${DIST_DIR}

    COMMAND
        ${CMAKE_COMMAND}
        -E copy
        $<TARGET_FILE:${PROJECT_NAME}>
        ${DIST_DIR}/

    COMMENT
        "Creating portable release package in ${DIST_DIR}"

    VERBATIM
)

add_dependencies(
    dist
    ${PROJECT_NAME}
)

foreach(dir ${RUNTIME_DIRS})

    if(EXISTS "${CMAKE_SOURCE_DIR}/${dir}")

        add_custom_command(
            TARGET
                dist

            POST_BUILD

            COMMAND
                ${CMAKE_COMMAND}
                -E copy_directory
                "${CMAKE_SOURCE_DIR}/${dir}"
                "${DIST_DIR}/${dir}"

            COMMENT
                "Copying ${dir}"
        )

    endif()

endforeach()

# ============================================================
# Run targets
# ============================================================

add_custom_target(
    run

    COMMAND
        $<TARGET_FILE:${PROJECT_NAME}>

    DEPENDS
        ${PROJECT_NAME}

    WORKING_DIRECTORY
        ${CMAKE_SOURCE_DIR}

    USES_TERMINAL
)

add_custom_target(
    run-dist

    COMMAND
        "${DIST_DIR}/${PROJECT_NAME}.exe"

    DEPENDS
        dist

    WORKING_DIRECTORY
        ${DIST_DIR}

    USES_TERMINAL
)

# ============================================================
# Configuration summary
# ============================================================

message(STATUS "")
message(STATUS "================================================")
message(STATUS "Procedural Game")
message(STATUS "================================================")
message(STATUS "Compiler:           ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C Compiler:         ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler:       ${CMAKE_CXX_COMPILER}")
message(STATUS "Raylib include:     ${RAYLIB_INCLUDE_DIR}")
message(STATUS "Raylib library:     ${RAYLIB_LIBRARY}")
message(STATUS "RUI:                C:/Libraries/rui")
message(STATUS "box3d:              C:/box3d")
message(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "================================================")
message(STATUS "")