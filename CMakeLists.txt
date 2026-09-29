cmake_minimum_required(VERSION 3.24)
project(F2MigrationTool LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(FetchContent)

# --- SDL2 (вендорен в third_party, из FORT/3rdparty) --------------------
set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2")
add_library(SDL2::SDL2 SHARED IMPORTED)
set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/x64/SDL2.dll"
    IMPORTED_IMPLIB "${SDL2_DIR}/lib/x64/SDL2.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_DIR}/include")
add_library(SDL2::SDL2main STATIC IMPORTED)
set_target_properties(SDL2::SDL2main PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/x64/SDL2main.lib")

# --- nlohmann/json ------------------------------------------------------
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# --- Dear ImGui (no upstream CMakeLists: populate only) -----------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5-docking
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR "cmake-does-not-exist")
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# --- Core library -------------------------------------------------------
add_library(f2mt_core STATIC
    src/core/Model.cpp
    src/core/FomapReader.cpp
    src/source/ByteReader.cpp
    src/source/ProtoReader.cpp
    src/source/MapReader.cpp
    src/source/DatProvisioner.cpp
    src/source/LstReader.cpp
    src/source/MsgReader.cpp
    src/project/MigrationProject.cpp)
target_include_directories(f2mt_core PUBLIC src)
target_link_libraries(f2mt_core PUBLIC nlohmann_json::nlohmann_json)

# --- Viewer application -------------------------------------------------
add_executable(f2mt_viewer WIN32
    src/main.cpp
    src/ui/App.cpp
    src/ui/MapRender.cpp
    src/render/Graphics.cpp)
target_link_libraries(f2mt_viewer PRIVATE f2mt_core imgui SDL2::SDL2main SDL2::SDL2)

# Копируем SDL2.dll рядом с исполняемым файлом.
add_custom_command(TARGET f2mt_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_DIR}/lib/x64/SDL2.dll"
        "$<TARGET_FILE_DIR:f2mt_viewer>/SDL2.dll")

if(MSVC)
    target_compile_options(f2mt_core PRIVATE /W4 /utf-8)
    target_compile_options(f2mt_viewer PRIVATE /W4 /utf-8)
endif()
