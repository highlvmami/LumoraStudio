# lumora_core: everything that is not UI. Depends on Qt Core/Gui (QImage,
# QUndoStack, JSON) and FFmpeg, never on Qt Widgets, so it can be reused by a
# command line tool, tests or a different front end.

add_library(lumora_core STATIC
    TimeBase.h TimeBase.cpp
    ffmpeg/FFmpeg.h ffmpeg/FFmpeg.cpp
    media/MediaInfo.h media/MediaInfo.cpp
    media/MediaPool.h media/MediaPool.cpp
    media/VideoDecoder.h media/VideoDecoder.cpp
    media/Proxy.h media/Proxy.cpp
    media/AudioDecoder.h media/AudioDecoder.cpp
    color/ColorGrade.h color/ColorGrade.cpp
    color/ColorProcessor.h color/ColorProcessor.cpp
    color/Lut.h color/Lut.cpp
    color/Looks.h color/Looks.cpp
    color/AutoColor.h color/AutoColor.cpp
    color/Scopes.h color/Scopes.cpp
    audio/AudioEffects.h audio/AudioEffects.cpp
    audio/AudioDsp.h audio/AudioDsp.cpp
    audio/Loudness.h audio/Loudness.cpp
    audio/Beats.h audio/Beats.cpp
    timeline/ClipEffects.h timeline/ClipEffects.cpp
    timeline/Subtitles.h timeline/Subtitles.cpp
    timeline/Timeline.h timeline/Timeline.cpp
    undo/TimelineCommands.h undo/TimelineCommands.cpp
    render/TitleRenderer.h render/TitleRenderer.cpp
    render/TitleTemplates.h render/TitleTemplates.cpp
    render/Collage.h render/Collage.cpp
    render/Effects.h render/Effects.cpp
    render/FrameRenderer.h render/FrameRenderer.cpp
    render/AudioMixer.h render/AudioMixer.cpp
    export/ExportSettings.h
    export/ExportCommon.h export/ExportCommon.cpp
    export/Encoders.h export/Encoders.cpp
    export/ExportPlanner.h export/ExportPlanner.cpp
    export/TimelineExporter.h export/TimelineExporter.cpp
    export/RenderExport.h export/RenderExport.cpp
    export/GifExport.h export/GifExport.cpp
    export/LiveWriter.h export/LiveWriter.cpp
    export/SubtitleStreams.h export/SubtitleStreams.cpp
    motion/Motion.h motion/Motion.cpp
    ai/SpeechToText.h ai/SpeechToText.cpp
    ai/Onnx.h ai/Onnx.cpp
    ai/Vision.h ai/Vision.cpp
    ai/Redaction.h ai/Redaction.cpp
    ai/SmartCut.h ai/SmartCut.cpp
    motion/Analysis.h motion/Analysis.cpp
    image/PhotoAdjustments.h image/PhotoAdjustments.cpp
    image/PhotoProcessor.h image/PhotoProcessor.cpp
    image/BatchPhoto.h image/BatchPhoto.cpp
    image/PhotoLibrary.h image/PhotoLibrary.cpp
    image/RawDecoder.h image/RawDecoder.cpp
    image/HeifDecoder.h image/HeifDecoder.cpp
    project/Project.h project/Project.cpp
    update/UpdateManifest.h update/UpdateManifest.cpp
)

target_include_directories(lumora_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumora_core
    PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent
           FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)
if(TARGET whisper)
    target_link_libraries(lumora_core PRIVATE whisper)
    target_compile_definitions(lumora_core PRIVATE LUMORA_HAVE_WHISPER)
endif()
if(TARGET onnxruntime::onnxruntime)
    target_link_libraries(lumora_core PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(lumora_core PRIVATE LUMORA_HAVE_ONNXRUNTIME)
endif()
if(TARGET LibRaw::LibRaw)
    target_link_libraries(lumora_core PRIVATE LibRaw::LibRaw)
    target_compile_definitions(lumora_core PRIVATE LUMORA_HAVE_LIBRAW)
endif()

# Core sources with user-visible strings, picked up by lupdate.
set(CORE_TRANSLATABLE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/SpeechToText.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/Vision.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/Redaction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ai/SmartCut.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/Beats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/color/Lut.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/color/Looks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/render/TitleTemplates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/render/Collage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/ExportPlanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/TimelineExporter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/GifExport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/export/LiveWriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/image/HeifDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/image/BatchPhoto.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/project/Project.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/timeline/Subtitles.cpp
    PARENT_SCOPE)
