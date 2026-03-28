cmake_minimum_required(VERSION 3.16)
project(LatexServerNoPreview VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

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

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(imgui imguicolortextedit asio nlohmann_json)

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

add_library(collab_lib
    src/log.cpp
    src/network/client.cpp
    src/network/server.cpp
)

target_include_directories(collab_lib PUBLIC
    src
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(collab_lib PUBLIC ASIO_STANDALONE)

target_link_libraries(collab_lib PUBLIC nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(collab_lib PUBLIC ws2_32 mswsock)
endif()

add_executable(latex_server
    src/main.cpp
    src/compiler.cpp
    src/editor.cpp
    src/file_tree.cpp
    src/ui.cpp
    src/core/editor_sync.cpp
    src/core/file_sync.cpp
)

target_include_directories(latex_server PRIVATE src)
target_link_libraries(latex_server PRIVATE OpenGL::GL glfw imgui_lib texteditor_lib collab_lib)

add_executable(latex_collab_server
    src/network/server_main.cpp
    src/compiler.cpp
)

target_include_directories(latex_collab_server PRIVATE src)
target_link_libraries(latex_collab_server PRIVATE collab_lib)

target_compile_options(latex_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

target_compile_options(latex_collab_server PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
