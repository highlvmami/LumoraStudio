set(APP_SOURCES
    main.cpp
    MainWindow.h MainWindow.cpp
    Theme.h Theme.cpp
    Icons.h Icons.cpp
    MediaCache.h MediaCache.cpp
    Updater.h Updater.cpp
    playback/PlaybackSequence.h playback/PlaybackSequence.cpp
    playback/PlaybackEngine.h playback/PlaybackEngine.cpp
    panels/MimeTypes.h
    panels/VideoView.h panels/VideoView.cpp
    panels/MonitorPanel.h panels/MonitorPanel.cpp
    panels/MediaBinPanel.h panels/MediaBinPanel.cpp
    panels/TimelineView.h panels/TimelineView.cpp
    panels/TimelinePanel.h panels/TimelinePanel.cpp
    panels/PropertiesPanel.h panels/PropertiesPanel.cpp
    dialogs/ExportDialog.h dialogs/ExportDialog.cpp
    dialogs/PhotoExportDialog.h dialogs/PhotoExportDialog.cpp
    dialogs/SettingsDialog.h dialogs/SettingsDialog.cpp
    photo/PhotoView.h photo/PhotoView.cpp
    photo/PhotoWorkspace.h photo/PhotoWorkspace.cpp
)

qt_add_executable(LumoraStudio WIN32 MACOSX_BUNDLE ${APP_SOURCES})

target_include_directories(LumoraStudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LumoraStudio PRIVATE lumora_core Qt6::Widgets Qt6::Multimedia Qt6::Concurrent Qt6::Network)
target_compile_definitions(LumoraStudio PRIVATE LUMORA_VERSION="${PROJECT_VERSION}")

# Translations: sources are English, lumora_tr.ts holds Turkish.
qt_add_translations(LumoraStudio
    TS_FILES ${PROJECT_SOURCE_DIR}/src/i18n/lumora_tr.ts
    RESOURCE_PREFIX /i18n
    SOURCES ${APP_SOURCES} ${CORE_TRANSLATABLE_SOURCES}
    LUPDATE_OPTIONS -no-obsolete -locations none)

set_target_properties(LumoraStudio PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.lumorastudio.LumoraStudio
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

if(WIN32)
    # Copy the FFmpeg DLLs next to the executable so it runs from the build tree.
    if(FFMPEG_RUNTIME_DIR)
        add_custom_command(TARGET LumoraStudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${FFMPEG_RUNTIME_DIR}" "$<TARGET_FILE_DIR:LumoraStudio>"
            COMMENT "Copying FFmpeg DLLs")
    endif()
endif()

install(TARGETS LumoraStudio RUNTIME DESTINATION . BUNDLE DESTINATION .)
