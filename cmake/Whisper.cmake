# Local speech recognition (automatic subtitles) with whisper.cpp, built from
# source as a static library. CPU only; the models are downloaded by the app
# the first time they are used.

option(LUMORA_WITH_WHISPER "Automatic subtitles with whisper.cpp (downloaded at configure time)" ON)

if(LUMORA_WITH_WHISPER)
    include(FetchContent)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
    set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    # Packages must run on other computers: no -march=native. AVX2/FMA
    # (every x86-64 CPU since 2013) keeps recognition fast.
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(GGML_AVX ON CACHE BOOL "" FORCE)
        set(GGML_AVX2 ON CACHE BOOL "" FORCE)
        set(GGML_FMA ON CACHE BOOL "" FORCE)
        set(GGML_F16C ON CACHE BOOL "" FORCE)
    endif()
    set(_lumora_shared ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG v1.9.5
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(whisper)
    set(BUILD_SHARED_LIBS ${_lumora_shared})
endif()
