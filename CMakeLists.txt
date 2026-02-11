cmake_minimum_required(VERSION 3.15)
project(AdvancedPaste LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(AdvancedPaste WIN32
    src/main.cpp
    src/overlay.cpp
    src/utils.cpp
)

target_link_libraries(AdvancedPaste PRIVATE
    gdiplus
    dwmapi
    shell32
    ole32
    oleaut32
    msimg32
    shlwapi
)

target_compile_definitions(AdvancedPaste PRIVATE
    UNICODE _UNICODE NOMINMAX
)

if(MSVC)
    target_compile_options(AdvancedPaste PRIVATE /utf-8)
endif()
