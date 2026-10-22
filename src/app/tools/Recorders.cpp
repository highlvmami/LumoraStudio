#include "Recorders.h"

#include <QApplication>
#include <QAudioSource>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "Icons.h"
#include "export/LiveWriter.h"

namespace lumora {

namespace {

QString recordingsFolder() {
    QSettings s;
    QString dir = s.value(QStringLiteral("dirs/recordings")).toString();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (base.isEmpty())
            base = QDir::homePath();
        dir = QDir(base).filePath(QStringLiteral("LumoraStudio"));
    }
    QDir().mkpath(dir);
    return dir;
}

QString newRecordingPath(const QString& name, const QString& ext) {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    return QDir(recordingsFolder()).filePath(QStringLiteral("%1 %2.%3").arg(name, stamp, ext));
}

void fillDevices(QComboBox* box) {
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) {
        box->addItem(d.description(), QVariant::fromValue(d));
        if (d.id() == def.id())
            box->setCurrentIndex(box->count() - 1);
    }
    if (box->count() == 0) {
        box->addItem(QCoreApplication::translate("lumora::VoiceoverDialog", "No microphone found"));
        box->setEnabled(false);
    }
}

QString clockText(qint64 ms) {
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

// --- MicCapture --------------------------------------------------------------

MicCapture::MicCapture(QObject* parent) : QObject(parent) {}

MicCapture::~MicCapture() { stop(); }

bool MicCapture::prepare(const QAudioDevice& device, QString* error) {
    m_device = device;
    if (device.isNull()) {
        if (error)
            *error = tr("No microphone found.");
        return false;
    }
    QAudioFormat f;
    f.setSampleFormat(QAudioFormat::Int16);
    for (int rate : {48000, 44100, device.preferredFormat().sampleRate()})
        for (int channels : {1, 2, device.preferredFormat().channelCount()}) {
            f.setSampleRate(rate);
            f.setChannelCount(channels);
            if (rate > 0 && channels > 0 && channels <= 2 && device.isFormatSupported(f)) {
                m_format = f;
                return true;
            }
        }
    if (error)
        *error = tr("The microphone cannot record 16-bit sound.");
    return false;
}

bool MicCapture::start(Sink sink, QString* error) {
    stop();
    m_sink = std::move(sink);
    m_source = new QAudioSource(m_device, m_format, this);
    m_source->setBufferSize(m_format.bytesForDuration(200000));
    m_io = m_source->start();
    if (!m_io) {
        if (error)
            *error = tr("The microphone could not be opened.");
        delete m_source;
        m_source = nullptr;
        return false;
    }
    connect(m_io, &QIODevice::readyRead, this, &MicCapture::read);
    m_levelClock.start();
    return true;
}

void MicCapture::stop() {
    if (!m_source)
        return;
    if (m_io)
        read();
    m_source->stop();
    delete m_source;
    m_source = nullptr;
    m_io = nullptr;
    m_carry.clear();
}

void MicCapture::read() {
    if (!m_io)
        return;
    m_carry += m_io->readAll();
    const int frameBytes = m_format.bytesPerFrame();
    const int frames = frameBytes > 0 ? int(m_carry.size() / frameBytes) : 0;
    if (frames <= 0)
        return;
    const auto* samples = reinterpret_cast<const std::int16_t*>(m_carry.constData());
    for (int i = 0; i < frames * m_format.channelCount(); ++i)
        m_peak = std::max(m_peak, std::abs(samples[i]) / 32768.0);
    if (m_sink)
        m_sink(samples, frames);
    m_carry.remove(0, frames * frameBytes);
    if (m_levelClock.elapsed() > 80) {
        emit level(m_peak);
        m_peak = 0;
        m_levelClock.restart();
    }
}

// --- WavWriter ---------------------------------------------------------------

bool WavWriter::open(const QString& path, int sampleRate, int channels) {
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    m_rate = sampleRate;
    m_channels = channels;
    m_frames = 0;
    m_file.write(QByteArray(44, '\0')); // header, filled in on close
    return true;
}

void WavWriter::write(const std::int16_t* samples, int frames) {
    if (!m_file.isOpen() || frames <= 0)
        return;
    if constexpr (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
        m_file.write(reinterpret_cast<const char*>(samples), qint64(frames) * m_channels * 2);
    } else {
        QByteArray le(frames * m_channels * 2, Qt::Uninitialized);
        for (int i = 0; i < frames * m_channels; ++i)
            qToLittleEndian<qint16>(samples[i], le.data() + 2 * i);
        m_file.write(le);
    }
    m_frames += frames;
}

bool WavWriter::close() {
    if (!m_file.isOpen())
        return false;
    const quint32 data = quint32(m_frames * m_channels * 2);
    QByteArray h(44, '\0');
    char* p = h.data();
    auto u32 = [&](int at, quint32 v) { qToLittleEndian<quint32>(v, p + at); };
    auto u16 = [&](int at, quint16 v) { qToLittleEndian<quint16>(v, p + at); };
    memcpy(p, "RIFF", 4);
    u32(4, 36 + data);
    memcpy(p + 8, "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1); // PCM
    u16(22, quint16(m_channels));
    u32(24, quint32(m_rate));
    u32(28, quint32(m_rate * m_channels * 2));
    u16(32, quint16(m_channels * 2));
    u16(34, 16);
    memcpy(p + 36, "data", 4);
    u32(40, data);
    m_file.seek(0);
    m_file.write(h);
    m_file.close();
    return m_frames > 0;
}

// --- VoiceoverDialog ---------------------------------------------------------

VoiceoverDialog::VoiceoverDialog(Ticks at, QWidget* parent) : QDialog(parent), m_at(at) {
    setWindowTitle(tr("Record voice-over"));
    setMinimumWidth(420);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Speak over the video: the timeline plays from the playhead while you talk, and the "
                                "recording goes on a sound track right there. Use headphones so the microphone does not "
                                "pick up the video's sound."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    m_device = new QComboBox;
    fillDevices(m_device);
    form->addRow(tr("Microphone"), m_device);
    m_meter = new QProgressBar;
    m_meter->setRange(0, 100);
    m_meter->setTextVisible(false);
    m_meter->setFixedHeight(8);
    form->addRow(tr("Level"), m_meter);
    layout->addLayout(form);
    m_play = new QCheckBox(tr("Play the video while recording (without its sound)"));
    m_play->setChecked(QSettings().value(QStringLiteral("voiceover/play"), true).toBool());
    layout->addWidget(m_play);
    m_countdown = new QCheckBox(tr("Count down 3 seconds first"));
    m_countdown->setChecked(QSettings().value(QStringLiteral("voiceover/countdown"), true).toBool());
    layout->addWidget(m_countdown);
    m_status = new QLabel(tr("Ready."));
    QFont big = m_status->font();
    big.setPointSizeF(big.pointSizeF() * 1.4);
    big.setBold(true);
    m_status->setFont(big);
    m_status->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    m_record = buttons->addButton(tr("Record"), QDialogButtonBox::ActionRole);
    m_record->setIcon(Icons::record(QColor(230, 60, 60)));
    m_record->setDefault(true);
    m_record->setEnabled(m_device->isEnabled());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_record, &QPushButton::clicked, this, &VoiceoverDialog::toggle);
    layout->addWidget(buttons);

    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &VoiceoverDialog::tick);
}

