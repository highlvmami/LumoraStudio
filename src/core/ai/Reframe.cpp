#include "Reframe.h"

#include <QCoreApplication>
#include <QTransform>
#include <algorithm>
#include <cmath>

#include "ai/Vision.h"
#include "media/VideoDecoder.h"

namespace lumora::ai {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::ai)
};

constexpr double kSampleRate = 5.0; // looks per second

QSizeF fitSize(double pictureAspect, QSize frame) {
    const double fa = static_cast<double>(frame.width()) / frame.height();
    return pictureAspect > fa ? QSizeF(frame.width(), frame.width() / pictureAspect)
                              : QSizeF(frame.height() * pictureAspect, frame.height());
}

} // namespace

QSize reframeSize(double aspect, QSize source) {
    // The part of the source that fills the new frame decides: 2160 wide
    // when it has that much detail, else the usual 1080.
    const double cut = std::min<double>(source.width(), source.height() * aspect);
    const int w = cut >= 2160 ? 2160 : 1080;
    return {w, static_cast<int>(std::lround(w / aspect / 2.0)) * 2};
}

double coverScale(double pictureAspect, QSize frame) {
    if (pictureAspect <= 0.0 || frame.isEmpty())
        return 1.0;
    const QSizeF fit = fitSize(pictureAspect, frame);
    return std::max(frame.width() / fit.width(), frame.height() / fit.height());
}

MotionPath cameraPath(const std::vector<SubjectSample>& samples, double pictureAspect, QSize frame, double deadZone) {
    MotionPath path;
    if (samples.empty() || frame.isEmpty())
        return path;
    const double scale = coverScale(pictureAspect, frame);
    const QSizeF fit = fitSize(pictureAspect, frame);
    // Picture size in frames.
    const double pw = fit.width() * scale / frame.width(), ph = fit.height() * scale / frame.height();
    const double maxX = std::max(0.0, (pw - 1.0) / 2.0), maxY = std::max(0.0, (ph - 1.0) / 2.0);
    auto offset = [&](QPointF c) {
        // The subject's centre to the middle of the frame across, and a
        // little above the middle (a face in the upper part of the picture).
        const double x = std::clamp(-(c.x() - 0.5) * pw, -maxX, maxX);
        const double y = std::clamp(-0.08 - (c.y() - 0.5) * ph, -maxY, maxY);
        return QPointF(x, y);
    };

    QPointF cam(0.5, 0.5);
    int current = -1;
    for (const auto& s : samples)
        if (s.track >= 0) {
            cam = s.centre;
            current = s.track;
            break;
        }
    bool movingX = false, movingY = false;
    // Follows with a time constant of about half a second.
    const double follow = 1.0 - std::exp(-1.0 / (kSampleRate * 0.5));
    std::vector<MotionPoint> points;
    for (size_t i = 0; i < samples.size(); ++i) {
        const SubjectSample& s = samples[i];
        if (s.track >= 0 && current >= 0 && s.track != current) {
            // Another person takes over: cut, do not pan.
            const QPointF before = offset(cam);
            if (!points.empty() && s.time - 1 > points.back().time)
                points.push_back({s.time - 1, before.x(), before.y()});
            cam = s.centre;
            movingX = movingY = false;
        } else if (s.track >= 0) {
            const double dx = (s.centre.x() - cam.x()) * pw, dy = (s.centre.y() - cam.y()) * ph;
            movingX = std::abs(dx) > deadZone || (movingX && std::abs(dx) > deadZone / 4);
            movingY = std::abs(dy) > deadZone || (movingY && std::abs(dy) > deadZone / 4);
            if (movingX)
                cam.rx() += (s.centre.x() - cam.x()) * follow;
            if (movingY)
                cam.ry() += (s.centre.y() - cam.y()) * follow;
        }
        if (s.track >= 0)
            current = s.track;
        const QPointF o = offset(cam);
        points.push_back({s.time, o.x(), o.y()});
    }
    // Points on a straight line between their neighbours add nothing.
    std::vector<MotionPoint> kept;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i > 0 && i + 1 < points.size()) {
            const MotionPoint& a = kept.back();
            const MotionPoint& b = points[i + 1];
            const double f = static_cast<double>(points[i].time - a.time) / std::max<Ticks>(1, b.time - a.time);
            if (std::abs(a.x + (b.x - a.x) * f - points[i].x) < 1e-4 && std::abs(a.y + (b.y - a.y) * f - points[i].y) < 1e-4)
                continue;
        }
        kept.push_back(points[i]);
    }
    path.points = std::move(kept);
    return path;
}

