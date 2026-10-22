#include "GifExport.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ffmpeg/FFmpeg.h"
#include "media/MediaPool.h"
#include "render/FrameRenderer.h"

extern "C" {
#include <libavutil/opt.h>
}

namespace lumora::gif {

namespace {

class Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::GifExport)
};

// --- median cut -------------------------------------------------------------

constexpr int kBits = 5;
constexpr int kLevels = 1 << kBits;

struct Bin {
    std::uint64_t count = 0;
    std::uint64_t r = 0, g = 0, b = 0;
};

inline int binIndex(int r, int g, int b) {
    return ((r >> (8 - kBits)) << (2 * kBits)) | ((g >> (8 - kBits)) << kBits) | (b >> (8 - kBits));
}

struct Box {
    std::vector<int> bins; // indices of non-empty bins
    std::uint64_t count = 0;
    int lo[3] = {kLevels, kLevels, kLevels};
    int hi[3] = {-1, -1, -1};

    void measure(const std::vector<Bin>& h) {
        count = 0;
        for (int i = 0; i < 3; ++i) {
            lo[i] = kLevels;
            hi[i] = -1;
        }
        for (int i : bins) {
            count += h[i].count;
            const int c[3] = {i >> (2 * kBits), (i >> kBits) & (kLevels - 1), i & (kLevels - 1)};
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], c[k]);
                hi[k] = std::max(hi[k], c[k]);
            }
        }
    }
    int longestAxis() const {
        // Green counts most for the eye, blue least.
        const double w[3] = {1.0, 1.2, 0.8};
        int best = 0;
        for (int k = 1; k < 3; ++k)
            if ((hi[k] - lo[k]) * w[k] > (hi[best] - lo[best]) * w[best])
                best = k;
        return best;
    }
    int span() const { return std::max({hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]}); }
};

inline int axisValue(int bin, int axis) {
    return axis == 0 ? bin >> (2 * kBits) : axis == 1 ? (bin >> kBits) & (kLevels - 1) : bin & (kLevels - 1);
}

// Nearest palette entry, cached per 6-bit colour.
class Mapper {
public:
    explicit Mapper(const QVector<QRgb>& palette) : m_palette(palette), m_cache(1 << 18, -1) {}

    int nearest(int r, int g, int b) {
        const int key = ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2);
        int& slot = m_cache[key];
        if (slot >= 0)
            return slot;
        int best = 0;
        long bestD = std::numeric_limits<long>::max();
        for (int i = 0; i < m_palette.size(); ++i) {
            const QRgb p = m_palette[i];
            const long dr = qRed(p) - r, dg = qGreen(p) - g, db = qBlue(p) - b;
            const long d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        slot = best;
        return best;
    }

private:
    const QVector<QRgb>& m_palette;
    std::vector<int> m_cache;
};

} // namespace

