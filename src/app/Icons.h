#pragma once

#include <QIcon>

namespace lumora::Icons {

// Small vector icons drawn with QPainter in the current palette's text
// colour, so they follow the dark / light theme without image assets.
QIcon play();
QIcon pause();
QIcon stepBack();
QIcon stepForward();
QIcon toStart();
QIcon toEnd();
QIcon markIn();
QIcon markOut();
QIcon add();
QIcon split();
QIcon remove();
QIcon join();
QIcon undo();
QIcon redo();
QIcon import();
QIcon exportFile();
QIcon crop();
QIcon magnet();
QIcon zoomIn();
QIcon zoomOut();
QIcon reset();
QIcon compare();
QIcon keyframe();
QIcon appIcon();

} // namespace lumora::Icons