VoiceoverDialog::~VoiceoverDialog() {
    if (m_recording || m_count > 0)
        end(true);
}

void VoiceoverDialog::toggle() {
    if (m_recording || m_count > 0) {
        end(true);
        return;
    }
    QSettings s;
    s.setValue(QStringLiteral("voiceover/play"), m_play->isChecked());
    s.setValue(QStringLiteral("voiceover/countdown"), m_countdown->isChecked());
    m_mic = std::make_unique<MicCapture>();
    QString err;
    if (!m_mic->prepare(m_device->currentData().value<QAudioDevice>(), &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        m_mic.reset();
        return;
    }
    connect(m_mic.get(), &MicCapture::level, this, [this](double p) { m_meter->setValue(int(std::sqrt(p) * 100)); });
    m_device->setEnabled(false);
    m_record->setText(tr("Stop"));
    if (m_countdown->isChecked()) {
        m_count = 3;
        m_status->setText(QString::number(m_count));
        m_clock.start();
        m_timer.start();
    } else {
        begin();
    }
}

void VoiceoverDialog::begin() {
    m_count = 0;
    m_path = newRecordingPath(tr("Voice-over"), QStringLiteral("wav"));
    m_wav = std::make_unique<WavWriter>();
    const QAudioFormat f = m_mic->format();
    QString err;
    if (!m_wav->open(m_path, f.sampleRate(), f.channelCount()) ||
        !m_mic->start([w = m_wav.get()](const std::int16_t* s, int n) { w->write(s, n); }, &err)) {
        QMessageBox::warning(this, windowTitle(), err.isEmpty() ? tr("Cannot write %1.").arg(m_path) : err);
        m_wav.reset();
        QFile::remove(m_path);
        end(false);
        return;
    }
    m_recording = true;
    if (m_play->isChecked())
        emit playRequested(m_at, true);
    m_clock.start();
    m_timer.start();
    tick();
}

void VoiceoverDialog::tick() {
    if (m_count > 0) {
        const int left = 3 - int(m_clock.elapsed() / 1000);
        if (left <= 0) {
            m_timer.stop();
            begin();
            return;
        }
        m_status->setText(QString::number(left));
        return;
    }
    m_status->setText(QStringLiteral("● %1").arg(clockText(m_clock.elapsed())));
}

void VoiceoverDialog::end(bool keep) {
    m_timer.stop();
    const bool wasRecording = m_recording;
    m_recording = false;
    m_count = 0;
    if (m_mic)
        m_mic->stop();
    if (wasRecording && m_play->isChecked())
        emit stopRequested();
    m_record->setText(tr("Record"));
    m_device->setEnabled(true);
    m_meter->setValue(0);
    if (m_wav) {
        const bool ok = m_wav->close();
        m_wav.reset();
        if (ok && keep && wasRecording) {
            m_status->setText(tr("Saved. Record again to add another take."));
            emit recorded(m_path, m_at);
            return;
        }
        QFile::remove(m_path);
    }
    m_status->setText(tr("Ready."));
}

// --- ScreenRecordDialog ------------------------------------------------------

ScreenRecordDialog::ScreenRecordDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Record the screen"));
    setMinimumWidth(440);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Records everything on a screen, with your voice if you like, as a video in high "
                                "quality. LumoraStudio steps aside while it records; press Stop on the small bar when done."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    m_screen = new QComboBox;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const QSize px = screens[i]->size() * screens[i]->devicePixelRatio();
        m_screen->addItem(tr("Screen %1 (%2 × %3)").arg(i + 1).arg(px.width()).arg(px.height()), i);
        if (screens[i] == QGuiApplication::primaryScreen())
            m_screen->setCurrentIndex(i);
    }
    form->addRow(tr("Screen"), m_screen);
    m_fps = new QComboBox;
    m_fps->addItem(tr("30 pictures per second"), 30);
    m_fps->addItem(tr("25 pictures per second"), 25);
    m_fps->addItem(tr("15 pictures per second (smaller file)"), 15);
    form->addRow(tr("Smoothness"), m_fps);
    m_micOn = new QCheckBox(tr("Record my voice"));
    m_mic = new QComboBox;
    fillDevices(m_mic);
    m_micOn->setEnabled(m_mic->isEnabled());
    form->addRow(m_micOn, m_mic);
    layout->addLayout(form);
    m_cursor = new QCheckBox(tr("Show the mouse pointer"));
    layout->addWidget(m_cursor);
    m_hide = new QCheckBox(tr("Minimise LumoraStudio while recording"));
    layout->addWidget(m_hide);

    QSettings s;
    m_fps->setCurrentIndex(std::max(0, m_fps->findData(s.value(QStringLiteral("screenRecord/fps"), 30))));
    m_micOn->setChecked(m_mic->isEnabled() && s.value(QStringLiteral("screenRecord/mic"), true).toBool());
    m_cursor->setChecked(s.value(QStringLiteral("screenRecord/cursor"), true).toBool());
    m_hide->setChecked(s.value(QStringLiteral("screenRecord/hide"), true).toBool());
    m_mic->setEnabled(m_micOn->isChecked());
    connect(m_micOn, &QCheckBox::toggled, m_mic, &QWidget::setEnabled);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* go = buttons->addButton(tr("Start recording"), QDialogButtonBox::AcceptRole);
    go->setIcon(Icons::record(QColor(230, 60, 60)));
    go->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &ScreenRecordDialog::start);
    layout->addWidget(buttons);

    connect(&m_grabTimer, &QTimer::timeout, this, &ScreenRecordDialog::grab);
    m_grabTimer.setTimerType(Qt::PreciseTimer);
}

