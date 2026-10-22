#include "BatchPhotoDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "Tasks.h"
#include "color/Looks.h"
#include "image/BatchPhoto.h"
#include "media/MediaInfo.h"

namespace lumora {

namespace {

enum FormatChoice { KeepFormat = -1 };

void paintSwatch(QToolButton* b, const QColor& c) {
    QPixmap pm(28, 16);
    pm.fill(c);
    b->setIcon(QIcon(pm));
    b->setIconSize(pm.size());
}

} // namespace

BatchPhotoDialog::BatchPhotoDialog(QWidget* parent, const QStringList& files) : QDialog(parent) {
    setWindowTitle(tr("Process many photos"));
    resize(760, 640);
    auto* outer = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Resize, colour, mark and convert many photos in one go. New files are written; "
                                "your originals are never changed."));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto* columns = new QHBoxLayout;
    outer->addLayout(columns, 1);

    // Left: the photos.
    auto* left = new QVBoxLayout;
    auto* head = new QHBoxLayout;
    head->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Photos"))));
    head->addStretch(1);
    m_count = new QLabel;
    head->addWidget(m_count);
    left->addLayout(head);
    m_files = new QListWidget;
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setAcceptDrops(true);
    left->addWidget(m_files, 1);
    auto* fileButtons = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add photos…"));
    auto* folderBtn = new QPushButton(tr("Add folder…"));
    auto* removeBtn = new QPushButton(tr("Remove"));
    fileButtons->addWidget(addBtn);
    fileButtons->addWidget(folderBtn);
    fileButtons->addWidget(removeBtn);
    fileButtons->addStretch(1);
    left->addLayout(fileButtons);
    columns->addLayout(left, 1);

    // Right: what to do with them.
    auto* right = new QVBoxLayout;
    columns->addLayout(right, 1);

    auto* sizeBox = new QGroupBox(tr("Size"));
    auto* sf = new QFormLayout(sizeBox);
    m_resize = new QComboBox;
    m_resize->addItem(tr("Keep the size"), int(BatchPhotoSettings::Resize::None));
    m_resize->addItem(tr("Longest side"), int(BatchPhotoSettings::Resize::LongEdge));
    m_resize->addItem(tr("Width"), int(BatchPhotoSettings::Resize::Width));
    m_resize->addItem(tr("Height"), int(BatchPhotoSettings::Resize::Height));
    m_resize->addItem(tr("Percent"), int(BatchPhotoSettings::Resize::Percent));
    sf->addRow(tr("Resize"), m_resize);
    m_pixels = new QSpinBox;
    m_pixels->setRange(16, 20000);
    m_pixels->setSuffix(tr(" px"));
    m_percent = new QSpinBox;
    m_percent->setRange(1, 400);
    m_percent->setSuffix(QStringLiteral(" %"));
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_pixels);
    sizeRow->addWidget(m_percent);
    sf->addRow(tr("To"), sizeRow);
    m_enlarge = new QCheckBox(tr("Also make small photos bigger"));
    sf->addRow(m_enlarge);
    right->addWidget(sizeBox);

    auto* colorBox = new QGroupBox(tr("Colour"));
    auto* cf = new QFormLayout(colorBox);
    m_autoColor = new QCheckBox(tr("Automatic colour and light"));
    cf->addRow(m_autoColor);
    m_look = new QComboBox;
    m_look->addItem(tr("No filter"), QString());
    for (const auto& l : looks())
        m_look->addItem(l.name, l.id);
    m_lookAmount = new QSpinBox;
    m_lookAmount->setRange(0, 100);
    m_lookAmount->setSuffix(QStringLiteral(" %"));
    auto* lookRow = new QHBoxLayout;
    lookRow->addWidget(m_look, 1);
    lookRow->addWidget(m_lookAmount);
    cf->addRow(tr("Filter"), lookRow);
    right->addWidget(colorBox);

    auto* markBox = new QGroupBox(tr("Watermark"));
    auto* mf = new QFormLayout(markBox);
    m_watermark = new QComboBox;
    m_watermark->addItem(tr("None"), int(BatchPhotoSettings::Watermark::None));
    m_watermark->addItem(tr("Text"), int(BatchPhotoSettings::Watermark::Text));
    m_watermark->addItem(tr("Picture (logo)"), int(BatchPhotoSettings::Watermark::Picture));
    mf->addRow(tr("Mark"), m_watermark);
    m_markPages = new QStackedWidget;
    m_markPages->addWidget(new QWidget);
    auto* textPage = new QWidget;
    auto* tl = new QHBoxLayout(textPage);
    tl->setContentsMargins(0, 0, 0, 0);
    m_markText = new QLineEdit;
    m_markText->setPlaceholderText(tr("© Your name"));
    m_markColor = new QToolButton;
    m_markColor->setToolTip(tr("Text colour"));
    tl->addWidget(m_markText, 1);
    tl->addWidget(m_markColor);
    m_markPages->addWidget(textPage);
    auto* picPage = new QWidget;
    auto* pl = new QHBoxLayout(picPage);
    pl->setContentsMargins(0, 0, 0, 0);
    m_markPicture = new QLineEdit;
    m_markPicture->setPlaceholderText(tr("A PNG with transparency works best"));
    auto* browsePic = new QPushButton(tr("Choose…"));
    pl->addWidget(m_markPicture, 1);
    pl->addWidget(browsePic);
    m_markPages->addWidget(picPage);
    mf->addRow(QString(), m_markPages);
    m_markPosition = new QComboBox;
    m_markPosition->addItem(tr("Bottom right"), int(Qt::AlignRight | Qt::AlignBottom));
    m_markPosition->addItem(tr("Bottom left"), int(Qt::AlignLeft | Qt::AlignBottom));
    m_markPosition->addItem(tr("Top right"), int(Qt::AlignRight | Qt::AlignTop));
    m_markPosition->addItem(tr("Top left"), int(Qt::AlignLeft | Qt::AlignTop));
    m_markPosition->addItem(tr("Centre"), int(Qt::AlignCenter));
    mf->addRow(tr("Place"), m_markPosition);
    m_markSize = new QSpinBox;
    m_markSize->setRange(2, 100);
    m_markSize->setSuffix(tr(" % of the width"));
    mf->addRow(tr("Size"), m_markSize);
    m_markOpacity = new QSpinBox;
    m_markOpacity->setRange(5, 100);
    m_markOpacity->setSuffix(QStringLiteral(" %"));
    mf->addRow(tr("Opacity"), m_markOpacity);
    right->addWidget(markBox);

    auto* outBox = new QGroupBox(tr("Save as"));
    auto* of = new QFormLayout(outBox);
    m_format = new QComboBox;
    m_format->addItem(tr("Same type as each photo"), int(KeepFormat));
    m_format->addItem(tr("JPEG — small, for sharing"), int(PhotoFormat::Jpeg));
    m_format->addItem(tr("PNG — lossless"), int(PhotoFormat::Png));
    if (PhotoExporter::formatAvailable(PhotoFormat::WebP))
        m_format->addItem(tr("WebP — small, for websites"), int(PhotoFormat::WebP));
    if (PhotoExporter::formatAvailable(PhotoFormat::Tiff))
        m_format->addItem(tr("TIFF — lossless, for print"), int(PhotoFormat::Tiff));
    of->addRow(tr("File type"), m_format);
    m_quality = new QSpinBox;
    m_quality->setRange(1, 100);
    of->addRow(tr("Quality"), m_quality);
    m_folder = new QLineEdit;
    auto* browseFolder = new QPushButton(tr("Choose…"));
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browseFolder);
    of->addRow(tr("Folder"), folderRow);
    m_pattern = new QLineEdit;
    m_pattern->setToolTip(tr("{name} = the photo's name, {n} = number, {date} = date of the photo"));
    of->addRow(tr("Name"), m_pattern);
    m_example = new QLabel;
    m_example->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_example->setWordWrap(true);
    of->addRow(QString(), m_example);
    right->addWidget(outBox);
    right->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* go = buttons->addButton(tr("Process photos"), QDialogButtonBox::AcceptRole);
    go->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &BatchPhotoDialog::run);
    outer->addWidget(buttons);

    // Last used settings.
    QSettings s;
    s.beginGroup(QStringLiteral("batchPhotos"));
    m_resize->setCurrentIndex(std::max(0, m_resize->findData(s.value(QStringLiteral("resize"), 1))));
    m_pixels->setValue(s.value(QStringLiteral("pixels"), 1920).toInt());
    m_percent->setValue(s.value(QStringLiteral("percent"), 50).toInt());
    m_enlarge->setChecked(s.value(QStringLiteral("enlarge"), false).toBool());
    m_autoColor->setChecked(s.value(QStringLiteral("autoColor"), false).toBool());
    m_look->setCurrentIndex(std::max(0, m_look->findData(s.value(QStringLiteral("look")).toString())));
    m_lookAmount->setValue(s.value(QStringLiteral("lookAmount"), 100).toInt());
    m_watermark->setCurrentIndex(std::max(0, m_watermark->findData(s.value(QStringLiteral("watermark"), 0))));
    m_markText->setText(s.value(QStringLiteral("markText")).toString());
    m_color = s.value(QStringLiteral("markColor"), QColor(Qt::white)).value<QColor>();
    m_markPicture->setText(s.value(QStringLiteral("markPicture")).toString());
    m_markPosition->setCurrentIndex(std::max(0, m_markPosition->findData(s.value(QStringLiteral("markPosition"), int(Qt::AlignRight | Qt::AlignBottom)))));
    m_markSize->setValue(s.value(QStringLiteral("markSize"), 20).toInt());
    m_markOpacity->setValue(s.value(QStringLiteral("markOpacity"), 70).toInt());
    m_format->setCurrentIndex(std::max(0, m_format->findData(s.value(QStringLiteral("format"), int(PhotoFormat::Jpeg)))));
    m_quality->setValue(s.value(QStringLiteral("quality"), 92).toInt());
    m_folder->setText(s.value(QStringLiteral("folder")).toString());
    m_pattern->setText(s.value(QStringLiteral("pattern"), QStringLiteral("{name}")).toString());
    s.endGroup();
    paintSwatch(m_markColor, m_color);

    connect(addBtn, &QPushButton::clicked, this, [this] {
        QSettings st;
        QStringList patterns;
        for (const auto& e : imageFileExtensions())
            patterns << QStringLiteral("*.") + e;
        const QStringList picked = QFileDialog::getOpenFileNames(this, tr("Add photos"), st.value(QStringLiteral("dirs/photos")).toString(),
                                                                 tr("Photos (%1)").arg(patterns.join(QLatin1Char(' '))));
        if (!picked.isEmpty())
            st.setValue(QStringLiteral("dirs/photos"), QFileInfo(picked.front()).absolutePath());
        addFiles(picked);
    });
    connect(folderBtn, &QPushButton::clicked, this, &BatchPhotoDialog::addFolder);
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        qDeleteAll(m_files->selectedItems());
        updateState();
    });
    connect(m_markColor, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("Text colour"));
        if (c.isValid()) {
            m_color = c;
            paintSwatch(m_markColor, c);
        }
    });
    connect(browsePic, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Watermark picture"), QFileInfo(m_markPicture->text()).absolutePath(),
                                                       tr("Pictures (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!f.isEmpty())
            m_markPicture->setText(QDir::toNativeSeparators(f));
    });
    connect(browseFolder, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Folder for the new photos"), m_folder->text());
        if (!d.isEmpty())
            m_folder->setText(QDir::toNativeSeparators(d));
    });
    for (QComboBox* c : {m_resize, m_look, m_watermark, m_format})
        connect(c, &QComboBox::currentIndexChanged, this, &BatchPhotoDialog::updateState);
    for (QLineEdit* e : {m_folder, m_pattern})
        connect(e, &QLineEdit::textChanged, this, &BatchPhotoDialog::updateState);
    addFiles(files);
    updateState();
}

