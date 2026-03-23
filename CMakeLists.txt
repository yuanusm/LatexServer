cmake_minimum_required(VERSION 3.16)
project(LatexServerNoPreview VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)

FetchContent_Declare(
    imguicolortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG master
)

FetchContent_MakeAvailable(imgui imguicolortextedit)

add_library(imgui_lib
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC OpenGL::GL glfw)

add_library(texteditor_lib
    src/TextEditor.cpp
)

target_include_directories(texteditor_lib PUBLIC
    ${imguicolortextedit_SOURCE_DIR}
)

target_link_libraries(texteditor_lib PUBLIC imgui_lib)

add_executable(latex_server
    src/main.cpp
    src/compiler.cpp
    src/editor.cpp
    src/file_tree.cpp
    src/ui.cpp
)

target_include_directories(latex_server PRIVATE src)
target_link_libraries(latex_server PRIVATE OpenGL::GL glfw imgui_lib texteditor_lib)

target_compile_options(latex_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