ScreenRecordDialog::~ScreenRecordDialog() {
    if (m_writer)
        stop();
    delete m_bar;
}

bool ScreenRecordDialog::available() {
    // Wayland does not let programs read the screen.
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland")) && QGuiApplication::primaryScreen();
}

void ScreenRecordDialog::start() {
    const auto screens = QGuiApplication::screens();
    const int index = m_screen->currentData().toInt();
    if (index < 0 || index >= screens.size())
        return;
    m_target = screens[index];
    QSettings s;
    s.setValue(QStringLiteral("screenRecord/fps"), m_fps->currentData());
    s.setValue(QStringLiteral("screenRecord/mic"), m_micOn->isChecked());
    s.setValue(QStringLiteral("screenRecord/cursor"), m_cursor->isChecked());
    s.setValue(QStringLiteral("screenRecord/hide"), m_hide->isChecked());

    LiveWriter::Options o;
    o.size = m_target->size() * m_target->devicePixelRatio();
    o.frameRate = m_fps->currentData().toInt();
    o.sampleRate = 0;
    QString err;
    if (m_micOn->isChecked()) {
        m_capture = std::make_unique<MicCapture>();
        if (m_capture->prepare(m_mic->currentData().value<QAudioDevice>(), &err)) {
            o.sampleRate = m_capture->format().sampleRate();
            o.channels = m_capture->format().channelCount();
        } else {
            QMessageBox::warning(this, windowTitle(), tr("Recording without sound: %1").arg(err));
            m_capture.reset();
        }
    }
    m_path = newRecordingPath(tr("Screen recording"), QStringLiteral("mp4"));
    m_writer = std::make_unique<LiveWriter>();
    if (!m_writer->open(m_path, o, &err)) {
        QMessageBox::warning(this, windowTitle(), tr("The recording could not start: %1").arg(err));
        m_writer.reset();
        m_capture.reset();
        QFile::remove(m_path);
        return;
    }
    m_clock.start();
    if (m_capture) {
        LiveWriter* w = m_writer.get();
        if (!m_capture->start([w](const std::int16_t* smp, int n) { w->addSound(smp, n); }, &err))
            m_capture.reset();
    }

    // The bar with the Stop button, in a corner of the recorded screen.
    m_bar = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_bar->setObjectName(QStringLiteral("recordBar"));
    m_bar->setAttribute(Qt::WA_StyledBackground);
    m_bar->setStyleSheet(QStringLiteral("#recordBar { background: #1d1b24; border: 1px solid #e64545; border-radius: 8px; }"
                                        "QLabel { color: white; font-weight: 600; }"));
    auto* row = new QHBoxLayout(m_bar);
    row->setContentsMargins(10, 6, 6, 6);
    auto* dot = new QLabel;
    dot->setPixmap(Icons::record(QColor(230, 60, 60)).pixmap(14, 14));
    row->addWidget(dot);
    m_barTime = new QLabel(QStringLiteral("00:00"));
    row->addWidget(m_barTime);
    auto* stopBtn = new QPushButton(tr("Stop"));
    connect(stopBtn, &QPushButton::clicked, this, &ScreenRecordDialog::stop);
    row->addWidget(stopBtn);
    m_bar->adjustSize();
    const QRect g = m_target->availableGeometry();
    m_bar->move(g.right() - m_bar->width() - 24, g.top() + 24);
    m_bar->show();

    hide();
    if (m_hide->isChecked() && parentWidget()) {
        m_hidden = parentWidget()->window();
        m_hidden->showMinimized();
    }
    m_dropped = 0;
    m_grabTimer.start(1000 / std::max(1, o.frameRate));
}

