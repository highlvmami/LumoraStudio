# Local AI tools (face / number plate blur, background removal) run small
# neural networks with ONNX Runtime. The prebuilt CPU package is downloaded at
# configure time; LUMORA_ONNXRUNTIME_ROOT can point to an extracted package
# instead (CI keeps one in its cache). The models themselves are downloaded by
# the app the first time a tool is used.
#
# Defines the imported target onnxruntime::onnxruntime and, on Windows, the
# function lumora_copy_onnxruntime(target) that puts the DLLs next to an
# executable.

option(LUMORA_WITH_ONNXRUNTIME "Local AI tools with ONNX Runtime (downloaded at configure time)" ON)
set(LUMORA_ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime package (optional)")

set(_ort_version 1.22.0)

function(lumora_copy_onnxruntime target)
    if(WIN32 AND TARGET onnxruntime::onnxruntime)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LUMORA_ONNXRUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()

if(NOT LUMORA_WITH_ONNXRUNTIME)
    return()
endif()

set(_ort_root "${LUMORA_ONNXRUNTIME_ROOT}")
if(NOT _ort_root)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_ort_package onnxruntime-win-x64-${_ort_version}.zip)
        set(_ort_hash 174c616efc0271194488642a72f1a514e01487da4dfe84c49296d66e40ebe0da)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(_ort_package onnxruntime-linux-x64-${_ort_version}.tgz)
        set(_ort_hash 8344d55f93d5bc5021ce342db50f62079daf39aaafb5d311a451846228be49b3)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(_ort_package onnxruntime-linux-aarch64-${_ort_version}.tgz)
    elseif(APPLE)
        set(_ort_package onnxruntime-osx-universal2-${_ort_version}.tgz)
    else()
        message(STATUS "ONNX Runtime: no prebuilt package for this platform, AI tools disabled")
        return()
    endif()
    include(FetchContent)
    set(_ort_args URL https://github.com/microsoft/onnxruntime/releases/download/v${_ort_version}/${_ort_package})
    if(_ort_hash)
        list(APPEND _ort_args URL_HASH SHA256=${_ort_hash})
    endif()
    FetchContent_Declare(onnxruntime ${_ort_args} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(onnxruntime)
    set(_ort_root "${onnxruntime_SOURCE_DIR}")
endif()

if(NOT EXISTS "${_ort_root}/include/onnxruntime_cxx_api.h")
    message(WARNING "ONNX Runtime not found in ${_ort_root}, AI tools disabled")
    return()
endif()

add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_ort_root}/include")
if(WIN32)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_IMPLIB "${_ort_root}/lib/onnxruntime.lib"
        IMPORTED_LOCATION "${_ort_root}/lib/onnxruntime.dll")
    file(GLOB LUMORA_ONNXRUNTIME_DLLS "${_ort_root}/lib/*.dll")
    set(LUMORA_ONNXRUNTIME_DLLS ${LUMORA_ONNXRUNTIME_DLLS} CACHE INTERNAL "")
elseif(APPLE)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_root}/lib/libonnxruntime.${_ort_version}.dylib")
else()
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_root}/lib/libonnxruntime.so.${_ort_version}"
        IMPORTED_SONAME "libonnxruntime.so.1")
endif()
message(STATUS "ONNX Runtime ${_ort_version}: ${_ort_root}")
