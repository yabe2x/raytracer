cmake_minimum_required(VERSION 3.20)
project(raytracer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(raytracer
    src/main.cpp
    src/scene.cpp
    src/scenes.cpp
    src/gpu_renderer.cpp
    src/demo_reel.cpp
    src/window_win32.cpp
)

target_compile_definitions(raytracer PRIVATE MODELS_DIR="${CMAKE_SOURCE_DIR}/models")

if(MINGW)
    target_compile_options(raytracer PRIVATE -Wall -Wextra)
    target_link_options(raytracer PRIVATE -static)
endif()

target_link_libraries(raytracer PRIVATE opengl32 gdi32 user32)
