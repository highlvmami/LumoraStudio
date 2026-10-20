#include "PropertiesPanel.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QVBoxLayout>

#include "export/ExportPlanner.h"
#include "project/Project.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace lumora {

namespace {

QString g_dim = QStringLiteral("#8b8b95");

QString row(const QString& k, const QString& v) {
    return QStringLiteral("<tr><td style='color:%3;padding-right:10px'>%1</td><td>%2</td></tr>")
        .arg(k.toHtmlEscaped(), v, g_dim);
}

QString section(const QString& title) {
    return QStringLiteral("<tr><td colspan=2 style='padding-top:10px'><b>%1</b></td></tr>").arg(title.toHtmlEscaped());
}

QString bitrate(std::int64_t bps) {
    if (bps <= 0)
        return QStringLiteral("—");
    if (bps >= 1'000'000)
        return QStringLiteral("%1 Mbit/s").arg(bps / 1e6, 0, 'f', 1);
    return QStringLiteral("%1 kbit/s").arg(bps / 1000);
}

QString nameOr(const char* s) { return s ? QString::fromUtf8(s) : QStringLiteral("—"); }

} // namespace

PropertiesPanel::PropertiesPanel(Project* project, QWidget* parent) : QWidget(parent), m_project(project) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_label = new QLabel;
    m_label->setTextFormat(Qt::RichText);
    m_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_label->setContentsMargins(10, 10, 10, 10);
    scroll->setWidget(m_label);
    layout->addWidget(scroll);
    connect(project->timeline(), &Timeline::changed, this, &PropertiesPanel::refresh);
    showNothing();
}

void PropertiesPanel::showClip(const QUuid& clipId) {
    m_clip = clipId;
    m_media = {};
    refresh();
}

void PropertiesPanel::showMedia(const QUuid& mediaId) {
    m_clip = {};
    m_media = mediaId;
    refresh();
}

void PropertiesPanel::showNothing() {
    m_clip = {};
    m_media = {};
    refresh();
}

