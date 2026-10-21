#pragma once

#include <QImage>
#include <QString>
#include <functional>
#include <vector>

#include "TimeBase.h"
#include "timeline/ClipEffects.h"

namespace lumora::ai {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

struct RedactionOptions {
    bool faces = true;
    bool plates = true;
    float faceThreshold = 0.6f;
    float plateThreshold = 0.3f;
};

struct RedactionResult {
    std::vector<RedactionTrack> tracks;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Finds the faces and number plates in [from, to) of a video file and
// follows each from frame to frame. Boxes are in media time and fractions of
// the decoded picture (as Redaction stores them), a little larger than what
// was detected so hair, ears and plate frames are covered too.
RedactionResult findRedactions(const QString& path, Ticks from, Ticks to, const RedactionOptions& options,
                               const Progress& progress = {});

// The same for a photo: one box (time 0) per face or plate, in fractions of
// the picture.
RedactionResult findRedactions(const QImage& image, const RedactionOptions& options);

// Joins per frame detections into tracks: a box that overlaps a box of the
// previous frames continues that track; a track ends when it is not seen
// for `maxGap`.
class DetectionTracker {
public:
    DetectionTracker(RedactionKind kind, Ticks maxGap);
    // Detections of one frame at media time `t`, in fractions of the picture.
    void add(Ticks t, const std::vector<QRectF>& boxes, const std::vector<float>& scores);
    // All tracks, smoothed and simplified. Lone single frame detections with
    // a low score are dropped as noise.
    std::vector<RedactionTrack> finish();

private:
    struct Active {
        RedactionTrack track;
        float bestScore = 0.0f;
    };
    void close(Active& a);

    RedactionKind m_kind;
    Ticks m_maxGap;
    std::vector<Active> m_active;
    std::vector<RedactionTrack> m_done;
};

// Hides `regions` (fractions of the image) by blurring, pixelating or
// filling them. `strength` 0..1: how coarse the blur or the pixels are.
void hideRegions(QImage& image, const std::vector<Redaction::Region>& regions, RedactionStyle style, double strength);

} // namespace lumora::ai
