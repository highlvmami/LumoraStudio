cmake_minimum_required(VERSION 3.21)

project(LumoraStudio
    VERSION 0.4.2
    DESCRIPTION "Free video and photo editor that never loses quality unless asked to"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(LUMORA_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /wd4251)
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Multimedia Network LinguistTools)
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swscale swresample)

option(LUMORA_WITH_LIBRAW "Open camera RAW photos with LibRaw when it is installed" ON)
option(LUMORA_REQUIRE_LIBRAW "Fail when LibRaw is missing (release builds)" OFF)
if(LUMORA_REQUIRE_LIBRAW)
    find_package(LibRaw REQUIRED)
elseif(LUMORA_WITH_LIBRAW)
    find_package(LibRaw)
endif()

add_subdirectory(src/core)
add_subdirectory(src/app)

if(LUMORA_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()