void PropertiesPanel::refresh() {
    g_dim = palette().color(QPalette::PlaceholderText).name();
    QString html = QStringLiteral("<table cellspacing=2>");
    QUuid mediaId = m_media;
    const Clip* clip = m_clip.isNull() ? nullptr : m_project->timeline()->clip(m_clip);
    if (clip)
        mediaId = clip->mediaId;
    auto media = mediaId.isNull() ? nullptr : m_project->media()->find(mediaId);
    if (!media) {
        m_label->setText(QStringLiteral("<p style='color:%2'>%1</p>")
                             .arg(tr("Select a clip or a file to see its properties."), g_dim));
        return;
    }
    const MediaInfo& info = media->info;
    const AVRational rate = info.video ? info.video->frameRate : m_project->timeline()->frameRate();

    if (clip) {
        html += section(tr("Clip"));
        html += row(tr("In"), formatTimecode(clip->sourceIn, rate));
        html += row(tr("Out"), formatTimecode(clip->sourceOut, rate));
        html += row(tr("Duration"), formatSeconds(clip->duration()));
        html += row(tr("Timeline position"), formatTimecode(clip->timelineStart, m_project->timeline()->frameRate()));
        if (info.video && media->hasKeyframes()) {
            const auto kf = media->keyframesCopy();
            const Ticks fd = frameDuration(rate);
            const Ticks k = previousKeyframe(kf, clip->sourceIn, fd / 2);
            const int frames = static_cast<int>((clip->sourceIn - k + fd / 2) / fd);
            html += row(tr("Lossless cut"),
                        frames <= 0 ? QStringLiteral("<span style='color:#3fb950'>✔ %1</span>").arg(tr("starts on a key frame"))
                                    : QStringLiteral("<span style='color:#d29922'>%1</span>")
                                          .arg(tr("starts %n frame(s) after a key frame", nullptr, frames)));
        }
    }

    html += section(tr("File"));
    html += row(tr("Name"), QFileInfo(info.path).fileName().toHtmlEscaped());
    html += row(tr("Folder"), QDir::toNativeSeparators(QFileInfo(info.path).absolutePath()).toHtmlEscaped());
    html += row(tr("Format"), info.formatLongName.toHtmlEscaped());
    html += row(tr("Size"), QLocale().formattedDataSize(info.fileSize));
    if (info.kind == MediaKind::Image) {
        html += row(tr("Resolution"), QStringLiteral("%1 × %2").arg(info.imageWidth).arg(info.imageHeight));
        html += row(tr("Bit depth"), QStringLiteral("%1-bit").arg(info.imageBitDepth));
    } else {
        html += row(tr("Duration"), formatSeconds(info.duration));
        html += row(tr("Bit rate"), bitrate(info.bitRate));
    }

    if (info.video) {
        const VideoStreamInfo& v = *info.video;
        html += section(tr("Video"));
        html += row(tr("Codec"), (v.codecName.toUpper() + (v.profile.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(v.profile))).toHtmlEscaped());
        html += row(tr("Resolution"), QStringLiteral("%1 × %2").arg(v.width).arg(v.height));
        html += row(tr("Frame rate"), formatFrameRate(v.frameRate) +
                                         (v.variableFrameRate ? QStringLiteral(" (%1)").arg(tr("variable")) : QString()));
        html += row(tr("Pixel format"), nameOr(av_get_pix_fmt_name(v.pixelFormat)));
        html += row(tr("Bit depth"), QStringLiteral("%1-bit").arg(v.bitDepth));
        const bool untagged = v.colorPrimaries == AVCOL_PRI_UNSPECIFIED && v.colorTrc == AVCOL_TRC_UNSPECIFIED &&
                              v.colorSpace == AVCOL_SPC_UNSPECIFIED;
        html += row(tr("Colour"), untagged ? tr("not tagged")
                                           : QStringLiteral("%1 / %2 / %3")
                                                 .arg(nameOr(av_color_primaries_name(v.colorPrimaries)),
                                                      nameOr(av_color_transfer_name(v.colorTrc)),
                                                      nameOr(av_color_space_name(v.colorSpace))));
        html += row(tr("Range"), v.colorRange == AVCOL_RANGE_JPEG    ? tr("full")
                                 : v.colorRange == AVCOL_RANGE_MPEG ? tr("limited")
                                                                    : tr("not tagged"));
        if (v.isHdr())
            html += row(tr("HDR"), v.colorTrc == AVCOL_TRC_SMPTE2084 ? QStringLiteral("HDR10 (PQ)") : QStringLiteral("HLG"));
        if (v.rotation)
            html += row(tr("Rotation"), QStringLiteral("%1°").arg(v.rotation));
        html += row(tr("B-frames"), v.hasBFrames ? tr("yes") : tr("no"));
        html += row(tr("Bit rate"), bitrate(v.bitRate));
        if (media->hasKeyframes()) {
            const auto kf = media->keyframesCopy();
            if (kf.size() > 1)
                html += row(tr("Key frames"), tr("%1 (every %2 s on average)")
                                                  .arg(kf.size())
                                                  .arg(ticksToSeconds(kf.back() - kf.front()) / (kf.size() - 1), 0, 'f', 2));
        }
    }
    if (info.audio) {
        const AudioStreamInfo& a = *info.audio;
        html += section(tr("Audio"));
        html += row(tr("Codec"), a.codecName.toUpper().toHtmlEscaped());
        html += row(tr("Sample rate"), QStringLiteral("%1 Hz").arg(a.sampleRate));
        html += row(tr("Channels"), QStringLiteral("%1 (%2)").arg(a.channels).arg(a.channelLayout.toHtmlEscaped()));
        html += row(tr("Sample format"), a.sampleFormat.toHtmlEscaped());
        html += row(tr("Bit rate"), bitrate(a.bitRate));
    }
    html += QStringLiteral("</table>");
    m_label->setText(html);
}

} // namespace lumora
