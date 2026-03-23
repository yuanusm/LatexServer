cmake_minimum_required(VERSION 3.16)
project(LatexServerNoPreview VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

add_executable(latex_server
    src/main.cpp
)

target_link_libraries(latex_server PRIVATE OpenGL::GL glfw)

target_compile_options(latex_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