ReframeResult findSubject(const QString& path, const Clip& clip, const Progress& progress) {
    ReframeResult r;
    if (!visionModelReady(QStringLiteral("face"))) {
        r.error = Tr::tr("The %1 model has not been downloaded yet.").arg(visionModel(QStringLiteral("face")).label);
        return r;
    }
    VideoDecoder dec;
    QString err;
    auto [from, to] = clip.mediaRange();
    if (!dec.open(path, &err) || to <= from || !dec.seek(from)) {
        r.error = err.isEmpty() ? Tr::tr("The video cannot be read.") : err;
        return r;
    }
    const int rotation = dec.info().rotation;
    auto localTime = [&](Ticks media) {
        const Ticks source = clip.hasSpeedChange() ? clip.remap.unmap(media) : media;
        return std::clamp<Ticks>(source - clip.sourceIn, 0, clip.duration());
    };

    struct Person {
        int id;
        QRectF box; // fractions of the upright picture
        Ticks seen;
    };
    std::vector<Person> people;
    int nextId = 0, main = -1;
    FrameConverter conv;
    DecodedFrame f;
    Ticks last = kInvalidTicks;
    const Ticks step = static_cast<Ticks>(kTicksPerSecond / kSampleRate);
    QString detectError;
    while (dec.next(f)) {
        if (f.pts + std::max<Ticks>(1, f.duration) <= from)
            continue;
        if (f.pts >= to)
            break;
        if (last != kInvalidTicks && f.pts - last < step)
            continue;
        last = f.pts;
        // Small enough to be quick; people talking to a camera are big.
        QImage img = conv.convert(f.frame.get(), QSize(960, 960));
        if (img.isNull())
            continue;
        if (rotation != 0)
            img = img.transformed(QTransform().rotate(rotation));
        const auto faces = detectFaces(img, 0.6f, &detectError);
        if (!detectError.isEmpty()) {
            r.error = detectError;
            return r;
        }
        // Follow each person from look to look by where their face was.
        std::vector<int> ids;
        for (const auto& d : faces) {
            const QRectF box(d.rect.x() / img.width(), d.rect.y() / img.height(), d.rect.width() / img.width(),
                             d.rect.height() / img.height());
            Person* match = nullptr;
            double bestDist = 1e9;
            for (auto& p : people) {
                const double dist = std::hypot(p.box.center().x() - box.center().x(), p.box.center().y() - box.center().y());
                if (f.pts - p.seen < 2 * kTicksPerSecond && dist < std::max(p.box.width(), box.width()) && dist < bestDist &&
                    std::find(ids.begin(), ids.end(), p.id) == ids.end()) {
                    bestDist = dist;
                    match = &p;
                }
            }
            if (!match) {
                people.push_back({nextId++, box, f.pts});
                match = &people.back();
            }
            match->box = box;
            match->seen = f.pts;
            ids.push_back(match->id);
        }
        // The largest face leads; the lead changes only to a face that is
        // clearly larger (or when the leader is gone).
        auto area = [&](int id) {
            for (const auto& p : people)
                if (p.id == id && p.seen == f.pts)
                    return p.box.width() * p.box.height();
            return 0.0;
        };
        int largest = -1;
        for (int id : ids)
            if (largest < 0 || area(id) > area(largest))
                largest = id;
        if (largest >= 0 && (main < 0 || area(main) < 0.6 * area(largest)))
            main = largest;
        SubjectSample s;
        s.time = localTime(f.pts);
        if (main >= 0 && area(main) > 0.0) {
            for (const auto& p : people)
                if (p.id == main)
                    s.centre = p.box.center();
            s.track = main;
        }
        r.samples.push_back(s);
        if (progress && !progress(static_cast<double>(f.pts - from) / (to - from))) {
            r.cancelled = true;
            return r;
        }
    }
    std::sort(r.samples.begin(), r.samples.end(), [](const SubjectSample& a, const SubjectSample& b) { return a.time < b.time; });
    r.faces = nextId;
    return r;
}

} // namespace lumora::ai