void BatchPhotoDialog::addFiles(const QStringList& files) {
    QSet<QString> have;
    for (int i = 0; i < m_files->count(); ++i)
        have.insert(m_files->item(i)->data(Qt::UserRole).toString());
    for (const QString& f : files) {
        const QString path = QFileInfo(f).absoluteFilePath();
        if (have.contains(path) || !isImageFile(path))
            continue;
        auto* item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
        m_files->addItem(item);
        have.insert(path);
    }
    updateState();
}

void BatchPhotoDialog::addFolder() {
    QSettings st;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add folder"), st.value(QStringLiteral("dirs/photos")).toString());
    if (dir.isEmpty())
        return;
    st.setValue(QStringLiteral("dirs/photos"), dir);
    QStringList found;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        found << it.next();
    found.sort(Qt::CaseInsensitive);
    addFiles(found);
}

BatchPhotoSettings BatchPhotoDialog::settings() const {
    BatchPhotoSettings s;
    s.resize = static_cast<BatchPhotoSettings::Resize>(m_resize->currentData().toInt());
    s.pixels = m_pixels->value();
    s.percent = m_percent->value();
    s.enlarge = m_enlarge->isChecked();
    s.autoColor = m_autoColor->isChecked();
    s.look = m_look->currentData().toString();
    s.lookAmount = m_lookAmount->value() / 100.0;
    s.watermark = static_cast<BatchPhotoSettings::Watermark>(m_watermark->currentData().toInt());
    s.watermarkText = m_markText->text();
    s.watermarkColor = m_color;
    s.watermarkPicture = QDir::fromNativeSeparators(m_markPicture->text().trimmed());
    s.watermarkPosition = Qt::Alignment(m_markPosition->currentData().toInt());
    s.watermarkSize = m_markSize->value() / 100.0;
    s.watermarkOpacity = m_markOpacity->value() / 100.0;
    const int f = m_format->currentData().toInt();
    s.keepFormat = f == KeepFormat;
    s.format.format = s.keepFormat ? PhotoFormat::Jpeg : static_cast<PhotoFormat>(f);
    s.format.quality = m_quality->value();
    s.folder = QDir::fromNativeSeparators(m_folder->text().trimmed());
    s.pattern = m_pattern->text();
    return s;
}

