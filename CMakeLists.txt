cmake_minimum_required(VERSION 3.20)
project(LocalLatexEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LATEX_EDITOR_FETCH_IMGUI "Fetch Dear ImGui if not available locally" ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)

set(IMGUI_DIR "" CACHE PATH "Path to Dear ImGui sources")

if(IMGUI_DIR)
  add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)
elseif(LATEX_EDITOR_FETCH_IMGUI)
  include(FetchContent)
  FetchContent_Declare(
    imgui_src
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8)
  FetchContent_MakeAvailable(imgui_src)
  add_library(imgui STATIC
    ${imgui_src_SOURCE_DIR}/imgui.cpp
    ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui PUBLIC ${imgui_src_SOURCE_DIR} ${imgui_src_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw)
else()
  message(FATAL_ERROR "Set IMGUI_DIR or enable LATEX_EDITOR_FETCH_IMGUI")
endif()

add_executable(local_latex_editor
  main.cpp
  editor.cpp
  pdf_renderer.cpp
  file_watcher.cpp)

target_include_directories(local_latex_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(local_latex_editor PRIVATE imgui glfw OpenGL::GL PkgConfig::MUPDF)

if(MINGW)
  target_link_libraries(local_latex_editor PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(WIN32)
  target_link_libraries(local_latex_editor PRIVATE gdi32 imm32)
endif()
