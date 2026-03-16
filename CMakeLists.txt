cmake_minimum_required(VERSION 3.20)
project(jefecheck VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system packages
find_package(OpenGL REQUIRED)
find_package(FLTK REQUIRED)
find_package(OpenImageIO REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

# GLAD (in-tree)
add_library(glad STATIC third_party/glad/src/glad.c)
target_include_directories(glad PUBLIC third_party/glad/include)
target_link_libraries(glad PUBLIC OpenGL::GL)

# CLI11 (header-only, in-tree)
add_library(cli11 INTERFACE)
target_include_directories(cli11 INTERFACE third_party/cli11)

# Collect source files
file(GLOB JEFECHECK_SOURCES
    src/*.cpp
    src/*.cxx
)

# Platform-specific exclusions
if(NOT WIN32)
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "WSAStartupSingleton")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "ExtendedOverlappedPool")
endif()

if(NOT UNIX OR APPLE)
    # _FindFirst is a Linux compatibility shim for Windows API
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_FindFirst")
endif()

# Main executable
add_executable(jefecheck ${JEFECHECK_SOURCES})

target_include_directories(jefecheck PRIVATE
    src/
)

target_link_libraries(jefecheck PRIVATE
    glad
    cli11
    OpenGL::GL
    ${FLTK_LIBRARIES}
    OpenImageIO::OpenImageIO
    OpenEXR::OpenEXR
    CURL::libcurl
    ZLIB::ZLIB
)

# FLTK include dirs
target_include_directories(jefecheck PRIVATE ${FLTK_INCLUDE_DIR})

# macOS
if(APPLE)
    target_link_libraries(jefecheck PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
    # GLUT is available as a system framework on macOS
    target_link_libraries(jefecheck PRIVATE "-framework GLUT")
endif()

# Linux
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
    target_link_libraries(jefecheck PRIVATE
        Threads::Threads
        X11::X11
    )
    # Find GLUT on Linux
    find_package(GLUT REQUIRED)
    target_link_libraries(jefecheck PRIVATE GLUT::GLUT)
endif()