void BatchPhotoDialog::saveSettings() const {
    QSettings s;
    s.beginGroup(QStringLiteral("batchPhotos"));
    s.setValue(QStringLiteral("resize"), m_resize->currentData());
    s.setValue(QStringLiteral("pixels"), m_pixels->value());
    s.setValue(QStringLiteral("percent"), m_percent->value());
    s.setValue(QStringLiteral("enlarge"), m_enlarge->isChecked());
    s.setValue(QStringLiteral("autoColor"), m_autoColor->isChecked());
    s.setValue(QStringLiteral("look"), m_look->currentData());
    s.setValue(QStringLiteral("lookAmount"), m_lookAmount->value());
    s.setValue(QStringLiteral("watermark"), m_watermark->currentData());
    s.setValue(QStringLiteral("markText"), m_markText->text());
    s.setValue(QStringLiteral("markColor"), m_color);
    s.setValue(QStringLiteral("markPicture"), m_markPicture->text());
    s.setValue(QStringLiteral("markPosition"), m_markPosition->currentData());
    s.setValue(QStringLiteral("markSize"), m_markSize->value());
    s.setValue(QStringLiteral("markOpacity"), m_markOpacity->value());
    s.setValue(QStringLiteral("format"), m_format->currentData());
    s.setValue(QStringLiteral("quality"), m_quality->value());
    s.setValue(QStringLiteral("folder"), m_folder->text());
    s.setValue(QStringLiteral("pattern"), m_pattern->text());
    s.endGroup();
}

