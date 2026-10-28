# Shared by the editor and the player: prebuilt MinGW raylib + Lua.
# Sets RAYLIB_INCLUDE_DIR / LUA_INCLUDE_DIR and creates the imported
# targets `raylib` and `lua` (once, even if included twice).

set(RAYLIB_DIR         "C:/raylib/raylib")
set(RAYLIB_INCLUDE_DIR "${RAYLIB_DIR}/src")
set(RAYLIB_LIBRARY     "${RAYLIB_DIR}/src/libraylib.a")

set(LUA_DIR            "C:/lua-5.4.8")
set(LUA_INCLUDE_DIR    "${LUA_DIR}/src")
set(LUA_LIBRARY        "${LUA_DIR}/src/liblua.a")

foreach(f "${RAYLIB_INCLUDE_DIR}/raylib.h" "${RAYLIB_LIBRARY}"
          "${LUA_INCLUDE_DIR}/lua.h"       "${LUA_LIBRARY}")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "Required file not found:\n  ${f}")
    endif()
endforeach()

if(NOT TARGET raylib)
    add_library(raylib STATIC IMPORTED GLOBAL)
    set_target_properties(raylib PROPERTIES
        IMPORTED_LOCATION             "${RAYLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_INCLUDE_DIR}")
endif()

if(NOT TARGET lua)
    add_library(lua STATIC IMPORTED GLOBAL)
    set_target_properties(lua PROPERTIES
        IMPORTED_LOCATION             "${LUA_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LUA_INCLUDE_DIR}")
endif()
