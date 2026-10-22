#pragma once

#include <QIcon>

namespace lumora::Icons {

// Small vector icons drawn with QPainter in the current palette's text
// colour, so they follow the dark / light theme without image assets.
// An explicit colour is used for icons drawn on an accent background.
QIcon play(const QColor& color = {});
QIcon pause(const QColor& color = {});
QIcon stepBack(const QColor& color = {});
QIcon stepForward(const QColor& color = {});
QIcon toStart();
QIcon toEnd();
QIcon markIn();
QIcon markOut();
QIcon add();
QIcon split();
QIcon remove();
QIcon join();
QIcon freeze();
QIcon undo();
QIcon redo();
QIcon import();
QIcon exportFile(const QColor& color = {});
QIcon crop();
QIcon removeBackground();
QIcon magnet();
QIcon zoomIn();
QIcon zoomOut();
QIcon reset();
QIcon compare();
QIcon keyframe();
QIcon title();
QIcon transition();
QIcon sticker();
QIcon close(const QColor& color = {});
QIcon popOut();
QIcon fullScreen(const QColor& color = {});
QIcon exitFullScreen(const QColor& color = {});
QIcon appIcon();
// 0.7 tools and the properties panel tabs.
QIcon smart();      // sparkles: automatic / AI tools
QIcon speed();      // gauge
QIcon effects();    // magic wand
QIcon sliders();    // edit settings
QIcon sound();      // speaker
QIcon info();       // i in a circle
QIcon filter();     // overlapping circles
QIcon keyColor();   // green screen
QIcon collage();    // grid of pictures
QIcon record(const QColor& color = {}); // filled circle
QIcon microphone();
QIcon screen();     // monitor
QIcon batch();      // stacked photos
QIcon chevron(bool open);

} // namespace lumora::Icons
