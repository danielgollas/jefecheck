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
if(USE_QT)
    # Qt build: include the whole codebase, then exclude the FLTK-only TUs.
    # Files that compile in both backends keep their FLTK touchpoints
    # gated behind JEFECHECK_USE_FLTK; files that are wholly FLTK (FLUID
    # windows, the callback shims, gfcStructures, etc.) are excluded
    # outright and replaced by the Qt-side equivalents in src/qt/.
    file(GLOB JEFECHECK_SOURCES
        src/*.cpp
        src/ui/*.cpp
    )
    # FLUID-generated FLTK window definitions and dropins.
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "Window\\.cpp$")
    # Concrete FLTK GUI implementations (have *_qt.cpp counterparts in src/qt/).
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_fltk\\.cpp$")
    # FLTK app entry point and the FLTK Fl_Gl_Window itself.
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "main\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "GlViewport\\.cpp$")
    # gfcStructures.cpp stays in the build — it has many FLTK-free utility
    # functions (ftos, ReadTextFileIntoString, swabFloat, gfcTimer, etc.)
    # that the rest of the chain links against. Its FLTK touchpoints are
    # gated behind JEFECHECK_USE_FLTK.
    # File chooser, text renderer, UI callbacks, etc. — FLTK-only.
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcfilechooser\\.cpp$")
    # gfcTextRenderer is FLTK-free despite the comment in its header;
    # leave it in the build so gfcplatemanager.cpp's text overlays link.
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "UICallbacks\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "trackwidget\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfctimer\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcui_fltk\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "iapplication_fltk\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfctrackmanagergui\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "Fl_.*\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "fxcontrolwindow\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "playlistwindow\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "renderwindow\\.cpp$")
    # Custom FLTK widget subclasses — pure FLTK Fl_Widget/Fl_Group/etc. extensions.
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcFXControlBase\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcSliderInput\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcplaylistwindowwindow\\.cpp$")
    # Same exclusions as the FLTK build for non-Windows / duplicated /
    # disabled files.
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcTrack\\.cpp")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcimageloaderexr")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "gfcimagesaver_exr")
    if(NOT WIN32)
        list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "WSAStartupSingleton")
        list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "ExtendedOverlappedPool")
    endif()
    if(WIN32)
        list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_FindFirst")
    endif()

    file(GLOB JEFECHECK_QT_SOURCES src/qt/*.cpp)
    list(APPEND JEFECHECK_SOURCES ${JEFECHECK_QT_SOURCES})
else()
    file(GLOB JEFECHECK_SOURCES
        src/*.cpp
        src/*.cxx
        src/ui/*.cpp
        src/callbacks/*.cpp
    )
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "_qt\\.cpp$")
    list(FILTER JEFECHECK_SOURCES EXCLUDE REGEX "main_qt\\.cpp$")

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
endif()

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
