#pragma once

#include <QImage>
#include <QSize>

#include "TimeBase.h"
#include "ffmpeg/FFmpeg.h"
#include "media/MediaInfo.h"

namespace lumora {

struct DecodedFrame {
    av::FramePtr frame;
    Ticks pts = kInvalidTicks; // media time (relative to the file's start)
    Ticks duration = 0;
    bool valid() const { return frame != nullptr; }
};

// Frame-accurate video decoder for a single file. Times are "media time":
// presentation time relative to the container start, in ticks. Not thread
// safe; each playback / export worker owns its own decoder.
class VideoDecoder {
public:
    VideoDecoder() = default;
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const QString& path, QString* error = nullptr, int threads = 0);
    bool isOpen() const { return m_codec != nullptr; }
    const QString& path() const { return m_path; }
    const VideoStreamInfo& info() const { return m_info; }
    Ticks origin() const { return m_origin; }

    // Returns the frame that is on screen at media time `t` (the last frame
    // whose pts <= t). Decodes forward when `t` is close to the current
    // position, otherwise seeks. The returned frame stays owned by the decoder
    // until the next call.
    const AVFrame* frameAt(Ticks t, Ticks* framePts = nullptr, Ticks* frameDur = nullptr);
    // True when frameAt(t) can get there by decoding forward, without a seek.
    bool reachesForward(Ticks t) const;

    // Sequential access.
    bool seek(Ticks t);
    bool next(DecodedFrame& out);

private:
    bool decodeOne(DecodedFrame& out);
    Ticks nominalFrameDuration() const;

    QString m_path;
    VideoStreamInfo m_info;
    av::FormatInputPtr m_fmt;
    av::CodecContextPtr m_codec;
    av::PacketPtr m_pkt;
    AVStream* m_stream = nullptr;
    Ticks m_origin = 0;
    bool m_eofSent = false;
    bool m_eof = false;
    DecodedFrame m_current; // frame returned by frameAt
    DecodedFrame m_lookahead; // decoded frame following m_current
};

// Decodes the frame of a video shown at media time `t` at full quality:
// display size (square pixels, rotation applied), 16 bits per channel,
// straight alpha kept (e.g. GIF, WebM or ProRes 4444 with transparency).
QImage decodeVideoFrame(const QString& path, Ticks t, QString* error = nullptr);

// Converts decoded frames to QImages for display, scaling down when asked.
// Preview only; exports never go through this path.
class FrameConverter {
public:
    QImage convert(const AVFrame* frame, QSize maxSize = QSize());

private:
    av::SwsPtr m_sws;
    int m_srcW = 0, m_srcH = 0, m_srcFmt = -1, m_dstW = 0, m_dstH = 0;
    int m_cs = -1, m_range = -1;
};

} // namespace lumora