void ScreenRecordDialog::grab() {
    if (!m_writer || !m_target)
        return;
    const qint64 ms = m_clock.elapsed();
    if (m_barTime)
        m_barTime->setText(clockText(ms));
    // Skip pictures while the encoder is behind, rather than lag.
    if (m_writer->backlog() > m_fps->currentData().toInt()) {
        ++m_dropped;
        return;
    }
    QImage img = m_target->grabWindow(0).toImage();
    if (img.isNull())
        return;
    if (m_cursor->isChecked()) {
        const QPoint global = QCursor::pos(m_target);
        const QRect g = m_target->geometry();
        if (g.contains(global)) {
            const double dpr = double(img.width()) / std::max(1, g.width());
            const QPointF at = QPointF(global - g.topLeft()) * dpr;
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(at);
            p.scale(dpr, dpr);
            QPainterPath arrow;
            arrow.moveTo(0, 0);
            arrow.lineTo(0, 17);
            arrow.lineTo(4.5, 12.5);
            arrow.lineTo(7.5, 19);
            arrow.lineTo(10, 18);
            arrow.lineTo(7, 11.5);
            arrow.lineTo(13, 11.5);
            arrow.closeSubpath();
            p.setPen(QPen(Qt::black, 1.2));
            p.setBrush(Qt::white);
            p.drawPath(arrow);
        }
    }
    m_writer->addPicture(img, ms);
}

void ScreenRecordDialog::stop() {
    m_grabTimer.stop();
    if (m_capture)
        m_capture->stop();
    m_capture.reset();
    if (m_bar) {
        m_bar->deleteLater();
        m_bar = nullptr;
        m_barTime = nullptr;
    }
    if (m_hidden) {
        m_hidden->showNormal();
        m_hidden->raise();
        m_hidden->activateWindow();
        m_hidden = nullptr;
    }
    if (!m_writer)
        return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = m_writer->finish(&err);
    QApplication::restoreOverrideCursor();
    m_writer.reset();
    if (!ok) {
        QMessageBox::warning(parentWidget(), windowTitle(), tr("The recording could not be saved: %1").arg(err));
        QFile::remove(m_path);
        reject();
        return;
    }
    emit recorded(m_path);
    accept();
}

} // namespace lumora
