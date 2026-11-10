#include "NewProjectSettings.h"

#include <QStringList>
#include <cmath>
#include <limits>

namespace komapaint {

namespace {

// One inch in millimetres and centimetres (exact by definition).
constexpr qreal kMmPerInch = 25.4;
constexpr qreal kCmPerInch = 2.54;

// Convert any supported unit to inches.
qreal toInches(qreal value, const QString& unit)
{
    if (unit == QStringLiteral("mm")) return value / kMmPerInch;
    if (unit == QStringLiteral("cm")) return value / kCmPerInch;
    if (unit == QStringLiteral("inches")) return value;
    return std::numeric_limits<qreal>::quiet_NaN();
}

qreal fromInches(qreal inches, const QString& unit)
{
    if (unit == QStringLiteral("mm")) return inches * kMmPerInch;
    if (unit == QStringLiteral("cm")) return inches * kCmPerInch;
    if (unit == QStringLiteral("inches")) return inches;
    return std::numeric_limits<qreal>::quiet_NaN();
}

bool isValidUnit(const QString& unit)
{
    return unit == QStringLiteral("px") || unit == QStringLiteral("mm") ||
           unit == QStringLiteral("cm") || unit == QStringLiteral("inches");
}

}  // namespace

NewProjectSettings::NewProjectSettings(QObject* parent) : QObject(parent)
{
    connect(this, &NewProjectSettings::nameChanged, this, &NewProjectSettings::validityChanged);
    connect(this, &NewProjectSettings::pixelsChanged, this, &NewProjectSettings::validityChanged);
    connect(this, &NewProjectSettings::dpiChanged, this, &NewProjectSettings::validityChanged);
    connect(this, &NewProjectSettings::backgroundChanged, this, &NewProjectSettings::validityChanged);
    recomputePixels();
}

void NewProjectSettings::setName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (m_name == trimmed) return;
    m_name = trimmed;
    emit nameChanged();
}

void NewProjectSettings::setUnits(const QString& units)
{
    if (!isValidUnit(units)) return;
    if (m_units == units) return;
    m_units = units;
    emit unitsChanged();
    emit displayChanged();
}

void NewProjectSettings::setSizeInUnit(const QString& unit, qreal width, qreal height)
{
    if (!isValidUnit(unit)) return;
    if (!std::isfinite(width) || !std::isfinite(height)) return;
    if (width <= 0 || height <= 0) return;

    qreal widthInches = 0.0;
    qreal heightInches = 0.0;

    if (unit == QStringLiteral("px")) {
        // Pixels -> inches depends on the current DPI. Guard against a
        // zero DPI so we never store a non-finite canonical size.
        if (m_dpi <= 0 || !std::isfinite(m_dpi)) return;
        widthInches  = inchesFromPixels(width, m_dpi);
        heightInches = inchesFromPixels(height, m_dpi);
    } else {
        widthInches  = toInches(width, unit);
        heightInches = toInches(height, unit);
    }

    if (!std::isfinite(widthInches) || !std::isfinite(heightInches)) return;

    m_widthInches  = widthInches;
    m_heightInches = heightInches;
    recomputePixels();
    emit displayChanged();
}

qreal NewProjectSettings::widthInUnit(const QString& unit) const
{
    if (!isValidUnit(unit)) return m_widthInches;
    if (unit == QStringLiteral("px")) {
        return pixelsFromInches(m_widthInches, m_dpi);
    }
    return fromInches(m_widthInches, unit);
}

qreal NewProjectSettings::heightInUnit(const QString& unit) const
{
    if (!isValidUnit(unit)) return m_heightInches;
    if (unit == QStringLiteral("px")) {
        return pixelsFromInches(m_heightInches, m_dpi);
    }
    return fromInches(m_heightInches, unit);
}

void NewProjectSettings::setDpi(qreal dpi)
{
    if (!std::isfinite(dpi) || dpi <= 0) return;
    if (qFuzzyCompare(dpi, m_dpi)) return;
    m_dpi = dpi;
    recomputePixels();
    emit dpiChanged();
    emit displayChanged();
    emit pixelsChanged();
}

