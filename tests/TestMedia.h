#pragma once

#include <QString>
#include <vector>

extern "C" {
struct AVFrame;
}

namespace lumora::test {

struct TestVideoSpec {
    int seconds = 4;
    int fps = 25;
    int width = 320;
    int height = 240;
    int gop = 10;         // key frame every `gop` frames
    int bFrames = 2;      // 0 = no B-frames
    bool audio = true;
    const char* encoder = "mpeg4";
};

// Writes a synthetic MP4 whose frames carry their own index as a binary
// pattern of black/white blocks, so tests can tell exactly which source frame
// ended up where after cutting, joining or re-encoding.
bool writeTestVideo(const QString& path, const TestVideoSpec& spec, QString* error = nullptr);

// Reads the index pattern back from a decoded frame (any pixel format).
int frameIndexOf(const AVFrame* frame);

struct DecodedSummary {
    std::vector<int> frameIndices; // pattern index of every decoded frame, in display order
    int decodeErrors = 0;
    double durationSeconds = 0;
    int audioPackets = 0;
    bool hasAudio = false;
};

DecodedSummary decodeAll(const QString& path);

// MD5 of the raw pixels of every decoded frame, in display order.
std::vector<QByteArray> decodedFrameHashes(const QString& path);

// Duration in seconds of the audio stream (sum of packet durations).
double audioDuration(const QString& path);

// MD5 of each video packet payload, in decode order.
std::vector<QByteArray> videoPacketHashes(const QString& path);

} // namespace lumora::test
