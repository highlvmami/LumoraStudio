#pragma once

#include <QDataStream>
#include <QList>
#include <QMimeData>
#include <QUuid>

namespace lumora {

inline constexpr char kMediaMimeType[] = "application/x-lumora-media";

inline QMimeData* encodeMediaIds(const QList<QUuid>& ids) {
    auto* mime = new QMimeData;
    QByteArray data;
    QDataStream s(&data, QIODevice::WriteOnly);
    s << ids;
    mime->setData(kMediaMimeType, data);
    return mime;
}

inline QList<QUuid> decodeMediaIds(const QMimeData* mime) {
    QList<QUuid> ids;
    QByteArray data = mime->data(kMediaMimeType);
    QDataStream s(&data, QIODevice::ReadOnly);
    s >> ids;
    return ids;
}

} // namespace lumora
