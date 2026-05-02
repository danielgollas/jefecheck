cmake_minimum_required(VERSION 3.20)
project(jefecheck VERSION 1.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system packages
find_package(OpenGL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
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
    src/ui/*.cpp
)
# Custom EXR loader/saver — OIIO handles EXR natively.
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

# Main executable
add_executable(jefecheck ${JEFECHECK_SOURCES})

# Suppress warnings in vendored RakNet code (template dependent base class issues)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(jefecheck PRIVATE
        -Wno-deprecated-declarations  # macOS deprecated OpenGL
        -Wno-register               # C++17 removed register keyword
    )
    if(WIN32)
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
    Qt6::Widgets
    Qt6::OpenGLWidgets
)
target_compile_definitions(jefecheck PRIVATE JEFECHECK_USE_QT=1)

# macOS
if(APPLE)
    # Objective-C++ TU for the NSBundle-backed bundle Resources lookup.
    # Used by getApplicationDataPath in src/gfcStructures.cpp.
    target_sources(jefecheck PRIVATE src/macos_bundle_path.mm)
    set_source_files_properties(src/macos_bundle_path.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc")

    target_link_libraries(jefecheck PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework Foundation"
    )

    # Build a proper .app bundle. CFBundleName and CFBundleIdentifier
    # flow into the AXApplication root's title and identifier, which is
    # what assistive tech (and Appium's mac2 driver) query. Also fixes
    # the dock app name and is required for codesign / notarization
    # down the line.
    configure_file(
        ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
        ${CMAKE_BINARY_DIR}/Info.plist
        @ONLY)
    set_target_properties(jefecheck PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist
        OUTPUT_NAME "jefecheck")

    # Copy runtime resources into the bundle so getApplicationDataPath
    # (= argv[0]/../../Resources/) finds FX/ and fonts/ without the
    # workspace symlinks dev builds used to need. Icon goes alongside
    # so CFBundleIconFile resolves.
    set(_app_resources $<TARGET_FILE_DIR:jefecheck>/../Resources)
    add_custom_command(TARGET jefecheck POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_app_resources}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/src/FX ${_app_resources}/FX
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/src/fonts ${_app_resources}/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_SOURCE_DIR}/packaging/macos/JefeCheck.icns
                ${_app_resources}/JefeCheck.icns
        VERBATIM)
endif()

# Linux
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(jefecheck PRIVATE
        Threads::Threads
        GLU
    )
endif()

# Appium-driven UI tests (macOS only). The pytest harness lives in
# tests/ui/ and discovers the binary via JEFECHECK_BIN. Bootstrap the
# venv once (or re-bootstrap when pyproject changes) before running.
if(APPLE)
    set(_uitests_dir ${CMAKE_SOURCE_DIR}/tests/ui)
    set(_uitests_venv ${_uitests_dir}/.venv)
    set(_uitests_stamp ${CMAKE_BINARY_DIR}/uitests-venv.stamp)

    add_custom_command(
        OUTPUT ${_uitests_stamp}
        COMMAND python3 -m venv ${_uitests_venv}
        COMMAND ${_uitests_venv}/bin/pip install --quiet --upgrade pip
        COMMAND ${_uitests_venv}/bin/pip install --quiet -e ${_uitests_dir}
        COMMAND ${CMAKE_COMMAND} -E touch ${_uitests_stamp}
        DEPENDS ${_uitests_dir}/pyproject.toml
        COMMENT "Bootstrapping Python UI test venv")

    add_custom_target(qt-uitests
        COMMAND ${CMAKE_COMMAND} -E env
                JEFECHECK_BIN=$<TARGET_BUNDLE_DIR:jefecheck>
                ${_uitests_venv}/bin/pytest ${_uitests_dir}
        DEPENDS jefecheck ${_uitests_stamp}
        WORKING_DIRECTORY ${_uitests_dir}
        USES_TERMINAL
        VERBATIM)
endif()
