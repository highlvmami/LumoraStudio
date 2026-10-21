#include <algorithm>
#include <cmath>
#include <cstring>
#include <QImageReader>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include "TestMedia.h"
#include "ffmpeg/FFmpeg.h"
extern "C" {
#include <libavutil/opt.h>
}
#include "image/HeifDecoder.h"
#include "image/PhotoLibrary.h"
#include "image/PhotoProcessor.h"
#include "image/RawDecoder.h"
#include "media/MediaInfo.h"

using namespace lumora;

namespace {
QImage makePhoto(int w, int h) {
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb((x * 255) / (w - 1), (y * 255) / (h - 1), (x * y) % 256));
    return img;
}

// A tiny linear DNG (already demosaiced, camera space = linear sRGB): enough
// for LibRaw to develop it like a real camera file. Left half a strong red,
// right half a mid grey.
QByteArray makeLinearDng(int w, int h) {
    QByteArray pixels;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const quint16 rgb[3] = {x < w / 2 ? quint16(30000) : quint16(9000),
                                    x < w / 2 ? quint16(1500) : quint16(9000),
                                    x < w / 2 ? quint16(1500) : quint16(9000)};
            pixels.append(reinterpret_cast<const char*>(rgb), 6); // little endian host assumed
        }
    struct Entry {
        quint16 tag, type;
        quint32 count;
        QByteArray data; // value bytes
    };
    auto shorts = [](std::initializer_list<quint16> v) {
        QByteArray b;
        for (quint16 x : v)
            b.append(reinterpret_cast<const char*>(&x), 2);
        return b;
    };
    auto longs = [](std::initializer_list<quint32> v) {
        QByteArray b;
        for (quint32 x : v)
            b.append(reinterpret_cast<const char*>(&x), 4);
        return b;
    };
    auto rationals = [](std::initializer_list<double> v) {
        QByteArray b;
        for (double x : v) {
            const qint32 n = qint32(std::lround(x * 10000)), d = 10000;
            b.append(reinterpret_cast<const char*>(&n), 4);
            b.append(reinterpret_cast<const char*>(&d), 4);
        }
        return b;
    };
    auto ascii = [](const char* s) { return QByteArray(s, int(std::strlen(s)) + 1); };
    const quint32 W = quint32(w), H = quint32(h);
    std::vector<Entry> e = {
        {254, 4, 1, longs({0})},
        {256, 4, 1, longs({W})},
        {257, 4, 1, longs({H})},
        {258, 3, 3, shorts({16, 16, 16})},
        {259, 3, 1, shorts({1})},
        {262, 3, 1, shorts({34892})}, // LinearRaw
        {271, 2, 7, ascii("Lumora")},
        {272, 2, 5, ascii("Test")},
        {273, 4, 1, longs({0})}, // strip offset, patched below
        {274, 3, 1, shorts({1})},
        {277, 3, 1, shorts({3})},
        {278, 4, 1, longs({H})},
        {279, 4, 1, longs({quint32(pixels.size())})},
        {284, 3, 1, shorts({1})},
        {50706, 1, 4, QByteArray("\x01\x04\x00\x00", 4)},
        {50708, 2, 12, ascii("Lumora Test")},
        {50717, 4, 1, longs({65535})},
        // XYZ (D65) to linear sRGB.
        {50721, 10, 9, rationals({3.2406, -1.5372, -0.4986, -0.9689, 1.8758, 0.0415, 0.0557, -0.2040, 1.0570})},
        {50728, 5, 3, rationals({1, 1, 1})},
        {50778, 3, 1, shorts({21})}, // D65
    };
    // Layout: header, IFD, out-of-line values, pixels.
    const quint32 ifdOffset = 8;
    const quint32 ifdSize = 2 + quint32(e.size()) * 12 + 4;
    QByteArray extra;
    const quint32 extraStart = ifdOffset + ifdSize;
    std::vector<quint32> where(e.size(), 0);
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i].data.size() > 4) {
            where[i] = extraStart + quint32(extra.size());
            extra.append(e[i].data);
            if (extra.size() % 2)
                extra.append('\0');
        }
    const quint32 pixelStart = extraStart + quint32(extra.size());
    for (auto& x : e)
        if (x.tag == 273)
            x.data = longs({pixelStart});
    QByteArray out("II*\0", 4);
    out.append(longs({ifdOffset}));
    out.append(shorts({quint16(e.size())}));
    for (size_t i = 0; i < e.size(); ++i) {
        out.append(shorts({e[i].tag, e[i].type}));
        out.append(longs({e[i].count}));
        if (e[i].data.size() > 4)
            out.append(longs({where[i]}));
        else
            out.append(e[i].data.leftJustified(4, '\0'));
    }
    out.append(longs({0}));
    out.append(extra);
    out.append(pixels);
    return out;
}

