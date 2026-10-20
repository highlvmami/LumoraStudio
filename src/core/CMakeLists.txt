# lumora_core: everything that is not UI. Depends on Qt Core/Gui (QImage,
# QUndoStack, JSON) and FFmpeg, never on Qt Widgets, so it can be reused by a
# command line tool, tests or a different front end.

add_library(lumora_core STATIC
    TimeBase.h TimeBase.cpp
    ffmpeg/FFmpeg.h ffmpeg/FFmpeg.cpp
    media/MediaInfo.h media/MediaInfo.cpp
    media/MediaPool.h media/MediaPool.cpp
    media/VideoDecoder.h media/VideoDecoder.cpp
    media/AudioDecoder.h media/AudioDecoder.cpp
    color/ColorGrade.h color/ColorGrade.cpp
    color/ColorProcessor.h color/ColorProcessor.cpp
    color/Lut.h color/Lut.cpp
    color/Scopes.h color/Scopes.cpp
    audio/AudioEffects.h audio/AudioEffects.cpp
    audio/AudioDsp.h audio/AudioDsp.cpp
    audio/Loudness.h audio/Loudness.cpp
    timeline/ClipEffects.h timeline/ClipEffects.cpp
    timeline/Subtitles.h timeline/Subtitles.cpp
    timeline/Timeline.h timeline/Timeline.cpp
    undo/TimelineCommands.h undo/TimelineCommands.cpp
    render/TitleRenderer.h render/TitleRenderer.cpp
    render/FrameRenderer.h render/FrameRenderer.cpp
    render/AudioMixer.h render/AudioMixer.cpp
    export/ExportSettings.h
    export/ExportCommon.h export/ExportCommon.cpp
    export/Encoders.h export/Encoders.cpp
    export/ExportPlanner.h export/ExportPlanner.cpp
    export/TimelineExporter.h export/TimelineExporter.cpp
    export/RenderExport.h export/RenderExport.cpp
    export/SubtitleStreams.h export/SubtitleStreams.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/color/Lut.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/ExportPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/TimelineExporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/project/Project.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/timeline/Subtitles.cpp
    PARENT_SCOPE)
