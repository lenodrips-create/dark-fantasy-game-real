cmake_minimum_required(VERSION 3.16)
project(dark_fantasy C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(EMSCRIPTEN)
    # Browser build (emcmake cmake ...): raylib's web platform on WebGL 2
    set(PLATFORM Web CACHE STRING "" FORCE)
    set(GRAPHICS GRAPHICS_API_OPENGL_ES3)
else()
    # raylib: use an installed copy if there is one, otherwise download and build 5.5
    find_package(raylib 5.5 QUIET)
endif()
if(NOT raylib_FOUND)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    )
    FetchContent_MakeAvailable(raylib)
    # The sky and some Blender textures are JPGs, which raylib leaves out by default
    target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1)
endif()

add_executable(dark_fantasy
    src/knight.c
    src/main.c
    src/menu.c
    src/player.c
    src/wanderer.c
    src/world.c
)
target_link_libraries(dark_fantasy PRIVATE raylib)

if(MSVC)
    target_compile_options(dark_fantasy PRIVATE /W3)
else()
    target_compile_options(dark_fantasy PRIVATE -Wall -Wextra)
    target_link_libraries(dark_fantasy PRIVATE m)
endif()

if(APPLE)
    target_link_libraries(dark_fantasy PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
endif()

if(EMSCRIPTEN)
    # Writes web/dark_fantasy.js and web/dark_fantasy.wasm, which index.html loads.
    # index.html downloads levels/ and characters/ itself, so swapping those files needs no rebuild.
    set_target_properties(dark_fantasy PROPERTIES
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/web
    )
    target_link_options(dark_fantasy PRIVATE
        -sUSE_GLFW=3
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=1048576
        -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=FS,addRunDependency,removeRunDependency
    )
else()
    # The game loads levels/, characters/ and menu/ relative to itself, so keep copies next to the executable
    add_custom_command(TARGET dark_fantasy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/levels
            $<TARGET_FILE_DIR:dark_fantasy>/levels
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/characters
            $<TARGET_FILE_DIR:dark_fantasy>/characters
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/menu
            $<TARGET_FILE_DIR:dark_fantasy>/menu
    )
endif()
