# FindFFmpeg.cmake - locate the libav* libraries without pkg-config.
# Provides imported targets FFmpeg::avformat avcodec avutil swresample swscale.
# Hint with the FFMPEG_ROOT environment variable or CMake variable.
set(_ffmpeg_libs avformat avcodec avutil swresample swscale)
set(_ffmpeg_required_vars "")
foreach(_lib IN LISTS _ffmpeg_libs)
    find_path(FFMPEG_${_lib}_INCLUDE_DIR lib${_lib}/${_lib}.h
        HINTS ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT}
              /opt/homebrew/opt/ffmpeg /usr/local/opt/ffmpeg /usr/local
        PATH_SUFFIXES include)
    find_library(FFMPEG_${_lib}_LIBRARY ${_lib}
        HINTS ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT}
              /opt/homebrew/opt/ffmpeg /usr/local/opt/ffmpeg /usr/local
        PATH_SUFFIXES lib)
    list(APPEND _ffmpeg_required_vars FFMPEG_${_lib}_INCLUDE_DIR FFMPEG_${_lib}_LIBRARY)
    mark_as_advanced(FFMPEG_${_lib}_INCLUDE_DIR FFMPEG_${_lib}_LIBRARY)
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS ${_ffmpeg_required_vars})

if(FFmpeg_FOUND)
    foreach(_lib IN LISTS _ffmpeg_libs)
        if(NOT TARGET FFmpeg::${_lib})
            add_library(FFmpeg::${_lib} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_lib} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${_lib}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_${_lib}_INCLUDE_DIR}")
        endif()
    endforeach()
endif()
