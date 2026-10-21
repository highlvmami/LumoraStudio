#include "HeifDecoder.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "ffmpeg/FFmpeg.h"
#include "media/MediaInfo.h"

extern "C" {
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
}

// Tile grids (how phones store a photo) are exposed by FFmpeg 7.1 and newer.
#define LUMORA_HEIF_TILE_GRID (LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(61, 7, 100))

namespace lumora::heif {

QStringList fileExtensions() {
    return {"heic", "heif", "hif", "avif"};
}

bool isHeifFile(const QString& path) {
    return fileExtensions().contains(QFileInfo(path).suffix().toLower());
}

namespace {

// Where each coded picture goes on the canvas. A plain photo is one picture
// at 0,0; a phone photo is a grid of tiles.
struct Layout {
    struct Tile {
        int stream = 0;
        int x = 0, y = 0;
    };
    std::vector<Tile> tiles;
    QSize canvas;
    QRect visible; // the part of the canvas that is the photo
    const AVStream* first = nullptr;
    const AVPacketSideData* sideData = nullptr;
    int sideDataCount = 0;
};

const AVPacketSideData* findSideData(const Layout& l, AVPacketSideDataType type) {
    if (const AVPacketSideData* sd = av_packet_side_data_get(l.sideData, l.sideDataCount, type))
        return sd;
    return av_packet_side_data_get(l.first->codecpar->coded_side_data, l.first->codecpar->nb_coded_side_data,
                                   type);
}

bool findLayout(AVFormatContext* fmt, Layout* l, QString* error) {
#if LUMORA_HEIF_TILE_GRID
    const AVStreamGroup* grid = nullptr;
    for (unsigned i = 0; i < fmt->nb_stream_groups; ++i) {
        const AVStreamGroup* g = fmt->stream_groups[i];
        if (g->type != AV_STREAM_GROUP_PARAMS_TILE_GRID || g->nb_streams == 0)
            continue;
        if (!grid ||
            ((g->disposition & AV_DISPOSITION_DEFAULT) && !(grid->disposition & AV_DISPOSITION_DEFAULT)))
            grid = g;
    }
    if (grid) {
        const AVStreamGroupTileGrid* tg = grid->params.tile_grid;
        for (unsigned t = 0; t < tg->nb_tiles; ++t) {
            const unsigned idx = tg->offsets[t].idx;
            if (idx < grid->nb_streams)
                l->tiles.push_back(
                    {grid->streams[idx]->index, tg->offsets[t].horizontal, tg->offsets[t].vertical});
        }
        l->canvas = QSize(tg->coded_width, tg->coded_height);
        l->visible = QRect(tg->horizontal_offset, tg->vertical_offset, tg->width, tg->height) &
                     QRect(QPoint(), l->canvas);
        l->first = grid->streams[0];
        l->sideData = tg->coded_side_data;
        l->sideDataCount = tg->nb_coded_side_data;
        if (l->tiles.empty() || l->visible.isEmpty()) {
            if (error)
                *error = QCoreApplication::translate("lumora::heif", "The photo's tile layout is damaged.");
            return false;
        }
        return true;
    }
#endif
    // A single picture: the primary one is marked default; thumbnails and
    // depth maps are not.
    const AVStream* best = nullptr;
    int videoStreams = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream* st = fmt->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
            continue;
        ++videoStreams;
        if (!best ||
            ((st->disposition & AV_DISPOSITION_DEFAULT) && !(best->disposition & AV_DISPOSITION_DEFAULT)))
            best = st;
    }
#if !LUMORA_HEIF_TILE_GRID
    // Older FFmpeg lists a phone photo's tiles as separate pictures; showing
    // one tile would look like a broken photo.
    if (videoStreams > 3) {
        if (error)
            *error = QCoreApplication::translate(
                "lumora::heif", "This photo is stored in tiles, which needs FFmpeg 7.1 or newer.");
        return false;
    }
#endif
    if (!best || best->codecpar->width <= 0 || best->codecpar->height <= 0) {
        if (error)
            *error = QCoreApplication::translate("lumora::heif", "The file holds no picture.");
        return false;
    }
    l->tiles.push_back({best->index, 0, 0});
    l->canvas = QSize(best->codecpar->width, best->codecpar->height);
    l->visible = QRect(QPoint(), l->canvas);
    l->first = best;
    return true;
}

// Clockwise degrees and mirroring from the display matrix, the way the
// ffmpeg tool applies them.
struct Orientation {
    int rotation = 0;       // 0, 90, 180, 270 clockwise
    bool transpose = false; // with 90 / 270: mirrored along the diagonal
    bool hflip = false, vflip = false;
};

Orientation orientationOf(const Layout& l) {
    Orientation o;
    const AVPacketSideData* sd = findSideData(l, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * 4)
        return o;
    const auto* m = reinterpret_cast<const int32_t*>(sd->data);
    const double angle = av_display_rotation_get(m);
    if (std::isnan(angle))
        return o;
    int deg = static_cast<int>(std::lround(-angle / 90.0)) * 90 % 360;
    if (deg < 0)
        deg += 360;
    o.rotation = deg;
    if (deg == 90)
        o.transpose = m[3] > 0;
    else if (deg == 270)
        o.transpose = m[3] < 0;
    else {
        o.hflip = m[0] < 0;
        o.vflip = m[4] < 0;
        o.rotation = 0;
        if (o.hflip && o.vflip) {
            o.hflip = o.vflip = false;
            o.rotation = 180;
        }
    }
    return o;
}

QImage applyOrientation(QImage img, const Orientation& o) {
    if (o.rotation)
        img = img.transformed(QTransform().rotate(o.rotation));
    if (o.transpose)
        img = o.rotation == 90 ? img.mirrored(true, false) : img.mirrored(false, true);
    if (o.hflip || o.vflip)
        img = img.mirrored(o.hflip, o.vflip);
    return img;
}

QColorSpace colorSpaceOf(const Layout& l, const AVFrame* f) {
    if (const AVPacketSideData* sd = findSideData(l, AV_PKT_DATA_ICC_PROFILE)) {
        const QColorSpace cs = QColorSpace::fromIccProfile(
            QByteArray(reinterpret_cast<const char*>(sd->data), static_cast<qsizetype>(sd->size)));
        if (cs.isValid())
            return cs;
    }
    switch (f->color_primaries) {
    case AVCOL_PRI_SMPTE432:
        return QColorSpace(QColorSpace::DisplayP3);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    case AVCOL_PRI_BT2020:
        return QColorSpace(QColorSpace::Bt2020);
#endif
    default:
        return QColorSpace(QColorSpace::SRgb);
    }
}

int swsColorspace(const AVFrame* f) {
    switch (f->colorspace) {
    case AVCOL_SPC_BT709:
        return SWS_CS_ITU709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return SWS_CS_BT2020;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    default:
        return SWS_CS_ITU601; // the HEIF default
    }
}

// Copies a decoded tile into the canvas frame (same pixel format).
void placeTile(AVFrame* canvas, const AVFrame* tile, int x, int y) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(canvas->format));
    const int planes = av_pix_fmt_count_planes(static_cast<AVPixelFormat>(canvas->format));
    for (int p = 0; p < planes; ++p) {
        int bytes = 1;
        for (int c = 0; c < d->nb_components; ++c)
            if (d->comp[c].plane == p)
                bytes = d->comp[c].step;
        const bool chroma = (p == 1 || p == 2) && !(d->flags & AV_PIX_FMT_FLAG_RGB);
        const int sx = chroma ? d->log2_chroma_w : 0, sy = chroma ? d->log2_chroma_h : 0;
        const int px = x >> sx, py = y >> sy;
        const int cw = AV_CEIL_RSHIFT(canvas->width, sx), ch = AV_CEIL_RSHIFT(canvas->height, sy);
        const int w = std::min(AV_CEIL_RSHIFT(tile->width, sx), cw - px);
        const int h = std::min(AV_CEIL_RSHIFT(tile->height, sy), ch - py);
        if (w <= 0 || h <= 0)
            continue;
        for (int row = 0; row < h; ++row)
            std::copy_n(tile->data[p] + static_cast<ptrdiff_t>(row) * tile->linesize[p],
                        static_cast<size_t>(w) * bytes,
                        canvas->data[p] + static_cast<ptrdiff_t>(py + row) * canvas->linesize[p] +
                            static_cast<ptrdiff_t>(px) * bytes);
    }
}

