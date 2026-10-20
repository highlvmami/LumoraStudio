#pragma once

#include <QString>
#include <functional>
#include <vector>

#include "export/ExportSettings.h"
#include "ffmpeg/FFmpeg.h"

namespace lumora {

// Writes the plan's embedded subtitle streams. Streams are added before the
// file header is written; cues are then handed to the muxer as the picture
// reaches them, so the file stays interleaved.
class SubtitleStreams {
public:
    // Adds one stream per plan.subtitleStreams entry. False (with `error`)
    // when the encoder cannot be started.
    bool addStreams(AVFormatContext* out, const ExportPlan& plan, QString* error);
    bool empty() const { return m_streams.empty(); }
    // Writes every cue starting before output time `t`.
    bool writeUntil(Ticks t, const std::function<bool(AVPacket*, int)>& write);
    bool finish(const std::function<bool(AVPacket*, int)>& write) { return writeUntil(INT64_MAX, write); }

    // The muxer's subtitle codec for a container, or AV_CODEC_ID_NONE when it
    // cannot hold text subtitles.
    static AVCodecID codecFor(const QString& muxerName);
    // Writes the plan's sidecar files next to `videoPath`; returns the paths.
    static QStringList writeSidecars(const ExportPlan& plan, const QString& videoPath, QString* error);

private:
    struct Stream {
        int index = -1;
        av::CodecContextPtr enc;
        std::vector<SubtitleCue> cues;
        size_t next = 0;
    };
    std::vector<Stream> m_streams;
    AVFormatContext* m_out = nullptr;
};

} // namespace lumora
