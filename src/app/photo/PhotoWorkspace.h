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
class QTabWidget;

namespace lumora {

class PhotoLibrary;
class PhotoView;
class ColorPanel;
class LayersPanel;
class SmartToolsPanel;
class ScopesWidget;

// The "Photo" workspace: photo list, preview with crop tool and mask
// outline, a scope, the photo's colour settings and its layers. Every change
// is an undo step; the original file is never modified.
class PhotoWorkspace : public QWidget {
    Q_OBJECT
public:
    PhotoWorkspace(PhotoLibrary* library, QUndoStack* undo, QWidget* parent = nullptr);

    void openFiles(const QStringList& paths);
    void showPhoto(const QUuid& id);
    QUuid currentPhoto() const { return m_current; }
    // The edited, cropped photo as shown in the preview, and its file name.
    QImage previewImage() const;
    QString currentTitle() const;
    // Moves to the photo `delta` places away in the list.
    void stepPhoto(int delta);

public slots:
    void openDialog();
    void exportCurrent();
    void resetAll();

signals:
    void statusMessage(const QString& msg);
    void previewChanged();
    void fullScreenRequested();

private:
    void loadCurrent();
    void syncControls();
    void scheduleRender();
    void render();
    void pushChange(const PhotoAdjustments& after, const QString& text, int control, const QString& key = {});
    void updateMaskOverlay();
    void findRedactions();
    // One click: cut the subject out with the default settings (or bring
    // the background back when it is already removed).
    void toggleBackground();
    void showViewMenu(const QPoint& pos);
    PhotoAdjustments current() const;

    PhotoLibrary* m_lib;
    QUndoStack* m_undo;
    QUuid m_current;
    QImage m_original; // as decoded (exact pixels)
    QImage m_working;  // RGBA64 full resolution
    QImage m_proxy;    // RGBA64 preview resolution
    bool m_compare = false;
    QImage m_shown; // last preview, uncropped
    QRect m_shownCrop;

    QListWidget* m_list;
    PhotoView* m_view;
    ScopesWidget* m_scope;
    QTabWidget* m_tabs;
    ColorPanel* m_color;
    LayersPanel* m_layers;
    SmartToolsPanel* m_smart;
    QToolButton* m_cropBtn;
    QComboBox* m_aspect;
    QPushButton* m_exportBtn;
    QToolButton* m_compareBtn;
    QToolButton* m_fullScreenBtn;
    QToolButton* m_bgBtn;
    QLabel* m_info;
    QTimer* m_renderTimer;
    bool m_syncing = false;
};

} // namespace lumora
