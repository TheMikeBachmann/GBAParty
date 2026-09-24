cmake_minimum_required(VERSION 3.20)
project(GBAParty VERSION 0.0.1 LANGUAGES C CXX)
set(LIBMGBA_ONLY ON)
add_subdirectory(third_party/mgba)
find_package(SDL2 REQUIRED CONFIG)
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_sdl2.cpp
    third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2)
add_executable(GBAParty GBAParty.cpp)
target_compile_features(GBAParty PRIVATE cxx_std_20)
target_link_libraries(GBAParty PUBLIC mgba imgui SDL2::SDL2)
target_compile_definitions(GBAParty PRIVATE ENABLE_VFS ENABLE_DIRECTORIES)
target_link_options(GBAParty PUBLIC -static-libstdc++ -static-libgcc)
