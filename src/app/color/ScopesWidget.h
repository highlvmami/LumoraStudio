#pragma once

#include <QImage>
#include <QWidget>

#include "color/Scopes.h"

class QComboBox;
class QTimer;

namespace lumora {

class ScopeView;

// Histogram, waveform, RGB parade or vectorscope of the picture on screen.
// setImage() can be called for every frame: the scope is recomputed at most
// about 15 times a second and only while visible.
class ScopesWidget : public QWidget {
    Q_OBJECT
public:
    explicit ScopesWidget(QWidget* parent = nullptr, scopes::Kind initial = scopes::Kind::Waveform,
                          const QString& settingsKey = QStringLiteral("scopes/kind"));

    void setImage(const QImage& image);
    void setKind(scopes::Kind kind);

protected:
    void showEvent(QShowEvent*) override;

private:
    void recompute();

    QComboBox* m_kind;
    ScopeView* m_view;
    QTimer* m_timer;
    QImage m_image;
    QString m_settingsKey;
    bool m_dirty = false;
};

// The drawing area: graticule plus the trace computed by ScopesWidget.
class ScopeView : public QWidget {
    Q_OBJECT
public:
    explicit ScopeView(QWidget* parent = nullptr);
    void setTrace(const QImage& trace, scopes::Kind kind);
    scopes::Kind kind() const { return m_kind; }
    QRect plotRect() const;
    QSize minimumSizeHint() const override { return {160, 100}; }
    QSize sizeHint() const override { return {320, 200}; }

signals:
    void resized();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QImage m_trace;
    scopes::Kind m_kind = scopes::Kind::Waveform;
};

} // namespace lumora