void NewProjectSettings::setTransparentBackground(bool transparent)
{
    if (m_transparent == transparent) return;
    m_transparent = transparent;
    emit backgroundChanged();
}

void NewProjectSettings::setBackgroundColor(const QString& color)
{
    if (m_backgroundColor == color) return;
    m_backgroundColor = color;
    emit backgroundChanged();
}

void NewProjectSettings::recomputePixels()
{
    const int width  = pixelsFromInches(m_widthInches, m_dpi);
    const int height = pixelsFromInches(m_heightInches, m_dpi);
    if (m_widthPixels != width || m_heightPixels != height) {
        m_widthPixels = width;
        m_heightPixels = height;
        emit pixelsChanged();
    }
}

bool NewProjectSettings::isValid() const
{
    if (m_name.isEmpty()) return false;
    if (!std::isfinite(m_widthInches) || !std::isfinite(m_heightInches)) return false;
    if (m_widthInches <= 0 || m_heightInches <= 0) return false;
    if (!std::isfinite(m_dpi) || m_dpi <= 0) return false;
    if (m_backgroundColor.isEmpty()) return false;
    // Derived pixel dimensions must fit within the document limits.
    if (m_widthPixels < 1 || m_widthPixels > MaxDimension) return false;
    if (m_heightPixels < 1 || m_heightPixels > MaxDimension) return false;
    return true;
}

QString NewProjectSettings::validationError() const
{
    if (m_name.isEmpty()) return QStringLiteral("Enter a project name.");
    if (!std::isfinite(m_widthInches) || !std::isfinite(m_heightInches) ||
        m_widthInches <= 0 || m_heightInches <= 0) {
        return QStringLiteral("Canvas dimensions must be greater than zero.");
    }
    if (!std::isfinite(m_dpi) || m_dpi <= 0) {
        return QStringLiteral("Resolution (DPI) must be greater than zero.");
    }
    if (m_backgroundColor.isEmpty()) {
        return QStringLiteral("Background colour is required.");
    }
    if (m_widthPixels < 1 || m_heightPixels < 1) {
        return QStringLiteral("Canvas dimensions are too small.");
    }
    if (m_widthPixels > MaxDimension ||
        m_heightPixels > MaxDimension) {
        return QStringLiteral("Canvas dimensions are too large (max 32768 px).");
    }
    return QString();
}

void NewProjectSettings::resetToDefaults()
{
    if (m_name != QStringLiteral("Untitled")) {
        m_name = QStringLiteral("Untitled");
        emit nameChanged();
    }
    setUnits(QStringLiteral("px"));
    m_widthInches = 8.5;
    m_heightInches = 11.0;
    if (!qFuzzyCompare(m_dpi, 300.0)) m_dpi = 300.0;
    m_transparent = true;
    m_backgroundColor = QStringLiteral("#FFFFFF");
    recomputePixels();
    emit dpiChanged();
    emit backgroundChanged();
    emit displayChanged();
    emit pixelsChanged();
}

qreal NewProjectSettings::pixelsFromInches(qreal inches, qreal dpi)
{
    if (!std::isfinite(inches) || !std::isfinite(dpi) || dpi <= 0) return 0;
    return std::round(inches * dpi);
}

qreal NewProjectSettings::inchesFromPixels(qreal pixels, qreal dpi)
{
    if (!std::isfinite(pixels) || !std::isfinite(dpi) || dpi <= 0) return 0;
    return pixels / dpi;
}

qreal NewProjectSettings::convert(qreal value, const QString& from, const QString& to)
{
    if (!std::isfinite(value)) return value;
    if (!isValidUnit(from) || !isValidUnit(to)) return value;
    if (from == to) return value;
    // Pixels cannot be converted without a DPI; this path is not used for
    // project creation, so leave values involving pixels unchanged.
    if (from == QStringLiteral("px") || to == QStringLiteral("px")) return value;
    return fromInches(toInches(value, from), to);
}

QStringList NewProjectSettings::availableUnits() const
{
    return { QStringLiteral("px"), QStringLiteral("mm"),
             QStringLiteral("cm"), QStringLiteral("inches") };
}

}  // namespace komapaint

#include "NewProjectSettings.moc"