void BatchPhotoDialog::updateState() {
    const auto r = static_cast<BatchPhotoSettings::Resize>(m_resize->currentData().toInt());
    m_pixels->setVisible(r == BatchPhotoSettings::Resize::LongEdge || r == BatchPhotoSettings::Resize::Width ||
                         r == BatchPhotoSettings::Resize::Height);
    m_percent->setVisible(r == BatchPhotoSettings::Resize::Percent);
    m_enlarge->setEnabled(r != BatchPhotoSettings::Resize::None);
    m_lookAmount->setEnabled(!m_look->currentData().toString().isEmpty());
    const int mark = m_watermark->currentData().toInt();
    m_markPages->setCurrentIndex(mark);
    for (QWidget* w : std::initializer_list<QWidget*>{m_markPosition, m_markSize, m_markOpacity})
        w->setEnabled(mark != int(BatchPhotoSettings::Watermark::None));
    const int f = m_format->currentData().toInt();
    m_quality->setEnabled(f == KeepFormat || f == int(PhotoFormat::Jpeg) || f == int(PhotoFormat::WebP));
    m_count->setText(tr("%n photo(s)", nullptr, m_files->count()));
    if (m_files->count() > 0) {
        const BatchPhoto batch(settings());
        const QString src = m_files->item(0)->data(Qt::UserRole).toString();
        m_example->setText(tr("For example: %1").arg(QDir::toNativeSeparators(batch.outputPath(src, 0, m_files->count()))));
    } else {
        m_example->setText(tr("Leave the folder empty to save next to each photo."));
    }
}

