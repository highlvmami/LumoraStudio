# FindFFmpeg
# ----------
# Locates the FFmpeg libraries (libav*) and creates an imported target per
# component: FFmpeg::avformat, FFmpeg::avcodec, FFmpeg::avutil, ...
#
# Search order:
#   1. FFMPEG_ROOT (CMake or environment variable), e.g. an unpacked Windows
#      "shared" build that contains include/, lib/ and bin/.
#   2. pkg-config (Linux / macOS / MSYS2).
#   3. Default system paths.
#
# On Windows FFMPEG_RUNTIME_DIR is set to the folder that holds the DLLs so that
# they can be copied next to the executable.

set(_ffmpeg_all_components avformat avcodec avutil swscale swresample avfilter avdevice)
if(NOT FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_FIND_COMPONENTS avformat avcodec avutil)
endif()

if(NOT FFMPEG_ROOT AND DEFINED ENV{FFMPEG_ROOT})
    set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}")
endif()

find_package(PkgConfig QUIET)

set(_ffmpeg_header_avformat libavformat/avformat.h)
set(_ffmpeg_header_avcodec libavcodec/avcodec.h)
set(_ffmpeg_header_avutil libavutil/avutil.h)
set(_ffmpeg_header_swscale libswscale/swscale.h)
set(_ffmpeg_header_swresample libswresample/swresample.h)
set(_ffmpeg_header_avfilter libavfilter/avfilter.h)
set(_ffmpeg_header_avdevice libavdevice/avdevice.h)

foreach(_comp IN LISTS FFmpeg_FIND_COMPONENTS)
    if(NOT _comp IN_LIST _ffmpeg_all_components)
        message(FATAL_ERROR "Unknown FFmpeg component: ${_comp}")
    endif()

    set(_hints_inc "")
    set(_hints_lib "")
    if(FFMPEG_ROOT)
        list(APPEND _hints_inc "${FFMPEG_ROOT}/include")
        list(APPEND _hints_lib "${FFMPEG_ROOT}/lib")
    endif()
    if(PKG_CONFIG_FOUND AND NOT FFMPEG_ROOT)
        pkg_check_modules(PC_FFMPEG_${_comp} QUIET lib${_comp})
        list(APPEND _hints_inc ${PC_FFMPEG_${_comp}_INCLUDE_DIRS})
        list(APPEND _hints_lib ${PC_FFMPEG_${_comp}_LIBRARY_DIRS})
    endif()

    find_path(FFmpeg_${_comp}_INCLUDE_DIR ${_ffmpeg_header_${_comp}} HINTS ${_hints_inc})
    find_library(FFmpeg_${_comp}_LIBRARY NAMES ${_comp} lib${_comp} HINTS ${_hints_lib})

    if(FFmpeg_${_comp}_INCLUDE_DIR AND FFmpeg_${_comp}_LIBRARY)
        set(FFmpeg_${_comp}_FOUND TRUE)
        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFmpeg_${_comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_${_comp}_INCLUDE_DIR}")
        endif()
        list(APPEND FFmpeg_LIBRARIES FFmpeg::${_comp})
        list(APPEND FFmpeg_INCLUDE_DIRS ${FFmpeg_${_comp}_INCLUDE_DIR})
    else()
        set(FFmpeg_${_comp}_FOUND FALSE)
    endif()
    mark_as_advanced(FFmpeg_${_comp}_INCLUDE_DIR FFmpeg_${_comp}_LIBRARY)
endforeach()

if(FFmpeg_avutil_INCLUDE_DIR AND EXISTS "${FFmpeg_avutil_INCLUDE_DIR}/libavutil/ffversion.h")
    file(STRINGS "${FFmpeg_avutil_INCLUDE_DIR}/libavutil/ffversion.h" _ffver REGEX "FFMPEG_VERSION")
    string(REGEX REPLACE ".*\"n?([^\"]*)\".*" "\\1" FFmpeg_VERSION "${_ffver}")
endif()

if(WIN32 AND FFMPEG_ROOT AND EXISTS "${FFMPEG_ROOT}/bin")
    set(FFMPEG_RUNTIME_DIR "${FFMPEG_ROOT}/bin")
endif()

if(FFmpeg_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES FFmpeg_INCLUDE_DIRS)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFmpeg_INCLUDE_DIRS FFmpeg_LIBRARIES
    VERSION_VAR FFmpeg_VERSION
    HANDLE_COMPONENTS)
