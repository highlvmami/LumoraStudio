#include "LookGallery.h"

#include <QPainter>
#include <QPainterPath>

#include "Theme.h"
#include "color/ColorProcessor.h"
#include "color/Looks.h"

namespace lumora {

namespace {
constexpr int kThumbW = 84;
constexpr int kThumbH = 56;

// A neutral stand-in when there is no picture yet: sky, skin and grass tones.
QImage samplePicture() {
    QImage img(kThumbW * 2, kThumbH * 2, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient sky(0, 0, 0, img.height());
    sky.setColorAt(0, QColor(110, 160, 220));
    sky.setColorAt(0.55, QColor(240, 200, 150));
    sky.setColorAt(0.56, QColor(90, 140, 70));
    sky.setColorAt(1, QColor(40, 80, 40));
    p.fillRect(img.rect(), sky);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(224, 172, 140));
    p.drawEllipse(QPointF(img.width() * 0.62, img.height() * 0.42), img.height() * 0.16, img.height() * 0.2);
    p.setBrush(QColor(200, 60, 60));
    p.drawRoundedRect(QRectF(img.width() * 0.5, img.height() * 0.6, img.height() * 0.48, img.height() * 0.4), 8, 8);
    return img;
}
} // namespace

LookGallery::LookGallery(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setIconSize(QSize(kThumbW, kThumbH));
    setGridSize(QSize(kThumbW + 10, kThumbH + 24));
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMinimumHeight(kThumbH + 30);
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!m_syncing)
            emit lookChosen(item->data(Qt::UserRole).toString());
    });
    rebuild();
}

QSize LookGallery::sizeHint() const { return QSize(3 * (kThumbW + 10), 2 * (kThumbH + 24) + 6); }

void LookGallery::setPreviewImage(const QImage& image) {
    if (image.cacheKey() == m_preview.cacheKey() && !image.isNull())
        return;
    m_preview = image;
    rebuild();
}

void LookGallery::setCurrentLook(const QString& id) {
    m_current = id;
    m_syncing = true;
    for (int i = 0; i < count(); ++i)
        if (item(i)->data(Qt::UserRole).toString() == id)
            setCurrentRow(i);
    m_syncing = false;
}

void LookGallery::rebuild() {
    m_syncing = true;
    clear();
    QImage base = m_preview.isNull() ? samplePicture() : m_preview;
    base = base.scaled(kThumbW, kThumbH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
               .convertToFormat(QImage::Format_RGB32);
    base = base.copy((base.width() - kThumbW) / 2, (base.height() - kThumbH) / 2, kThumbW, kThumbH);
    auto rounded = [](const QImage& img) {
        QImage out(img.size(), QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(out.rect()), 6, 6);
        p.setClipPath(path);
        p.drawImage(0, 0, img);
        return QPixmap::fromImage(out);
    };
    auto add = [&](const QString& id, const QString& name, const QImage& img) {
        auto* it = new QListWidgetItem(QIcon(rounded(img)), name, this);
        it->setData(Qt::UserRole, id);
        it->setToolTip(name);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    };
    add(QString(), tr("None"), base);
    for (const Look& l : looks()) {
        ColorGrade g;
        g.look = l.id;
        add(l.id, l.name, ColorProcessor(g).applied(base));
    }
    m_syncing = false;
    setCurrentLook(m_current);
}

} // namespace lumora