av::FramePtr decodeOne(const AVStream* st, const AVPacket* pkt, QString* error) {
    auto dec = av::openDecoder(st, error, 1);
    if (!dec)
        return nullptr;
    auto frame = av::makeFrame();
    int ret = avcodec_send_packet(dec.get(), pkt);
    if (ret >= 0)
        ret = avcodec_send_packet(dec.get(), nullptr);
    ret = avcodec_receive_frame(dec.get(), frame.get());
    if (ret < 0) {
        if (error)
            *error = av::errorString(ret);
        return nullptr;
    }
    return frame;
}

QImage decodeFFmpeg(const QString& path, QString* error) {
    auto fmt = av::openInput(path, error);
    if (!fmt)
        return {};
    Layout l;
    if (!findLayout(fmt.get(), &l, error))
        return {};

    std::multimap<int, std::pair<int, int>> where; // stream -> tile position
    for (const auto& t : l.tiles)
        where.emplace(t.stream, std::make_pair(t.x, t.y));

    av::FramePtr canvas;
    size_t placed = 0;
    auto pkt = av::makePacket();
    while (placed < l.tiles.size() && av_read_frame(fmt.get(), pkt.get()) >= 0) {
        const auto range = where.equal_range(pkt->stream_index);
        if (range.first == range.second) {
            av_packet_unref(pkt.get());
            continue;
        }
        av::FramePtr tile = decodeOne(fmt->streams[pkt->stream_index], pkt.get(), error);
        av_packet_unref(pkt.get());
        if (!tile)
            return {};
        if (l.tiles.size() == 1) {
            canvas = std::move(tile);
            placed = 1;
            break;
        }
        if (!canvas) {
            canvas = av::makeFrame();
            canvas->format = tile->format;
            canvas->width = l.canvas.width();
            canvas->height = l.canvas.height();
            if (av_frame_get_buffer(canvas.get(), 0) < 0) {
                if (error)
                    *error = QCoreApplication::translate("lumora::heif", "Not enough memory for the photo.");
                return {};
            }
            // Grids leave no gaps, but start from black just in case.
            ptrdiff_t lines[4];
            for (int i = 0; i < 4; ++i)
                lines[i] = canvas->linesize[i];
            av_image_fill_black(canvas->data, lines, static_cast<AVPixelFormat>(canvas->format),
                                tile->color_range, canvas->width, canvas->height);
            av_frame_copy_props(canvas.get(), tile.get());
        } else if (tile->format != canvas->format) {
            if (error)
                *error = QCoreApplication::translate("lumora::heif", "The photo's tiles do not match.");
            return {};
        }
        for (auto it = range.first; it != range.second; ++it, ++placed)
            placeTile(canvas.get(), tile.get(), it->second.first, it->second.second);
    }
    if (!canvas) {
        if (error && error->isEmpty())
            *error = QCoreApplication::translate("lumora::heif", "The file holds no picture.");
        return {};
    }

    // Full-range "J" formats are the plain ones with the range set below.
    auto srcFmt = static_cast<AVPixelFormat>(canvas->format);
    const bool fullRange = canvas->color_range == AVCOL_RANGE_JPEG || srcFmt == AV_PIX_FMT_YUVJ420P ||
                           srcFmt == AV_PIX_FMT_YUVJ422P || srcFmt == AV_PIX_FMT_YUVJ444P ||
                           srcFmt == AV_PIX_FMT_YUVJ440P;
    switch (srcFmt) {
    case AV_PIX_FMT_YUVJ420P:
        srcFmt = AV_PIX_FMT_YUV420P;
        break;
    case AV_PIX_FMT_YUVJ422P:
        srcFmt = AV_PIX_FMT_YUV422P;
        break;
    case AV_PIX_FMT_YUVJ444P:
        srcFmt = AV_PIX_FMT_YUV444P;
        break;
    case AV_PIX_FMT_YUVJ440P:
        srcFmt = AV_PIX_FMT_YUV440P;
        break;
    default:
        break;
    }
    av::SwsPtr sws(sws_getContext(canvas->width, canvas->height, srcFmt, canvas->width, canvas->height,
                                  AV_PIX_FMT_RGBA64, SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                                  nullptr, nullptr, nullptr));
    if (!sws) {
        if (error)
            *error = QCoreApplication::translate("lumora::heif", "Unsupported picture format.");
        return {};
    }
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(srcFmt);
    if (!(d->flags & AV_PIX_FMT_FLAG_RGB))
        sws_setColorspaceDetails(sws.get(), sws_getCoefficients(swsColorspace(canvas.get())),
                                 fullRange ? 1 : 0, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16,
                                 1 << 16);
    const bool alpha = d->flags & AV_PIX_FMT_FLAG_ALPHA;
    QImage img(canvas->width, canvas->height, alpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64);
    if (img.isNull()) {
        if (error)
            *error = QCoreApplication::translate("lumora::heif", "Not enough memory for the photo.");
        return {};
    }
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), canvas->data, canvas->linesize, 0, canvas->height, dst, stride);

    if (l.visible != img.rect())
        img = img.copy(l.visible);
    img = applyOrientation(img, orientationOf(l));
    img.setColorSpace(colorSpaceOf(l, canvas.get()));
    return img;
}