QVector<QRgb> makePalette(const QVector<QImage>& samples, int colors) {
    colors = std::clamp(colors, 2, 256);
    std::vector<Bin> hist(kLevels * kLevels * kLevels);
    for (const QImage& sample : samples) {
        if (sample.isNull())
            continue;
        const QImage img = sample.convertToFormat(QImage::Format_RGB32);
        // Every pixel of small pictures; a grid of about 200 000 of large ones.
        const int step = std::max(1, int(std::sqrt(double(img.width()) * img.height() / 200000.0)));
        for (int y = 0; y < img.height(); y += step) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); x += step) {
                const int r = qRed(row[x]), g = qGreen(row[x]), b = qBlue(row[x]);
                Bin& bin = hist[binIndex(r, g, b)];
                ++bin.count;
                bin.r += r;
                bin.g += g;
                bin.b += b;
            }
        }
    }
    std::vector<Box> boxes(1);
    for (int i = 0; i < int(hist.size()); ++i)
        if (hist[i].count)
            boxes[0].bins.push_back(i);
    if (boxes[0].bins.empty())
        return {qRgb(0, 0, 0), qRgb(255, 255, 255)};
    boxes[0].measure(hist);

    while (int(boxes.size()) < colors) {
        // Split the box that matters most: many pixels over a wide range.
        int pick = -1;
        double best = 0;
        for (int i = 0; i < int(boxes.size()); ++i) {
            if (boxes[i].bins.size() < 2)
                continue;
            const double score = std::sqrt(double(boxes[i].count)) * (boxes[i].span() + 1);
            if (score > best) {
                best = score;
                pick = i;
            }
        }
        if (pick < 0)
            break;
        Box& box = boxes[pick];
        const int axis = box.longestAxis();
        std::sort(box.bins.begin(), box.bins.end(), [axis](int a, int b) { return axisValue(a, axis) < axisValue(b, axis); });
        // Median by pixel count, keeping at least one bin on each side.
        std::uint64_t half = box.count / 2, run = 0;
        size_t cut = 1;
        for (size_t i = 0; i < box.bins.size() - 1; ++i) {
            run += hist[box.bins[i]].count;
            cut = i + 1;
            if (run >= half)
                break;
        }
        Box other;
        other.bins.assign(box.bins.begin() + cut, box.bins.end());
        box.bins.resize(cut);
        box.measure(hist);
        other.measure(hist);
        boxes.push_back(std::move(other));
    }

    QVector<QRgb> palette;
    for (const Box& box : boxes) {
        std::uint64_t n = 0, r = 0, g = 0, b = 0;
        for (int i : box.bins) {
            n += hist[i].count;
            r += hist[i].r;
            g += hist[i].g;
            b += hist[i].b;
        }
        if (n)
            palette.push_back(qRgb(int((r + n / 2) / n), int((g + n / 2) / n), int((b + n / 2) / n)));
    }
    return palette;
}

std::vector<std::uint8_t> quantize(const QImage& image, const QVector<QRgb>& palette, bool dither) {
    const QImage img = image.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    std::vector<std::uint8_t> out(size_t(w) * h);
    if (palette.isEmpty())
        return out;
    Mapper mapper(palette);
    if (!dither) {
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x)
                out[size_t(y) * w + x] = std::uint8_t(mapper.nearest(qRed(row[x]), qGreen(row[x]), qBlue(row[x])));
        }
        return out;
    }
    // Serpentine Floyd-Steinberg; the error is damped a little so flat areas
    // do not crawl from frame to frame.
    std::vector<std::array<float, 3>> cur(w + 2), next(w + 2);
    for (int y = 0; y < h; ++y) {
        std::fill(next.begin(), next.end(), std::array<float, 3>{0, 0, 0});
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const bool ltr = (y % 2) == 0;
        for (int i = 0; i < w; ++i) {
            const int x = ltr ? i : w - 1 - i;
            const int dir = ltr ? 1 : -1;
            float c[3] = {qRed(row[x]) + cur[x + 1][0], qGreen(row[x]) + cur[x + 1][1], qBlue(row[x]) + cur[x + 1][2]};
            int v[3];
            for (int k = 0; k < 3; ++k)
                v[k] = std::clamp(int(std::lround(c[k])), 0, 255);
            const int idx = mapper.nearest(v[0], v[1], v[2]);
            out[size_t(y) * w + x] = std::uint8_t(idx);
            const QRgb p = palette[idx];
            const float err[3] = {(c[0] - qRed(p)) * 0.85f, (c[1] - qGreen(p)) * 0.85f, (c[2] - qBlue(p)) * 0.85f};
            for (int k = 0; k < 3; ++k) {
                cur[x + 1 + dir][k] += err[k] * 7.f / 16.f;
                next[x + 1 - dir][k] += err[k] * 3.f / 16.f;
                next[x + 1][k] += err[k] * 5.f / 16.f;
                next[x + 1 + dir][k] += err[k] * 1.f / 16.f;
            }
        }
        std::swap(cur, next);
    }
    return out;
}

