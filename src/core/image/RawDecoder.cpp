#include "RawDecoder.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QTransform>
#include <cstring>
#include <memory>

#ifdef LUMORA_HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace lumora::raw {

QStringList fileExtensions() {
    if (!available())
        return {};
    return {"dng", "cr2", "cr3", "crw", "nef", "nrw", "arw", "srf", "sr2", "raf", "orf", "rw2", "pef",
            "srw", "x3f", "3fr", "erf", "kdc", "mrw", "mos", "rwl", "iiq"};
}

bool isRawFile(const QString& path) {
    return fileExtensions().contains(QFileInfo(path).suffix().toLower());
}

#ifdef LUMORA_HAVE_LIBRAW

bool available() {
    return true;
}

QString libraryVersion() {
    return QString::fromLatin1(LibRaw::version());
}

namespace {

// The file is read by Qt so that any path (non-Latin names on Windows
// included) works the same on every system.
struct Opened {
    std::unique_ptr<LibRaw> raw = std::make_unique<LibRaw>();
    QByteArray data;
};

QString errorText(int code) {
    return QString::fromLatin1(libraw_strerror(code));
}

bool open(Opened& o, const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    o.data = f.readAll();
    const int rc = o.raw->open_buffer(o.data.constData(), static_cast<size_t>(o.data.size()));
    if (rc != LIBRAW_SUCCESS) {
        if (error)
            *error = errorText(rc);
        return false;
    }
    return true;
}

// LibRaw's flip code: 3 = 180°, 5 = 90° counter-clockwise, 6 = 90° clockwise.
QImage turnUpright(const QImage& img, int flip) {
    switch (flip) {
    case 3: return img.transformed(QTransform().rotate(180));
    case 5: return img.transformed(QTransform().rotate(-90));
    case 6: return img.transformed(QTransform().rotate(90));
    default: return img;
    }
}

void setDevelopParams(LibRaw& r) {
    auto& p = r.imgdata.params;
    p.output_bps = 16;
    p.output_color = 1; // sRGB primaries
    p.gamm[0] = 1 / 2.4; // sRGB transfer curve
    p.gamm[1] = 12.92;
    p.use_camera_wb = 1;
    p.use_camera_matrix = 1;
    p.auto_bright_thr = 0.001f; // keep highlights: clip at most 0.1 %
    p.user_flip = -1;
}

QImage fromBitmap(const libraw_processed_image_t* m) {
    if (!m || m->type != LIBRAW_IMAGE_BITMAP || (m->colors != 3 && m->colors != 1))
        return {};
    const int w = m->width, h = m->height;
    if (m->bits == 16) {
        QImage img(w, h, QImage::Format_RGBX64);
        if (img.isNull())
            return {};
        const auto* src = reinterpret_cast<const quint16*>(m->data);
        for (int y = 0; y < h; ++y) {
            auto* dst = reinterpret_cast<quint16*>(img.scanLine(y));
            const quint16* s = src + size_t(y) * w * m->colors;
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 3; ++c)
                    dst[x * 4 + c] = s[x * m->colors + (m->colors == 3 ? c : 0)];
                dst[x * 4 + 3] = 0xffff;
            }
        }
        return img;
    }
    QImage img(w, h, QImage::Format_RGB888);
    if (img.isNull())
        return {};
    for (int y = 0; y < h; ++y) {
        const uchar* s = m->data + size_t(y) * w * m->colors;
        uchar* dst = img.scanLine(y);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                dst[x * 3 + c] = s[x * m->colors + (m->colors == 3 ? c : 0)];
    }
    return img;
}

QImage develop(Opened& o, bool half, QString* error) {
    setDevelopParams(*o.raw);
    if (half)
        o.raw->imgdata.params.half_size = 1;
    int rc = o.raw->unpack();
    if (rc == LIBRAW_SUCCESS)
        rc = o.raw->dcraw_process();
    if (rc != LIBRAW_SUCCESS) {
        if (error)
            *error = errorText(rc);
        return {};
    }
    libraw_processed_image_t* m = o.raw->dcraw_make_mem_image(&rc);
    if (!m) {
        if (error)
            *error = errorText(rc);
        return {};
    }
    QImage img = fromBitmap(m);
    LibRaw::dcraw_clear_mem(m);
    if (img.isNull() && error)
        *error = QStringLiteral("Unsupported RAW picture layout");
    img.setColorSpace(QColorSpace::SRgb);
    return img;
}

} // namespace

bool probe(const QString& path, Info* info, QString* error) {
    Opened o;
    if (!open(o, path, error))
        return false;
    if (info) {
        const auto& s = o.raw->imgdata.sizes;
        const bool turned = s.flip & 4;
        info->size = turned ? QSize(s.iheight, s.iwidth) : QSize(s.iwidth, s.iheight);
        const auto& id = o.raw->imgdata.idata;
        info->camera = QStringLiteral("%1 %2").arg(QString::fromLatin1(id.make), QString::fromLatin1(id.model)).trimmed();
        int bits = 0;
        for (unsigned v = o.raw->imgdata.color.maximum; v; v >>= 1)
            ++bits;
        info->bitDepth = bits ? bits : 16;
    }
    return true;
}

QImage decode(const QString& path, QString* error) {
    Opened o;
    if (!open(o, path, error))
        return {};
    return develop(o, false, error);
}

QImage thumbnail(const QString& path, QSize maxSize) {
    Opened o;
    if (!open(o, path, nullptr))
        return {};
    QImage img;
    if (o.raw->unpack_thumb() == LIBRAW_SUCCESS) {
        int rc = 0;
        if (libraw_processed_image_t* m = o.raw->dcraw_make_mem_thumb(&rc)) {
            if (m->type == LIBRAW_IMAGE_JPEG)
                img = QImage::fromData(m->data, static_cast<int>(m->data_size));
            else
                img = fromBitmap(m);
            LibRaw::dcraw_clear_mem(m);
        }
        if (!img.isNull())
            img = turnUpright(img, o.raw->imgdata.sizes.flip);
    }
    if (img.isNull()) {
        o.raw->recycle();
        if (!open(o, path, nullptr))
            return {};
        img = develop(o, true, nullptr);
    }
    if (img.isNull())
        return {};
    if (img.width() > maxSize.width() || img.height() > maxSize.height())
        img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

#else

bool available() {
    return false;
}

QString libraryVersion() {
    return {};
}

bool probe(const QString&, Info*, QString* error) {
    if (error)
        *error = QStringLiteral("This build cannot open RAW photos");
    return false;
}

QImage decode(const QString&, QString* error) {
    if (error)
        *error = QStringLiteral("This build cannot open RAW photos");
    return {};
}

QImage thumbnail(const QString&, QSize) {
    return {};
}

#endif

} // namespace lumora::raw
