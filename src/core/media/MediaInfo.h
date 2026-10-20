#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

#include "TimeBase.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavcodec/codec_id.h>
}

namespace lumora {

enum class MediaKind { Unknown, Video, Audio, Image };

struct VideoStreamInfo {
    int index = -1;
    AVCodecID codecId = AV_CODEC_ID_NONE;
    QString codecName;
    QString profile;
    int width = 0;
    int height = 0;
    AVRational sampleAspectRatio{0, 1};
    AVRational frameRate{0, 1};     // average frame rate
    AVRational realFrameRate{0, 1}; // r_frame_rate (lowest common rate)
    AVRational timeBase{1, 1};
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    int bitDepth = 8;
    AVColorPrimaries colorPrimaries = AVCOL_PRI_UNSPECIFIED;
    AVColorTransferCharacteristic colorTrc = AVCOL_TRC_UNSPECIFIED;
    AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
    AVColorRange colorRange = AVCOL_RANGE_UNSPECIFIED;
    AVChromaLocation chromaLocation = AVCHROMA_LOC_UNSPECIFIED;
    std::int64_t bitRate = 0;
    bool hasBFrames = false;
    bool variableFrameRate = false;
    int rotation = 0; // display matrix rotation in degrees (clockwise)
    Ticks startTime = 0;
    Ticks duration = 0;

    bool isHdr() const {
        return colorTrc == AVCOL_TRC_SMPTE2084 || colorTrc == AVCOL_TRC_ARIB_STD_B67;
    }
};

struct AudioStreamInfo {
    int index = -1;
    AVCodecID codecId = AV_CODEC_ID_NONE;
    QString codecName;
    int sampleRate = 0;
    int channels = 0;
    QString channelLayout;
    QString sampleFormat;
    std::int64_t bitRate = 0;
    AVRational timeBase{1, 1};
    Ticks startTime = 0;
    Ticks duration = 0;
};

struct MediaInfo {
    QString path;
    MediaKind kind = MediaKind::Unknown;
    QString formatName;     // e.g. "mov,mp4,m4a,3gp,3g2,mj2"
    QString formatLongName; // e.g. "QuickTime / MOV"
    Ticks duration = 0;
    std::int64_t bitRate = 0;
    std::int64_t fileSize = 0;
    std::optional<VideoStreamInfo> video;   // primary video stream
    std::optional<AudioStreamInfo> audio;   // primary audio stream
    int videoStreamCount = 0;
    int audioStreamCount = 0;
    int otherStreamCount = 0;               // subtitles, data, attachments
    // Image-only fields.
    int imageWidth = 0;
    int imageHeight = 0;
    int imageBitDepth = 8;

    bool isValid() const { return kind != MediaKind::Unknown; }
    QString summary() const;
};

// Probes a media file with FFmpeg (and Qt's image readers for stills).
// Returns std::nullopt and sets `error` if the file cannot be opened.
std::optional<MediaInfo> probeMedia(const QString& path, QString* error = nullptr);

// Reads all packets of a stream (no decoding) and returns the presentation
// times of key frames, sorted ascending. Cheap enough to run in the background
// on import; required for stream-copy (lossless) cut planning.
std::vector<Ticks> scanKeyframes(const QString& path, int streamIndex, QString* error = nullptr);

QStringList videoFileExtensions();
QStringList imageFileExtensions();
bool isImageFile(const QString& path);

int bitDepthOf(AVPixelFormat fmt);
QString formatFrameRate(AVRational rate);

} // namespace lumora
