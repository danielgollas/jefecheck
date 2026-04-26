cmake_minimum_required(VERSION 3.20)
project(jefecheck VERSION 1.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# UI backend selection (FLTK is the current default; Qt is in-progress).
# See docs/MIGRATION.md for the FLTK->Qt migration plan and current state.
option(USE_QT "Build with Qt6 instead of FLTK" OFF)

# Find system packages
find_package(OpenGL REQUIRED)
if(USE_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
else()
    find_package(FLTK REQUIRED)
endif()
find_package(OpenImageIO REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Freetype REQUIRED)

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
    src/ui/*.cpp
    src/callbacks/*.cpp
)

# Backend filtering: include only the active backend's *_fltk / *_qt files,
# and pull in src/qt/*.cpp on USE_QT.
if(USE_QT)
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_fltk\\.cpp$")
    file(GLOB JEFECHECK_QT_SOURCES src/qt/*.cpp)
    list(APPEND JEFECHECK_SOURCES ${JEFECHECK_QT_SOURCES})
else()
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_qt\\.cpp$")
endif()

# Platform-specific exclusions
if(NOT WIN32)
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "WSAStartupSingleton")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "ExtendedOverlappedPool")
endif()

# _FindFirst provides POSIX implementations of Windows FindFirst/FindNext API
# Needed on Linux and macOS; excluded only on actual Windows
if(WIN32)
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_FindFirst")
endif()

# gfcTrack.cpp is a duplicate of gfcSequence.cpp (same symbols)
list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcTrack\\.cpp")

# Exclude custom EXR loader/saver - OIIO handles EXR natively
list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcimageloaderexr")
list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcimagesaver_exr")

# Main executable
add_executable(jefecheck ${JEFECHECK_SOURCES})

# Suppress warnings in vendored RakNet code (template dependent base class issues)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(jefecheck PRIVATE
        -Wno-deprecated-declarations  # macOS deprecated OpenGL
        -Wno-register               # C++17 removed register keyword
    )
    if(WIN32)
        target_compile_options(jefecheck PRIVATE
            -fpermissive             # void* to int casts in FLTK callbacks
        )
        target_link_libraries(jefecheck PRIVATE
            glu32 opengl32 ws2_32 winmm iphlpapi
        )
    endif()
endif()

# Find Imath (separated from OpenEXR in v3+)
find_package(Imath QUIET)

target_include_directories(jefecheck PRIVATE
    src/
)

# Add Imath include dirs (separated from OpenEXR in v3+)
if(Imath_FOUND)
    target_link_libraries(jefecheck PRIVATE Imath::Imath)
endif()

target_link_libraries(jefecheck PRIVATE
    glad
    cli11
    Freetype::Freetype
    OpenGL::GL
    OpenImageIO::OpenImageIO
    OpenEXR::OpenEXR
    CURL::libcurl
    ZLIB::ZLIB
)

# Backend-specific includes / libs
if(USE_QT)
    target_compile_definitions(jefecheck PRIVATE JEFECHECK_USE_QT=1)
    target_link_libraries(jefecheck PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)
else()
    target_compile_definitions(jefecheck PRIVATE JEFECHECK_USE_FLTK=1)
    target_link_libraries(jefecheck PRIVATE ${FLTK_LIBRARIES})
    target_include_directories(jefecheck PRIVATE ${FLTK_INCLUDE_DIR})
endif()

# macOS
if(APPLE)
    target_link_libraries(jefecheck PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# Linux
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
    target_link_libraries(jefecheck PRIVATE
        Threads::Threads
        X11::X11
        GLU
    )
endif()
