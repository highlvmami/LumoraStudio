#include "Lut.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace lumora {

namespace {

constexpr int kMaxSize3D = 256;
constexpr int kMaxSize1D = 65536;

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

void Lut::apply(float rgb[3]) const {
    if (m_size < 2)
        return;
    const float n = static_cast<float>(m_size - 1);
    float pos[3];
    int i0[3], i1[3];
    float f[3];
    for (int c = 0; c < 3; ++c) {
        const float span = m_domainMax[c] - m_domainMin[c];
        float v = span > 0 ? (rgb[c] - m_domainMin[c]) / span : 0.0f;
        if (!(v >= 0.0f)) // also catches NaN
            v = 0.0f;
        v = std::min(v, 1.0f);
        pos[c] = v * n;
        i0[c] = std::min(static_cast<int>(pos[c]), m_size - 2);
        i1[c] = i0[c] + 1;
        f[c] = pos[c] - i0[c];
    }
    if (!m_3d) {
        for (int c = 0; c < 3; ++c)
            rgb[c] = lerp(m_data[i0[c]][c], m_data[i1[c]][c], f[c]);
        return;
    }
    const int s = m_size;
    auto at = [&](int r, int g, int b) -> const Rgb& { return m_data[(static_cast<size_t>(b) * s + g) * s + r]; };
    for (int c = 0; c < 3; ++c) {
        const float c00 = lerp(at(i0[0], i0[1], i0[2])[c], at(i1[0], i0[1], i0[2])[c], f[0]);
        const float c10 = lerp(at(i0[0], i1[1], i0[2])[c], at(i1[0], i1[1], i0[2])[c], f[0]);
        const float c01 = lerp(at(i0[0], i0[1], i1[2])[c], at(i1[0], i0[1], i1[2])[c], f[0]);
        const float c11 = lerp(at(i0[0], i1[1], i1[2])[c], at(i1[0], i1[1], i1[2])[c], f[0]);
        rgb[c] = lerp(lerp(c00, c10, f[1]), lerp(c01, c11, f[1]), f[2]);
    }
}

Lut Lut::parseCube(const QByteArray& text, QString* error) {
    auto fail = [error](const QString& msg) {
        if (error)
            *error = msg;
        return Lut{};
    };
    Lut lut;
    int size3 = 0, size1 = 0;
    size_t expected = 0;
    int lineNo = 0;
    for (QByteArray line : text.split('\n')) {
        ++lineNo;
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const char first = line.at(0);
        if ((first >= '0' && first <= '9') || first == '-' || first == '+' || first == '.') {
            if (expected == 0)
                return fail(tr("Line %1: table data before LUT_3D_SIZE or LUT_1D_SIZE").arg(lineNo));
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() < 3)
                return fail(tr("Line %1: expected three numbers").arg(lineNo));
            Rgb v;
            for (int c = 0; c < 3; ++c) {
                bool ok = false;
                v[c] = parts[c].toFloat(&ok);
                if (!ok || !std::isfinite(v[c]))
                    return fail(tr("Line %1: invalid number").arg(lineNo));
            }
            if (lut.m_data.size() >= expected)
                return fail(tr("Line %1: more table entries than the declared size").arg(lineNo));
            lut.m_data.push_back(v);
            continue;
        }
        const int space = line.indexOf(' ');
        const QByteArray key = (space < 0 ? line : line.left(space)).toUpper();
        const QByteArray rest = space < 0 ? QByteArray() : line.mid(space + 1).trimmed();
        if (key == "TITLE") {
            lut.m_title = QString::fromUtf8(rest).remove(QLatin1Char('"'));
        } else if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
            if (expected != 0)
                return fail(tr("The file declares more than one table"));
            bool ok = false;
            const int n = rest.toInt(&ok);
            const bool is3 = key == "LUT_3D_SIZE";
            if (!ok || n < 2 || n > (is3 ? kMaxSize3D : kMaxSize1D))
                return fail(tr("Line %1: unsupported table size").arg(lineNo));
            (is3 ? size3 : size1) = n;
            expected = is3 ? static_cast<size_t>(n) * n * n : static_cast<size_t>(n);
            lut.m_data.reserve(expected);
        } else if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX" || key == "LUT_3D_INPUT_RANGE" ||
                   key == "LUT_1D_INPUT_RANGE") {
            const QList<QByteArray> parts = rest.simplified().split(' ');
            Rgb lo = lut.m_domainMin, hi = lut.m_domainMax;
            if (key.endsWith("INPUT_RANGE")) {
                if (parts.size() < 2)
                    return fail(tr("Line %1: expected two numbers").arg(lineNo));
                lo.fill(parts[0].toFloat());
                hi.fill(parts[1].toFloat());
            } else {
                if (parts.size() < 3)
                    return fail(tr("Line %1: expected three numbers").arg(lineNo));
                Rgb& target = key == "DOMAIN_MIN" ? lo : hi;
                for (int c = 0; c < 3; ++c)
                    target[c] = parts[c].toFloat();
            }
            lut.m_domainMin = lo;
            lut.m_domainMax = hi;
        }
        // Other keywords (vendor extensions) are ignored.
    }
    if (expected == 0)
        return fail(tr("No LUT_3D_SIZE or LUT_1D_SIZE found"));
    if (lut.m_data.size() != expected)
        return fail(tr("The table has %1 entries, %2 expected").arg(lut.m_data.size()).arg(expected));
    lut.m_3d = size3 > 0;
    lut.m_size = lut.m_3d ? size3 : size1;
    return lut;
}

