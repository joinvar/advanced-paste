cmake_minimum_required(VERSION 3.15)
project(AdvancedPaste LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)

# --- 工具函数静态库（主程序和测试共用）---
add_library(AdvancedPasteLib STATIC src/utils.cpp src/render.cpp)
target_include_directories(AdvancedPasteLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(AdvancedPasteLib PUBLIC UNICODE _UNICODE NOMINMAX)
target_link_libraries(AdvancedPasteLib PUBLIC gdiplus shell32 ole32 oleaut32 shlwapi msimg32)
if(MSVC)
    target_compile_options(AdvancedPasteLib PRIVATE /utf-8)
endif()

# --- 主程序 ---
add_executable(AdvancedPaste WIN32
    src/main.cpp
    src/overlay.cpp
    src/app.rc
)
target_link_libraries(AdvancedPaste PRIVATE AdvancedPasteLib dwmapi)
if(MSVC)
    target_compile_options(AdvancedPaste PRIVATE /utf-8)
endif()

# --- 命令行工具（独立 console 子系统，方便脚本/测试调用）---
add_executable(AdvancedPasteCli src/cli.cpp)
target_link_libraries(AdvancedPasteCli PRIVATE AdvancedPasteLib)
if(MSVC)
    target_compile_options(AdvancedPasteCli PRIVATE /utf-8)
endif()

# --- 单元测试 ---
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(AdvancedPasteTests tests/test_utils.cpp)
    target_link_libraries(AdvancedPasteTests PRIVATE AdvancedPasteLib GTest::gtest_main)
    if(MSVC)
        target_compile_options(AdvancedPasteTests PRIVATE /utf-8)
    endif()
    include(GoogleTest)
    gtest_discover_tests(AdvancedPasteTests)
endif()
