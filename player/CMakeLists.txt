# Mirage-Player: runs a scene. No RUI, no editor code.
#
#   cmake -S player -B build-player -G "MinGW Makefiles"
#   cmake --build build-player
#
# Everything under src/ is compiled EXCEPT src/mirage_editor/ (editor-only).
#   src/mirage_core/     shared with the editor
#   src/mirage_player/   player-only (put player_main.cpp here)
#   src/<anything else>  shared with the editor, so keep main()s and
#                        editor/renderer code inside src/mirage_editor/

cmake_minimum_required(VERSION 3.20)
project(Mirage-Player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# This file lives in <root>/player/, the sources live in <root>/.
get_filename_component(MIRAGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

include(${MIRAGE_ROOT}/cmake/MirageDeps.cmake)

# ---- Sources: all of src/ minus the editor-only folder ----
file(GLOB_RECURSE PLAYER_SRC CONFIGURE_DEPENDS ${MIRAGE_ROOT}/src/*.cpp)
list(FILTER PLAYER_SRC EXCLUDE REGEX ".*/src/mirage_editor/.*")

if(NOT PLAYER_SRC)
    message(FATAL_ERROR "No player sources found under src/")
endif()

add_executable(${PROJECT_NAME} ${PLAYER_SRC})

# Headers are found as "mirage_core/..." and "mirage_player/..." from include/.
# include/mirage_editor/ is never included by player code.
target_include_directories(${PROJECT_NAME} PRIVATE ${MIRAGE_ROOT}/include)

# raylib's external/ folder, for glad.h (raw OpenGL calls used by render_graph)
set(RAYLIB_EXTERNAL_DIR "${RAYLIB_INCLUDE_DIR}/external")

if(NOT EXISTS "${RAYLIB_EXTERNAL_DIR}/glad.h")
    message(FATAL_ERROR "glad.h was not found at:\n  ${RAYLIB_EXTERNAL_DIR}")
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${RAYLIB_EXTERNAL_DIR})

target_compile_options(${PROJECT_NAME} PRIVATE
    -w
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g1>
    $<$<CONFIG:Debug>:-fno-var-tracking>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-DNDEBUG>
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    lua
    gdi32 opengl32 shell32 winmm ws2_32
)

# No -mwindows on purpose: keep the console visible while you debug the player.
if(WIN32)
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-static-libgcc>
        $<$<CONFIG:Release>:-static-libstdc++>
    )
endif()

# Run from the project root so relative paths (scripts/, font/) resolve.
add_custom_target(run-player
    COMMAND $<TARGET_FILE:${PROJECT_NAME}> --scene scenes/test.json
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${MIRAGE_ROOT}
    USES_TERMINAL
)

message(STATUS "Player sources: ${PLAYER_SRC}")
