# lumora_core: everything that is not UI. Depends on Qt Core/Gui (QImage,
# QUndoStack, JSON) and FFmpeg, never on Qt Widgets, so it can be reused by a
# command line tool, tests or a different front end.

add_library(lumora_core STATIC
    Time.h Time.cpp
    ffmpeg/FFmpeg.h ffmpeg/FFmpeg.cpp
    media/MediaInfo.h media/MediaInfo.cpp
    media/MediaPool.h media/MediaPool.cpp
    media/VideoDecoder.h media/VideoDecoder.cpp
    media/AudioDecoder.h media/AudioDecoder.cpp
    timeline/Timeline.h timeline/Timeline.cpp
    undo/TimelineCommands.h undo/TimelineCommands.cpp
    export/ExportSettings.h
    export/ExportPlanner.h export/ExportPlanner.cpp
    export/TimelineExporter.h export/TimelineExporter.cpp
    image/PhotoAdjustments.h image/PhotoAdjustments.cpp
    image/PhotoProcessor.h image/PhotoProcessor.cpp
    image/PhotoLibrary.h image/PhotoLibrary.cpp
    project/Project.h project/Project.cpp
)

target_include_directories(lumora_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumora_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent
           FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)

# Core sources with user-visible strings, picked up by lupdate.
set(CORE_TRANSLATABLE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/export/ExportPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/TimelineExporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/project/Project.cpp
    PARENT_SCOPE)
