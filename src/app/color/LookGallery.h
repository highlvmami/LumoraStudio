#pragma once

#include <QImage>
#include <QListWidget>

namespace lumora {

// Ready-made colour filters as small pictures: the current picture with each
// filter applied. Clicking one chooses it; "None" removes the filter.
class LookGallery : public QListWidget {
    Q_OBJECT
public:
    explicit LookGallery(QWidget* parent = nullptr);

    // The picture the previews are made from (any size; it is scaled down).
    void setPreviewImage(const QImage& image);
    // Marks the filter in use (empty = none) without emitting lookChosen.
    void setCurrentLook(const QString& id);
    QSize sizeHint() const override;

signals:
    void lookChosen(const QString& id);

private:
    void rebuild();

    QImage m_preview;
    QString m_current;
    bool m_syncing = false;
};

} // namespace lumora
