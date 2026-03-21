cmake_minimum_required(VERSION 3.20)
project(jefecheck VERSION 2.0.0 LANGUAGES C CXX)

# C++20 on macOS/Linux, C++17 on Windows (avoids std::byte vs Windows byte conflict)
if(WIN32)
    set(CMAKE_CXX_STANDARD 17)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
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
        target_compile_definitions(jefecheck PRIVATE
            _HAS_STD_BYTE=0          # Prevent std::byte conflicting with Windows byte
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
        GLU
    )
    # Find GLUT on Linux
    find_package(GLUT REQUIRED)
    target_link_libraries(jefecheck PRIVATE GLUT::GLUT)
endif()
