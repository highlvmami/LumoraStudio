#pragma once

#include <QString>
#include <functional>

#include "media/MediaInfo.h"

namespace lumora::proxy {

// Preview copies ("proxies") of heavy videos: 4K and larger, often HEVC
// 10-bit at 60 fps, which most computers cannot decode in real time. The
// copy is at most 1920 pixels on its long side, 8-bit H.264 with short key
// frame intervals, and has exactly the same frame times, rotation and colour
// tags as the original, so the preview shows the same frames. Only the
// playback preview reads it; exports always read the original.

// True for videos that get a preview copy.
bool wanted(const MediaInfo& info);

// Where the copy of `source` is kept (in the cache folder). The name depends
// on the file's path, size and modification time, so a changed file gets a
// new copy.
QString pathFor(const QString& source);

// Writes the copy to `dest` (through a temporary file, so `dest` exists only
// when it is complete). `progress` gets 0..1 and cancels by returning false.
// `hardware` decodes the original on the graphics card when it can.
bool make(const QString& source, const QString& dest, const std::function<bool(double)>& progress = {},
          bool hardware = false, QString* error = nullptr);

// Marks proxy files; VideoDecoder counts their times from zero.
inline constexpr const char* kMetadataKey = "lumora_proxy";

} // namespace lumora::proxy
