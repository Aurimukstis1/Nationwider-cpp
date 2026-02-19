cmake_minimum_required(VERSION 3.23)
project(nationwider)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(sdl3 STATIC IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/SDL3-3.4.0/lib/x64/SDL3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/SDL3-3.4.0/include"
)

add_library(sdl3_image STATIC IMPORTED)
set_target_properties(sdl3_image PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/SDL3_image-3.4.0/lib/x64/SDL3_image.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/SDL3_image-3.4.0/include"
)

set(IMGUI_SRC
        imgui/imgui.cpp
        imgui/imgui.h
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_impl_sdl3.cpp
        imgui/imgui_impl_sdl3.h
        imgui/imgui_impl_sdlrenderer3.cpp
        imgui/imgui_impl_sdlrenderer3.h
        imgui/imgui_internal.h
        imgui/imgui_stdlib.cpp
        imgui/imgui_stdlib.h
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/main.cpp
        )

add_executable(Nationwider ${IMGUI_SRC})

target_include_directories(Nationwider PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

target_link_libraries(Nationwider PRIVATE sdl3 sdl3_image)
target_sources(Nationwider
  PRIVATE
    imgui/main.cpp
)
