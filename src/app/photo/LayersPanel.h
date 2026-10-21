#pragma once

#include <QHash>
#include <QUuid>
#include <QWidget>
#include <functional>
#include <vector>

#include "image/PhotoAdjustments.h"

class QListWidget;
class QToolButton;
class QPushButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QGroupBox;
class QLabel;

namespace lumora {

class SliderRow;
class ColorPanel;

// Layer stack of the current photo: adjustment layers (colour settings seen
// through a mask) and picture layers (another image placed on top), each with
// blend mode, opacity and a rectangle / ellipse / gradient / brush mask. The panel
// reports edits; the photo workspace turns them into undo steps.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget* parent = nullptr);

    void setLayers(const std::vector<PhotoLayer>& layers, bool enabled);
    QUuid currentLayer() const { return m_current; }
    const PhotoLayer* current() const;
    // The brush the photo is painted with when the mask is a brush mask:
    // size, hardness, flow and paint / erase (no points).
    BrushStroke brush() const;
    bool showMaskOverlay() const;

signals:
    void layersEdited(const std::vector<lumora::PhotoLayer>& layers, const QString& undoText, const QString& field);
    void currentLayerChanged();
    void brushChanged();

private:
    void rebuildList();
    void syncEditors();
    void editCurrent(const QString& text, const QString& field, const std::function<void(PhotoLayer&)>& change);
    void commit(std::vector<PhotoLayer> layers, const QString& text, const QString& field = {});
    int currentIndex() const;
    void addAdjustment();
    void addPicture();
    void removeCurrent();
    void moveCurrent(int delta);

    std::vector<PhotoLayer> m_layers;
    QUuid m_current;
    bool m_syncing = false;
    bool m_enabled = false;

    QListWidget* m_list;
    QToolButton* m_addAdjustment;
    QToolButton* m_addPicture;
    QToolButton* m_remove;
    QToolButton* m_up;
    QToolButton* m_down;
    QWidget* m_editors;
    QLineEdit* m_name;
    QComboBox* m_blend;
    SliderRow* m_opacity;
    QGroupBox* m_pictureBox;
    SliderRow* m_picX;
    SliderRow* m_picY;
    SliderRow* m_picScale;
    SliderRow* m_picRotation;
    SliderRow* m_picFrame; // video stickers: which frame (seconds)
    QHash<QString, double> m_durations; // seconds, per video sticker file
    QComboBox* m_maskShape;
    QWidget* m_maskEditors;
    SliderRow* m_maskX;
    SliderRow* m_maskY;
    SliderRow* m_maskW;
    SliderRow* m_maskH;
    SliderRow* m_maskAngle;
    SliderRow* m_maskFeather;
    QCheckBox* m_maskInvert;
    QLabel* m_maskHint;
    QWidget* m_brushEditors;
    SliderRow* m_brushSize;
    SliderRow* m_brushHardness;
    SliderRow* m_brushFlow;
    QCheckBox* m_brushErase;
    QCheckBox* m_brushOverlay;
    QPushButton* m_brushClear;
    QGroupBox* m_colorBox;
    ColorPanel* m_color;
};

} // namespace lumora