Lut Lut::load(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    if (f.size() > 512LL * 1024 * 1024) {
        if (error)
            *error = tr("The file is too large");
        return {};
    }
    Lut lut = parseCube(f.readAll(), error);
    if (!lut.isNull() && lut.m_title.isEmpty())
        lut.m_title = QFileInfo(path).completeBaseName();
    return lut;
}

Lut Lut::sample(int size, const std::function<void(float rgb[3])>& fn, const QString& title) {
    Lut lut;
    size = std::clamp(size, 2, kMaxSize3D);
    lut.m_size = size;
    lut.m_3d = true;
    lut.m_title = title;
    lut.m_data.resize(static_cast<size_t>(size) * size * size);
    const float n = static_cast<float>(size - 1);
    size_t i = 0;
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r) {
                float rgb[3] = {r / n, g / n, b / n};
                fn(rgb);
                lut.m_data[i++] = {rgb[0], rgb[1], rgb[2]};
            }
    return lut;
}

bool Lut::save(const QString& path, QString* error) const {
    if (isNull() || !m_3d) {
        if (error)
            *error = tr("Only 3D tables can be saved");
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QByteArray out;
    out.reserve(static_cast<qsizetype>(m_data.size()) * 30 + 200);
    out += "# Created with LumoraStudio\n";
    if (!m_title.isEmpty())
        out += "TITLE \"" + m_title.toUtf8().replace('"', '\'') + "\"\n";
    out += "LUT_3D_SIZE " + QByteArray::number(m_size) + "\n";
    out += "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    for (const Rgb& v : m_data) {
        for (int c = 0; c < 3; ++c) {
            out += QByteArray::number(static_cast<double>(v[c]), 'f', 6);
            out += c < 2 ? ' ' : '\n';
        }
    }
    f.write(out);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

namespace luts {

namespace {
struct Entry {
    QDateTime modified;
    std::shared_ptr<const Lut> lut;
    QString error;
};
std::mutex g_mutex;
std::map<QString, Entry> g_cache;
} // namespace

std::shared_ptr<const Lut> get(const QString& path, QString* error) {
    if (path.isEmpty())
        return {};
    const QDateTime modified = QFileInfo(path).lastModified();
    std::lock_guard lock(g_mutex);
    auto it = g_cache.find(path);
    if (it == g_cache.end() || it->second.modified != modified) {
        Entry e;
        e.modified = modified;
        Lut lut = Lut::load(path, &e.error);
        if (!lut.isNull())
            e.lut = std::make_shared<const Lut>(std::move(lut));
        if (g_cache.size() > 32)
            g_cache.clear();
        it = g_cache.insert_or_assign(path, std::move(e)).first;
    }
    if (error)
        *error = it->second.error;
    return it->second.lut;
}

void clear() {
    std::lock_guard lock(g_mutex);
    g_cache.clear();
}

} // namespace luts

} // namespace lumora