QImage decodeQt(const QString& path, QString* error) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(2048);
    QImage img = reader.read();
    if (img.isNull() && error)
        *error = reader.errorString();
    return img;
}

} // namespace

bool probe(const QString& path, Info* info, QString* error) {
    QString err;
    if (auto fmt = av::openInput(path, &err)) {
        Layout l;
        if (findLayout(fmt.get(), &l, &err)) {
            const Orientation o = orientationOf(l);
            QSize size = l.visible.size();
            if (o.rotation == 90 || o.rotation == 270)
                size.transpose();
            const AVCodecParameters* par = l.first->codecpar;
            info->size = size;
            info->bitDepth = par->bits_per_raw_sample > 0
                                 ? par->bits_per_raw_sample
                                 : bitDepthOf(static_cast<AVPixelFormat>(par->format));
            info->codec = QString::fromUtf8(avcodec_get_name(par->codec_id));
            return true;
        }
    }
    // Some systems have a Qt plugin for these files.
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (reader.canRead()) {
        info->size = reader.size();
        info->bitDepth = 8;
        info->codec = QString::fromLatin1(reader.format());
        return true;
    }
    if (error)
        *error = err.isEmpty()
                     ? QCoreApplication::translate("lumora::heif", "This photo format cannot be opened.")
                     : err;
    return false;
}

QImage decode(const QString& path, QString* error) {
    QString err;
    QImage img = decodeFFmpeg(path, &err);
    if (!img.isNull())
        return img;
    img = decodeQt(path, nullptr);
    if (img.isNull() && error)
        *error = err.isEmpty()
                     ? QCoreApplication::translate("lumora::heif", "This photo format cannot be opened.")
                     : err;
    return img;
}

QImage thumbnail(const QString& path, QSize maxSize) {
    const QImage img = decode(path);
    if (img.isNull())
        return {};
    return img
        .scaled(img.size().scaled(maxSize, Qt::KeepAspectRatio), Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBX8888);
}

} // namespace lumora::heif
