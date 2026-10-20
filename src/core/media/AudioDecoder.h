#pragma once

#include <vector>

#include "TimeBase.h"
#include "ffmpeg/FFmpeg.h"
#include "media/MediaInfo.h"

namespace lumora {

// Decodes the primary audio stream of a file and converts it to interleaved
// 32-bit float at a fixed output rate / channel count (used for preview
// playback and waveform generation, never for stream-copy export).
class AudioDecoder {
public:
    bool open(const QString& path, int outSampleRate = 48000, int outChannels = 2, QString* error = nullptr);
    bool isOpen() const { return m_codec != nullptr; }
    int sampleRate() const { return m_outRate; }
    int channels() const { return m_outChannels; }
    Ticks origin() const { return m_origin; }

    bool seek(Ticks t);
    // Decodes the next chunk. `start` receives the media time of the first
    // sample. Returns false at end of stream.
    bool next(std::vector<float>& samples, Ticks& start);

private:
    av::FormatInputPtr m_fmt;
    av::CodecContextPtr m_codec;
    av::SwrPtr m_swr;
    av::PacketPtr m_pkt;
    av::FramePtr m_frame;
    AVStream* m_stream = nullptr;
    Ticks m_origin = 0;
    int m_outRate = 48000;
    int m_outChannels = 2;
    bool m_eofSent = false;
    Ticks m_nextPts = kInvalidTicks;
};

} // namespace lumora