// A phone-style HEIC: a grid of HEVC tiles in solid colours, cropped to
// outW x outH and turned `ccw` quarter turns counter-clockwise (irot).
// Returns an empty array when no HEVC encoder is available.
QByteArray makeTiledHeic(const QString& scratch, int cols, int rows, int tile, const std::vector<QColor>& colors,
                         int outW, int outH, int ccw) {
    const AVCodec* codec = avcodec_find_encoder_by_name("libx265");
    if (!codec)
        codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    if (!codec)
        return {};
    av::CodecContextPtr enc(avcodec_alloc_context3(codec));
    enc->width = enc->height = tile;
    enc->pix_fmt = AV_PIX_FMT_YUVJ420P;
    enc->color_range = AVCOL_RANGE_JPEG;
    enc->colorspace = AVCOL_SPC_SMPTE170M;
    enc->time_base = AVRational{1, 25};
    enc->gop_size = 1;
    enc->max_b_frames = 0;
    enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(enc->priv_data, "x265-params", "log-level=error", 0);
    if (avcodec_open2(enc.get(), codec, nullptr) < 0)
        return {};

    // Annex B packets -> length-prefixed NAL units, as HEIF stores them.
    auto lengthPrefixed = [](const uint8_t* d, int n) {
        QByteArray out;
        int i = 0;
        auto startCode = [&](int at) { return at + 3 <= n && d[at] == 0 && d[at + 1] == 0 && d[at + 2] == 1; };
        while (i < n && !startCode(i))
            ++i;
        while (i < n) {
            i += 3;
            int j = i;
            while (j < n && !startCode(j))
                ++j;
            int end = j;
            while (end > i && d[end - 1] == 0 && j < n)
                --end;
            const quint32 len = quint32(end - i);
            const char be[4] = {char(len >> 24), char(len >> 16), char(len >> 8), char(len)};
            out.append(be, 4);
            out.append(reinterpret_cast<const char*>(d + i), end - i);
            i = j;
        }
        return out;
    };
    std::vector<QByteArray> tiles;
    auto pkt = av::makePacket();
    auto firstPacket = av::makePacket();
    auto drain = [&] {
        while (avcodec_receive_packet(enc.get(), pkt.get()) >= 0) {
            tiles.push_back(lengthPrefixed(pkt->data, pkt->size));
            if (tiles.size() == 1)
                av_packet_ref(firstPacket.get(), pkt.get());
            av_packet_unref(pkt.get());
        }
    };
    for (int t = 0; t < cols * rows; ++t) {
        auto f = av::makeFrame();
        f->format = enc->pix_fmt;
        f->width = f->height = tile;
        f->pts = t;
        av_frame_get_buffer(f.get(), 0);
        const QColor c = colors[size_t(t) % colors.size()];
        const int r = c.red(), g = c.green(), b = c.blue();
        const int Y = std::clamp(int(std::lround(0.299 * r + 0.587 * g + 0.114 * b)), 0, 255);
        const int U = std::clamp(int(std::lround(128 - 0.168736 * r - 0.331264 * g + 0.5 * b)), 0, 255);
        const int V = std::clamp(int(std::lround(128 + 0.5 * r - 0.418688 * g - 0.081312 * b)), 0, 255);
        for (int y = 0; y < tile; ++y)
            std::memset(f->data[0] + y * f->linesize[0], Y, size_t(tile));
        for (int y = 0; y < tile / 2; ++y) {
            std::memset(f->data[1] + y * f->linesize[1], U, size_t(tile / 2));
            std::memset(f->data[2] + y * f->linesize[2], V, size_t(tile / 2));
        }
        avcodec_send_frame(enc.get(), f.get());
        drain();
    }
    avcodec_send_frame(enc.get(), nullptr);
    drain();
    if (int(tiles.size()) != cols * rows)
        return {};

    // The decoder configuration (hvcC) is taken from a one-frame MP4.
    QByteArray hvcC;
    {
        const QString mp4 = scratch + QStringLiteral("/hvcc.mp4");
        AVFormatContext* oc = nullptr;
        avformat_alloc_output_context2(&oc, nullptr, "mp4", mp4.toUtf8().constData());
        AVStream* st = avformat_new_stream(oc, nullptr);
        avcodec_parameters_from_context(st->codecpar, enc.get());
        st->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        st->time_base = enc->time_base;
        avio_open(&oc->pb, mp4.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (avformat_write_header(oc, nullptr) >= 0) {
            firstPacket->stream_index = 0;
            av_packet_rescale_ts(firstPacket.get(), enc->time_base, st->time_base);
            av_interleaved_write_frame(oc, firstPacket.get());
            av_write_trailer(oc);
        }
        avio_closep(&oc->pb);
        avformat_free_context(oc);
        QFile f(mp4);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        const QByteArray all = f.readAll();
        const int at = all.indexOf("hvcC");
        if (at < 4)
            return {};
        const auto* p = reinterpret_cast<const uchar*>(all.constData() + at - 4);
        const int size = int(quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3]);
        hvcC = all.mid(at - 4, size);
    }

    auto u8 = [](QByteArray& b, int v) { b.append(char(v)); };
    auto u16 = [](QByteArray& b, int v) { b.append(char(v >> 8)).append(char(v)); };
    auto u32 = [](QByteArray& b, quint32 v) { b.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v)); };
    auto box = [&](const char* type, const QByteArray& body) {
        QByteArray b;
        u32(b, quint32(8 + body.size()));
        b.append(type, 4).append(body);
        return b;
    };
    auto fullBox = [&](const char* type, int version, int flags, const QByteArray& body) {
        QByteArray b;
        u8(b, version);
        u8(b, flags >> 16);
        u16(b, flags & 0xffff);
        return box(type, b + body);
    };

    const int n = cols * rows;
    const int gridId = n + 1;
    QByteArray gridData;
    u8(gridData, 0);
    u8(gridData, 0);
    u8(gridData, rows - 1);
    u8(gridData, cols - 1);
    u16(gridData, outW);
    u16(gridData, outH);

    auto buildMeta = [&](quint32 mdatStart) {
        QByteArray hdlr;
        u32(hdlr, 0);
        hdlr.append("pict", 4);
        u32(hdlr, 0), u32(hdlr, 0), u32(hdlr, 0);
        u8(hdlr, 0);
        QByteArray pitm;
        u16(pitm, gridId);
        QByteArray iloc;
        u8(iloc, 0x44);
        u8(iloc, 0x00);
        u16(iloc, n + 1);
        quint32 offset = mdatStart;
        for (int i = 0; i <= n; ++i) {
            const QByteArray& data = i < n ? tiles[size_t(i)] : gridData;
            u16(iloc, i + 1);
            u16(iloc, 0);
            u16(iloc, 1);
            u32(iloc, offset);
            u32(iloc, quint32(data.size()));
            offset += quint32(data.size());
        }
        QByteArray iinf;
        u16(iinf, n + 1);
        for (int i = 1; i <= n + 1; ++i) {
            QByteArray infe;
            u16(infe, i);
            u16(infe, 0);
            infe.append(i <= n ? "hvc1" : "grid", 4);
            u8(infe, 0);
            iinf.append(fullBox("infe", 2, i <= n ? 1 : 0, infe));
        }
        QByteArray dimg;
        u16(dimg, gridId);
        u16(dimg, n);
        for (int i = 1; i <= n; ++i)
            u16(dimg, i);
        QByteArray ispeTile, ispeGrid, irot;
        u32(ispeTile, quint32(tile)), u32(ispeTile, quint32(tile));
        u32(ispeGrid, quint32(outW)), u32(ispeGrid, quint32(outH));
        u8(irot, ccw & 3);
        // Properties: 1 hvcC, 2 tile ispe, 3 grid ispe, 4 irot.
        const QByteArray ipco = box("ipco", hvcC + fullBox("ispe", 0, 0, ispeTile) + fullBox("ispe", 0, 0, ispeGrid) +
                                                box("irot", irot));
        QByteArray ipma;
        u32(ipma, quint32(n + 1));
        for (int i = 1; i <= n; ++i) {
            u16(ipma, i);
            u8(ipma, 2);
            u8(ipma, 0x80 | 1);
            u8(ipma, 2);
        }
        u16(ipma, gridId);
        u8(ipma, 2);
        u8(ipma, 3);
        u8(ipma, 0x80 | 4);
        return fullBox("meta", 0, 0,
                       fullBox("hdlr", 0, 0, hdlr) + fullBox("pitm", 0, 0, pitm) + fullBox("iloc", 0, 0, iloc) +
                           fullBox("iinf", 0, 0, iinf) + fullBox("iref", 0, 0, box("dimg", dimg)) +
                           box("iprp", ipco + fullBox("ipma", 0, 0, ipma)));
    };
    QByteArray ftypBody("heic", 4);
    u32(ftypBody, 0);
    ftypBody.append("mif1heic", 8);
    const QByteArray ftyp = box("ftyp", ftypBody);
    const quint32 metaSize = quint32(buildMeta(0).size());
    const QByteArray meta = buildMeta(quint32(ftyp.size()) + metaSize + 8);
    QByteArray mdat;
    for (const auto& t : tiles)
        mdat.append(t);
    mdat.append(gridData);
    return ftyp + meta + box("mdat", mdat);
}
} // namespace

class PhotoTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_src;

private slots:
    void initTestCase() {
        m_src = m_dir.filePath("photo.png");
        QVERIFY(makePhoto(200, 100).save(m_src));
    }

    void stickerLayersTurnAndShowVideoFrames() {
        // A bright video frame as a sticker over a black photo.
        test::TestVideoSpec spec;
        spec.seconds = 1;
        spec.audio = false;
        spec.solidLuma = 220;
        QString err;
        const QString video = m_dir.filePath("bright.mp4");
        QVERIFY2(test::writeTestVideo(video, spec, &err), qPrintable(err));
        QImage black(400, 300, QImage::Format_RGBX64);
        black.fill(Qt::black);
        PhotoAdjustments adj;
        PhotoLayer l;
        l.id = QUuid::createUuid();
        l.kind = LayerKind::Image;
        l.imagePath = video;
        l.frameTime = ticksFromSeconds(0.5);
        l.scale = 0.5; // 200 x 150 in the middle
        adj.layers.push_back(l);
        QCOMPARE(PhotoProcessor::layerPictureSize(l), QSize(320, 240));
        QImage out = PhotoProcessor::apply(black, adj).convertToFormat(QImage::Format_RGB32);
        QVERIFY(qGray(out.pixel(200, 150)) > 200);
        QVERIFY(qGray(out.pixel(120, 150)) > 200);
        QVERIFY(qGray(out.pixel(80, 150)) < 10);
        // Turned a quarter: now tall instead of wide.
        adj.layers[0].rotation = 90;
        out = PhotoProcessor::apply(black, adj).convertToFormat(QImage::Format_RGB32);
        QVERIFY(qGray(out.pixel(120, 150)) < 10);
        QVERIFY(qGray(out.pixel(200, 90)) > 200);
    }

    void neutralSettingsAreBitExact() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        QVERIFY(!working.isNull());
        const QString out = m_dir.filePath("same.png");
        QVERIFY(PhotoExporter::save(out, original, working, {}, {}));
        QImage a(m_src), b(out);
        QCOMPARE(b.size(), a.size());
        QCOMPARE(b.convertToFormat(QImage::Format_RGB888), a.convertToFormat(QImage::Format_RGB888));
    }

    void cropIsExactAndLossless() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        PhotoAdjustments adj;
        adj.crop = QRectF(0.25, 0.5, 0.5, 0.5);
        const QString out = m_dir.filePath("crop.png");
        QVERIFY(PhotoExporter::save(out, original, working, adj, {}));
        QImage b(out);
        QCOMPARE(b.size(), QSize(100, 50));
        const QImage expected = QImage(m_src).copy(50, 50, 100, 50).convertToFormat(QImage::Format_RGB888);
        QCOMPARE(b.convertToFormat(QImage::Format_RGB888), expected);
    }

    void exposureDoublesLinearLight() {
        QImage img(1, 1, QImage::Format_RGBX64);
        // sRGB 0.5 -> linear 0.214; +1 EV -> 0.428 linear -> sRGB 0.686
        img.setPixelColor(0, 0, QColor::fromRgbF(0.5f, 0.5f, 0.5f));
        PhotoAdjustments adj;
        adj.color.exposure = 1.0;
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(out.pixelColor(0, 0).redF() - 0.6858) < 0.002);
        adj.color.exposure = -1.0;
        const QImage dark = PhotoProcessor::apply(img, adj);
        QVERIFY(dark.pixelColor(0, 0).redF() < 0.5f);
    }

    void contrastPivotsAroundMiddleGrey() {
        QImage img(3, 1, QImage::Format_RGBX64);
        const float grey = 0.4614f; // sRGB value of linear 0.18
        img.setPixelColor(0, 0, QColor::fromRgbF(grey, grey, grey));
        img.setPixelColor(1, 0, QColor::fromRgbF(0.2f, 0.2f, 0.2f));
        img.setPixelColor(2, 0, QColor::fromRgbF(0.8f, 0.8f, 0.8f));
        PhotoAdjustments adj;
        adj.color.contrast = 50;
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(out.pixelColor(0, 0).redF() - grey) < 0.002);
        QVERIFY(out.pixelColor(1, 0).redF() < 0.2f);
        QVERIFY(out.pixelColor(2, 0).redF() > 0.8f);
    }

    void temperatureWarmsAndCools() {
        QImage img(1, 1, QImage::Format_RGBX64);
        img.setPixelColor(0, 0, QColor::fromRgbF(0.5f, 0.5f, 0.5f));
        PhotoAdjustments adj;
        adj.color.temperature = 50;
        QColor warm = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(warm.redF() > warm.blueF());
        adj.color.temperature = -50;
        QColor cool = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(cool.blueF() > cool.redF());
        adj.color.temperature = 0;
        adj.color.tint = 50;
        QColor magenta = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(magenta.greenF() < magenta.redF());
    }

    void adjustedSaveIs16Bit() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        PhotoAdjustments adj;
        adj.color.exposure = 0.5;
        const QString out = m_dir.filePath("adjusted.png");
        QVERIFY(PhotoExporter::save(out, original, working, adj, {}));
        QImageReader r(out);
        const QImage img = r.read();
        QVERIFY(PhotoExporter::sourceBitDepth(img) == 16);
        // And the original file is untouched.
        QCOMPARE(QImage(m_src).convertToFormat(QImage::Format_RGB888), makePhoto(200, 100));
    }

    void undoMergesSliderDrags() {
        PhotoLibrary lib;
        QUndoStack stack;
        const QUuid id = lib.add(m_src);
        PhotoAdjustments a, b, c;
        b.color.exposure = 0.3;
        c.color.exposure = 0.6;
        stack.push(new PhotoAdjustCommand(&lib, id, a, b, "exp", PhotoAdjustCommand::Exposure));
        stack.push(new PhotoAdjustCommand(&lib, id, b, c, "exp", PhotoAdjustCommand::Exposure));
        QCOMPARE(stack.count(), 1);
        QCOMPARE(lib.find(id)->adjustments.color.exposure, 0.6);
        stack.undo();
        QCOMPARE(lib.find(id)->adjustments.color.exposure, 0.0);
        stack.redo();
        QCOMPARE(lib.find(id)->adjustments.color.exposure, 0.6);
    }

    void heicPhotosAreAssembledFromTiles() {
        QVERIFY(isImageFile(QStringLiteral("a/b/IMG_0001.HEIC")));
        QVERIFY(isImageFile(QStringLiteral("x.avif")));
        if (LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(61, 7, 100))
            QSKIP("FFmpeg too old for HEIF tile grids");
        QTemporaryDir dir;
        // 2 x 2 tiles of 64: red, green / blue, white; cropped to 120 x 100.
        const std::vector<QColor> colors = {Qt::red, Qt::green, Qt::blue, Qt::white};
        const QByteArray heic = makeTiledHeic(dir.path(), 2, 2, 64, colors, 120, 100, 0);
        if (heic.isEmpty())
            QSKIP("No HEVC encoder");
        const QString path = dir.filePath(QStringLiteral("IMG_0001.HEIC"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(heic);
        f.close();

        const auto info = probeMedia(path);
        QVERIFY(info);
        QCOMPARE(info->kind, MediaKind::Image);
        QCOMPARE(info->imageWidth, 120);
        QCOMPARE(info->imageHeight, 100);

        QString err;
        const QImage img = PhotoProcessor::load(path, nullptr, &err);
        QVERIFY2(!img.isNull(), qPrintable(err));
        QCOMPARE(img.size(), QSize(120, 100));
        QCOMPARE(img.format(), QImage::Format_RGBX64);
        auto looksLike = [&](const QImage& im, int x, int y, QColor want) {
            const QColor c = im.pixelColor(x, y);
            return std::abs(c.red() - want.red()) < 24 && std::abs(c.green() - want.green()) < 24 &&
                   std::abs(c.blue() - want.blue()) < 24;
        };
        QVERIFY2(looksLike(img, 10, 10, Qt::red), "top left tile");
        QVERIFY2(looksLike(img, 110, 10, Qt::green), "top right tile");
        QVERIFY2(looksLike(img, 10, 90, Qt::blue), "bottom left tile");
        QVERIFY2(looksLike(img, 110, 90, Qt::white), "bottom right tile");
        // Tiles meet edge to edge, without gaps or offsets.
        QVERIFY(looksLike(img, 60, 60, Qt::red) && looksLike(img, 67, 60, Qt::green));
        QVERIFY(looksLike(img, 60, 67, Qt::blue) && looksLike(img, 67, 67, Qt::white));

        // Turned a quarter counter-clockwise: the top right comes to the top left.
        const QString turned = dir.filePath(QStringLiteral("turned.heic"));
        QFile g(turned);
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write(makeTiledHeic(dir.path(), 2, 2, 64, colors, 120, 100, 1));
        g.close();
        const auto turnedInfo = probeMedia(turned);
        QVERIFY(turnedInfo);
        QCOMPARE(QSize(turnedInfo->imageWidth, turnedInfo->imageHeight), QSize(100, 120));
        const QImage up = PhotoProcessor::load(turned);
        QCOMPARE(up.size(), QSize(100, 120));
        QVERIFY2(looksLike(up, 10, 10, Qt::green), "top left after turning");
        QVERIFY2(looksLike(up, 90, 110, Qt::blue), "bottom right after turning");

        const QImage thumb = heif::thumbnail(path, QSize(32, 32));
        QVERIFY(!thumb.isNull());
        QVERIFY(thumb.width() <= 32 && thumb.height() <= 32);
    }

    void brokenHeicIsReportedNotOpened() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("broken.heic"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("\0\0\0\x18" "ftypheic", 12) + QByteArray(64, 'x'));
        f.close();
        QString err;
        QVERIFY(!probeMedia(path, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(PhotoProcessor::load(path, nullptr, &err).isNull());
    }

    void rawPhotosAreDeveloped() {
        if (!raw::available())
            QSKIP("Built without LibRaw");
        QVERIFY(isImageFile(QStringLiteral("a/b/IMG_0001.CR2")));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("çekim.dng"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeLinearDng(64, 48));
        f.close();

        const auto info = probeMedia(path);
        QVERIFY(info);
        QCOMPARE(info->kind, MediaKind::Image);
        QCOMPARE(info->imageWidth, 64);
        QCOMPARE(info->imageHeight, 48);

        QString err;
        QImage original;
        const QImage img = PhotoProcessor::load(path, &original, &err);
        QVERIFY2(!img.isNull(), qPrintable(err));
        QCOMPARE(img.size(), QSize(64, 48));
        QCOMPARE(img.format(), QImage::Format_RGBX64);
        const QRgba64 red = img.pixelColor(10, 24).rgba64();
        const QRgba64 grey = img.pixelColor(54, 24).rgba64();
        QVERIFY2(red.red() > 3 * red.green() && red.red() > 3 * red.blue(), "red stays red");
        QVERIFY2(std::abs(int(grey.red()) - int(grey.green())) < 3000
                     && std::abs(int(grey.blue()) - int(grey.green())) < 3000,
                 "grey stays neutral (camera white balance)");
        QVERIFY(grey.green() > 10000 && grey.green() < 60000);

        const QImage thumb = raw::thumbnail(path, QSize(32, 32));
        QVERIFY(!thumb.isNull());
        QVERIFY(thumb.width() <= 32 && thumb.height() <= 32);

        QVERIFY(raw::decode(dir.filePath(QStringLiteral("missing.nef")), &err).isNull());
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(PhotoTest)
#include "tst_photo.moc"
