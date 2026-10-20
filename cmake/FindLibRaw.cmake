# FindLibRaw
# ----------
# Locates LibRaw (camera RAW decoding) and creates the imported target
# LibRaw::LibRaw, using the thread-safe libraw_r build.
#
# Search order:
#   1. LibRaw's own CMake package (vcpkg on Windows: libraw:x64-windows).
#   2. pkg-config (Linux / macOS / MSYS2).
#
# RAW support is optional: without LibRaw the app builds and simply does not
# list RAW files.

if(TARGET LibRaw::LibRaw)
    set(LibRaw_FOUND TRUE)
    return()
endif()

find_package(libraw CONFIG QUIET)
if(TARGET libraw::raw_r)
    add_library(LibRaw::LibRaw INTERFACE IMPORTED)
    target_link_libraries(LibRaw::LibRaw INTERFACE libraw::raw_r)
elseif(TARGET libraw::raw)
    add_library(LibRaw::LibRaw INTERFACE IMPORTED)
    target_link_libraries(LibRaw::LibRaw INTERFACE libraw::raw)
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_LIBRAW QUIET IMPORTED_TARGET libraw_r)
        if(NOT PC_LIBRAW_FOUND)
            pkg_check_modules(PC_LIBRAW QUIET IMPORTED_TARGET libraw)
        endif()
        if(PC_LIBRAW_FOUND)
            add_library(LibRaw::LibRaw INTERFACE IMPORTED)
            target_link_libraries(LibRaw::LibRaw INTERFACE PkgConfig::PC_LIBRAW)
            set(LibRaw_VERSION ${PC_LIBRAW_VERSION})
        endif()
    endif()
endif()

include(FindPackageHandleStandardArgs)
if(TARGET LibRaw::LibRaw)
    set(_libraw_target LibRaw::LibRaw)
endif()
find_package_handle_standard_args(LibRaw REQUIRED_VARS _libraw_target VERSION_VAR LibRaw_VERSION)