namespace {

class Writer {
public:
    bool open(const QString& path, QSize size, int loops, QString* error) {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
        if (!codec)
            return fail(error, Tr::tr("This FFmpeg has no GIF encoder."));
        m_enc.reset(avcodec_alloc_context3(codec));
        m_enc->width = size.width();
        m_enc->height = size.height();
        m_enc->pix_fmt = AV_PIX_FMT_PAL8;
        m_enc->time_base = {1, 100};
        int ret = avcodec_open2(m_enc.get(), codec, nullptr);
        if (ret < 0)
            return fail(error, Tr::tr("Cannot start the GIF encoder: %1").arg(av::errorString(ret)));

        const std::string out = av::toFFmpegPath(path);
        AVFormatContext* raw = nullptr;
        ret = avformat_alloc_output_context2(&raw, nullptr, "gif", out.c_str());
        if (ret < 0 || !raw)
            return fail(error, Tr::tr("Cannot create the output file: %1").arg(av::errorString(ret)));
        m_out.reset(raw);
        m_stream = avformat_new_stream(m_out.get(), nullptr);
        avcodec_parameters_from_context(m_stream->codecpar, m_enc.get());
        m_stream->time_base = m_enc->time_base;
        ret = avio_open(&m_out->pb, out.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
            return fail(error, Tr::tr("Cannot write %1: %2").arg(path, av::errorString(ret)));
        AVDictionary* opts = nullptr;
        // The muxer counts repeats: 0 forever, -1 play once.
        av_dict_set_int(&opts, "loop", loops <= 0 ? 0 : loops == 1 ? -1 : loops - 1, 0);
        ret = avformat_write_header(m_out.get(), &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return fail(error, Tr::tr("Cannot write the file header: %1").arg(av::errorString(ret)));
        m_frame = av::makeFrame();
        m_frame->format = AV_PIX_FMT_PAL8;
        m_frame->width = size.width();
        m_frame->height = size.height();
        if (av_frame_get_buffer(m_frame.get(), 0) < 0)
            return fail(error, Tr::tr("Out of memory."));
        m_packet = av::makePacket();
        return true;
    }

    bool write(const std::vector<std::uint8_t>& indices, const QVector<QRgb>& palette, int delay, QString* error) {
        if (av_frame_make_writable(m_frame.get()) < 0)
            return fail(error, Tr::tr("Out of memory."));
        const int w = m_frame->width;
        for (int y = 0; y < m_frame->height; ++y)
            std::copy_n(indices.data() + size_t(y) * w, w, m_frame->data[0] + size_t(y) * m_frame->linesize[0]);
        auto* pal = reinterpret_cast<std::uint32_t*>(m_frame->data[1]);
        for (int i = 0; i < 256; ++i)
            pal[i] = i < palette.size() ? (0xff000000u | palette[i]) : 0xff000000u;
        m_frame->pts = m_pts;
        m_delays.push_back(delay);
        m_pts += delay;
        return send(m_frame.get(), error);
    }

    bool finish(QString* error) {
        if (!send(nullptr, error))
            return false;
        const int ret = av_write_trailer(m_out.get());
        if (ret < 0)
            return fail(error, Tr::tr("Cannot finish the file: %1").arg(av::errorString(ret)));
        m_out.reset();
        return true;
    }

private:
    bool send(AVFrame* frame, QString* error) {
        int ret = avcodec_send_frame(m_enc.get(), frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return fail(error, Tr::tr("GIF encoding failed: %1").arg(av::errorString(ret)));
        while ((ret = avcodec_receive_packet(m_enc.get(), m_packet.get())) >= 0) {
            if (m_written < m_delays.size())
                m_packet->duration = m_delays[m_written];
            ++m_written;
            av_packet_rescale_ts(m_packet.get(), m_enc->time_base, m_stream->time_base);
            m_packet->stream_index = m_stream->index;
            ret = av_interleaved_write_frame(m_out.get(), m_packet.get());
            if (ret < 0)
                return fail(error, Tr::tr("Cannot write the file: %1").arg(av::errorString(ret)));
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            return fail(error, Tr::tr("GIF encoding failed: %1").arg(av::errorString(ret)));
        return true;
    }

    static bool fail(QString* error, const QString& text) {
        if (error)
            *error = text;
        return false;
    }

    av::CodecContextPtr m_enc;
    av::FormatOutputPtr m_out;
    AVStream* m_stream = nullptr;
    av::FramePtr m_frame;
    av::PacketPtr m_packet;
    std::int64_t m_pts = 0;
    std::vector<int> m_delays;
    size_t m_written = 0;
};

} // namespace

bool writeFrames(const QVector<QImage>& frames, const QVector<int>& delays, const GifSettings& settings, const QString& path,
                 QString* error) {
    if (frames.isEmpty())
        return false;
    const QVector<QRgb> palette = makePalette(frames, settings.colors);
    Writer writer;
    if (!writer.open(path, frames.front().size(), settings.loops, error))
        return false;
    for (int i = 0; i < frames.size(); ++i)
        if (!writer.write(quantize(frames[i], palette, settings.dither), palette, std::max(2, delays.value(i, 10)), error))
            return false;
    return writer.finish(error);
}

bool exportTimeline(const TimelineState& timeline, const MediaPool& pool, const GifSettings& settings, const QString& path,
                    const std::function<void(double)>& progress, const std::atomic_bool* cancel, QString* error) {
    if (settings.to <= settings.from) {
        if (error)
            *error = Tr::tr("The part to export is empty.");
        return false;
    }
    FrameRenderer r(&pool);
    r.setTimeline(timeline);
    const QSize canvas = FrameRenderer::defaultCanvasSize(timeline, pool);
    r.setCanvasSize(canvas);
    const int w = std::clamp(settings.width, 16, canvas.width()) & ~1;
    const QSize size(w, std::max(2, int(std::lround(double(canvas.height()) * w / std::max(1, canvas.width())))));
    r.setOutputSize(size);
    r.setPrecision(FrameRenderer::Precision::Preview);

    const double fps = std::clamp(settings.fps, 1.0, 50.0);
    const double seconds = ticksToSeconds(settings.to - settings.from);
    const int count = std::max(1, int(std::ceil(seconds * fps - 1e-6)));
    auto timeOf = [&](int i) { return settings.from + ticksFromSeconds(i / fps); };
    auto frameOf = [&](int i) { return r.render(timeOf(i)).convertToFormat(QImage::Format_RGB32); };

    // One palette for the whole animation, from frames spread over it.
    QVector<QImage> samples;
    const int sampleCount = std::min(count, 16);
    for (int k = 0; k < sampleCount; ++k) {
        if (cancel && *cancel)
            return false;
        samples.push_back(frameOf(int(std::lround(double(k) * (count - 1) / std::max(1, sampleCount - 1)))));
        if (progress)
            progress(0.15 * (k + 1) / sampleCount);
    }
    const QVector<QRgb> palette = makePalette(samples, settings.colors);
    samples.clear();

    Writer writer;
    if (!writer.open(path, size, settings.loops, error))
        return false;
    for (int i = 0; i < count; ++i) {
        if (cancel && *cancel)
            return false;
        // Delays in 1/100 s, rounded so they add up to the right length.
        const double end = std::min(seconds, (i + 1) / fps);
        const int delay = std::max(2, int(std::lround(end * 100)) - int(std::lround(i / fps * 100)));
        if (!writer.write(quantize(frameOf(i), palette, settings.dither), palette, delay, error))
            return false;
        if (progress)
            progress(0.15 + 0.85 * (i + 1) / count);
    }
    return writer.finish(error);
}

} // namespace lumora::gif
