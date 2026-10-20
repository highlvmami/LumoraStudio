#pragma once

#include <QImage>
#include <QUuid>
#include <QWidget>

#include "image/PhotoAdjustments.h"

class QListWidget;
class QComboBox;
class QPushButton;
class QToolButton;
class QUndoStack;
class QLabel;
class QTimer;
class QSlider;
class QDoubleSpinBox;

namespace lumora {

class PhotoLibrary;
class PhotoView;
class SliderRow;

// The "Photo" workspace: photo list, preview with crop tool and the basic
// adjustments (exposure, contrast, white balance). Every change is an undo
// step; the original file is never modified.
class PhotoWorkspace : public QWidget {
    Q_OBJECT
public:
    PhotoWorkspace(PhotoLibrary* library, QUndoStack* undo, QWidget* parent = nullptr);

    void openFiles(const QStringList& paths);
    void showPhoto(const QUuid& id);
    QUuid currentPhoto() const { return m_current; }

public slots:
    void openDialog();
    void exportCurrent();
    void resetAll();

signals:
    void statusMessage(const QString& msg);

private:
    void loadCurrent();
    void syncControls();
    void scheduleRender();
    void render();
    void pushChange(const PhotoAdjustments& after, const QString& text, int control);
    PhotoAdjustments current() const;

    PhotoLibrary* m_lib;
    QUndoStack* m_undo;
    QUuid m_current;
    QImage m_original; // as decoded (exact pixels)
    QImage m_working;  // RGBA64 full resolution
    QImage m_proxy;    // RGBA64 preview resolution
    bool m_compare = false;

    QListWidget* m_list;
    PhotoView* m_view;
    SliderRow* m_exposure;
    SliderRow* m_contrast;
    SliderRow* m_temperature;
    SliderRow* m_tint;
    QToolButton* m_cropBtn;
    QComboBox* m_aspect;
    QPushButton* m_exportBtn;
    QToolButton* m_compareBtn;
    QLabel* m_info;
    QTimer* m_renderTimer;
    bool m_syncing = false;
};

// Label + slider + spin box with double-click reset.
class SliderRow : public QWidget {
    Q_OBJECT
public:
    SliderRow(const QString& label, double min, double max, double step, int decimals, QWidget* parent = nullptr);
    double value() const { return m_value; }
    void setValue(double v);

signals:
    void valueChanged(double v);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QSlider* m_slider;
    QDoubleSpinBox* m_spin;
    double m_step;
    double m_value = 0;
};

} // namespace lumora
