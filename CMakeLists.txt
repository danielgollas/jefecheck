cmake_minimum_required(VERSION 3.20)
project(jefecheck VERSION 1.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find system packages
find_package(OpenGL REQUIRED)
# Network: the OAuth loopback listener and the token exchange (JEF-31). Ships
# in qt6-base, which every platform already installs (brew qt, qt6-base-dev,
# mingw-w64-x86_64-qt6-base), so this adds no package to any CI image.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network)
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

# Desktop sign-in (JEF-31). Its own directory rather than src/qt/: these units
# depend on Qt but not on the viewport or the rendering chain, so they stay
# includable from headless self-tests without dragging GL in.
file(GLOB JEFECHECK_AUTH_SOURCES src/auth/*.cpp)
list(APPEND JEFECHECK_SOURCES ${JEFECHECK_AUTH_SOURCES})

# Main executable
add_executable(jefecheck ${JEFECHECK_SOURCES})

# Suppress warnings in vendored RakNet code (template dependent base class issues)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(jefecheck PRIVATE
        -Wno-deprecated-declarations  # macOS deprecated OpenGL
        -Wno-register               # C++17 removed register keyword
    )
    if(WIN32)
        # MinGW64 defines _UNICODE by default, which flips xmlParser
        # (vendored in src/) into wchar_t* mode via its `#ifdef _UNICODE`
        # gate. Our XML schema uses `const char*` literals throughout, so
        # disable the macro globally on Windows to keep xmlParser in
        # UTF-8/char* mode. Without this, the build hits a cascade of
        # "cannot convert const char[N] to const wchar_t*" errors in
        # gfcStructures.h / gfcNetworkStructures.h template instantiations.
        target_compile_options(jefecheck PRIVATE
            -UUNICODE -U_UNICODE
            # -fpermissive keeps a few residual void*/long FLTK-callback
            # conversions in legacy ports compiling on the stricter MinGW
            # toolchain. Harmless on the Qt build; cheap to keep.
            -fpermissive
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
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network
)
target_compile_definitions(jefecheck PRIVATE JEFECHECK_USE_QT=1)

# --- WebRTC transport (libdatachannel) --------------------------------------
# JEF-24: second ITransport backend over DTLS-encrypted data channels.
# RakNet stays the runtime default (env JEFECHECK_TRANSPORT selects).
option(JEFECHECK_WEBRTC "Build WebRTC transport (libdatachannel)" ON)
if(JEFECHECK_WEBRTC)
    # libdatachannel v0.22.0 vendors an old plog whose CMake predates 3.5;
    # CMake 4.x refuses to configure it without this floor.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    if(APPLE)
        set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
    endif()
    include(FetchContent)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
    FetchContent_Declare(libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG v0.22.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(libdatachannel)
    target_link_libraries(jefecheck PRIVATE LibDataChannel::LibDataChannel)
    target_compile_definitions(jefecheck PRIVATE JEFECHECK_WEBRTC=1)
endif()

# --- Static FFmpeg for video export ----------------------------------------
# Fetch the same GPL static ffmpeg the release packaging uses, once, into the
# build tree, so a dev/.app build resolves it from the bundle. GUI launches
# get a minimal PATH (no /opt/homebrew/bin), so the PATH fallback misses —
# bundling next to the app is what makes video export work out of the box.
# Override the source with -DJEFECHECK_FFMPEG_BIN=/path (or the env var);
# turn the whole thing off with -DJEFECHECK_BUNDLE_FFMPEG=OFF.
option(JEFECHECK_BUNDLE_FFMPEG "Bundle a static ffmpeg for video export" ON)
set(_ffmpeg_bundled "")
if(JEFECHECK_BUNDLE_FFMPEG)
    set(_ffmpeg_cache "${CMAKE_BINARY_DIR}/ffmpeg-static")
    set(_ffmpeg_src "$ENV{JEFECHECK_FFMPEG_BIN}")
    if(NOT _ffmpeg_src AND DEFINED JEFECHECK_FFMPEG_BIN)
        set(_ffmpeg_src "${JEFECHECK_FFMPEG_BIN}")
    endif()
    if(NOT EXISTS "${_ffmpeg_cache}")
        if(_ffmpeg_src AND EXISTS "${_ffmpeg_src}")
            configure_file("${_ffmpeg_src}" "${_ffmpeg_cache}" COPYONLY)
        else()
            if(APPLE AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
                set(_ffmpeg_asset "ffmpeg-darwin-arm64")
            elseif(APPLE)
                set(_ffmpeg_asset "ffmpeg-darwin-x64")
            elseif(WIN32)
                set(_ffmpeg_asset "ffmpeg-win32-x64")
            else()
                set(_ffmpeg_asset "ffmpeg-linux-x64")
            endif()
            message(STATUS "Fetching static ffmpeg (${_ffmpeg_asset}) for video export...")
            file(DOWNLOAD
                "https://github.com/eugeneware/ffmpeg-static/releases/download/b6.1.1/${_ffmpeg_asset}"
                "${_ffmpeg_cache}" TLS_VERIFY ON STATUS _ffmpeg_dl)
            list(GET _ffmpeg_dl 0 _ffmpeg_dl_code)
            if(NOT _ffmpeg_dl_code EQUAL 0)
                message(WARNING "ffmpeg download failed (${_ffmpeg_dl}); "
                    "video export will rely on a Preferences/PATH ffmpeg")
                file(REMOVE "${_ffmpeg_cache}")
            endif()
        endif()
    endif()
    if(EXISTS "${_ffmpeg_cache}")
        file(CHMOD "${_ffmpeg_cache}" PERMISSIONS
             OWNER_READ OWNER_WRITE OWNER_EXECUTE
             GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
        set(_ffmpeg_bundled "${_ffmpeg_cache}")
    endif()
endif()

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

    # Drop the static ffmpeg into Resources/ where VideoEncoder_Qt::findFfmpeg
    # looks first (Contents/Resources/ffmpeg).
    if(_ffmpeg_bundled)
        add_custom_command(TARGET jefecheck POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_ffmpeg_bundled}" ${_app_resources}/ffmpeg
            COMMAND chmod +x ${_app_resources}/ffmpeg
            VERBATIM)
    endif()
endif()

# Non-macOS dev builds: drop ffmpeg next to the binary (where findFfmpeg
# looks first on Linux/Windows).
if(NOT APPLE AND _ffmpeg_bundled)
    if(WIN32)
        set(_ffmpeg_dest_name "ffmpeg.exe")
    else()
        set(_ffmpeg_dest_name "ffmpeg")
    endif()
    add_custom_command(TARGET jefecheck POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_ffmpeg_bundled}" $<TARGET_FILE_DIR:jefecheck>/${_ffmpeg_dest_name}
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