void BatchPhotoDialog::run() {
    if (m_files->count() == 0) {
        QMessageBox::information(this, windowTitle(), tr("Add the photos to change first."));
        return;
    }
    const BatchPhotoSettings s = settings();
    if (s.watermark == BatchPhotoSettings::Watermark::Picture && !QFileInfo::exists(s.watermarkPicture)) {
        QMessageBox::information(this, windowTitle(), tr("Choose the watermark picture."));
        return;
    }
    saveSettings();
    QStringList sources;
    for (int i = 0; i < m_files->count(); ++i)
        sources << m_files->item(i)->data(Qt::UserRole).toString();

    // Files that would be written over.
    auto batch = std::make_shared<BatchPhoto>(s);
    QStringList targets;
    int existing = 0;
    for (int i = 0; i < sources.size(); ++i) {
        targets << batch->outputPath(sources[i], i, sources.size());
        existing += QFileInfo::exists(targets.back());
    }
    if (existing > 0 &&
        QMessageBox::question(this, windowTitle(), tr("%n file(s) with these names already exist. Replace them?", nullptr, existing)) !=
            QMessageBox::Yes)
        return;

    auto result = withProgress<QStringList>(this, tr("Processing photos…"), [batch, sources, targets](const TaskProgress& progress) {
        QStringList failures;
        for (int i = 0; i < sources.size(); ++i) {
            if (!progress(double(i) / sources.size()))
                break;
            QString err;
            if (!batch->process(sources[i], targets[i], &err))
                failures << QStringLiteral("%1: %2").arg(QFileInfo(sources[i]).fileName(), err);
        }
        progress(1.0);
        return failures;
    });
    if (!result)
        return;
    const int done = int(sources.size()) - int(result->size());
    QMessageBox box(this);
    box.setWindowTitle(windowTitle());
    box.setIcon(result->isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
    box.setText(tr("%n photo(s) saved.", nullptr, done));
    if (!result->isEmpty())
        box.setDetailedText(result->join(QLatin1Char('\n')));
    QPushButton* open = box.addButton(tr("Open folder"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == open && !targets.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(targets.front()).absolutePath()));
}

} // namespace lumora
