#pragma once

#include <QCoreApplication>
#include <QString>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace lumora {

// A colour look-up table as read from an Adobe / Resolve .cube file: either a
// 3D table (LUT_3D_SIZE, trilinear interpolation) or a per channel 1D table
// (LUT_1D_SIZE, linear interpolation). Input outside DOMAIN_MIN..DOMAIN_MAX
// is clamped.
class Lut {
    Q_DECLARE_TR_FUNCTIONS(Lut)

public:
    using Rgb = std::array<float, 3>;

    bool isNull() const { return m_size == 0; }
    bool is3D() const { return m_3d; }
    int size() const { return m_size; }
    const QString& title() const { return m_title; }

    // In-place lookup of one RGB triple.
    void apply(float rgb[3]) const;

    static Lut parseCube(const QByteArray& text, QString* error = nullptr);
    static Lut load(const QString& path, QString* error = nullptr);

    // Builds a 3D table by sampling `fn` on a size^3 grid over 0..1.
    static Lut sample(int size, const std::function<void(float rgb[3])>& fn, const QString& title = {});
    // Writes a .cube file (3D tables only).
    bool save(const QString& path, QString* error = nullptr) const;

private:
    std::vector<Rgb> m_data; // 3D: r fastest, then g, then b (the .cube order)
    int m_size = 0;
    bool m_3d = true;
    Rgb m_domainMin{0, 0, 0};
    Rgb m_domainMax{1, 1, 1};
    QString m_title;
};

// Process wide cache of loaded LUT files, keyed by path and modification
// time, safe to use from render threads.
namespace luts {
std::shared_ptr<const Lut> get(const QString& path, QString* error = nullptr);
void clear();
} // namespace luts

} // namespace lumora
