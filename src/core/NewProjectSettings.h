#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace komapaint {

// Canonical input settings for creating a new project.
//
// The physical canvas size is stored canonically in inches (a double), so
// switching measurement units never rounds the physical size and dimensions
// cannot drift no matter how many times units are toggled. Pixel dimensions
// are derived from the canonical inches and DPI on demand and are rounded to
// the nearest integer (half away from zero) at the last moment.
//
// This is a pure, UI-free value object: it knows nothing of CanvasDocument,
// layers or tiles. It is exposed to QML as a context property so the New
// Project dialog has a single source of truth for conversion and validation.
class NewProjectSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString units READ units WRITE setUnits NOTIFY unitsChanged)
    Q_PROPERTY(qreal dpi READ dpi WRITE setDpi NOTIFY dpiChanged)
    Q_PROPERTY(bool transparentBackground READ transparentBackground
               WRITE setTransparentBackground NOTIFY backgroundChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor
               NOTIFY backgroundChanged)
    // Derived pixel dimensions (read only): round(inches * dpi).
    Q_PROPERTY(int widthPixels READ widthPixels NOTIFY pixelsChanged)
    Q_PROPERTY(int heightPixels READ heightPixels NOTIFY pixelsChanged)

public:
    // Matches CanvasDocument::MaxDimension; kept here so core does not depend
    // on the canvas module.
    static constexpr int MaxDimension = 32768;
    Q_INVOKABLE int maxDimension() const { return MaxDimension; }

    explicit NewProjectSettings(QObject* parent = nullptr);

    // ---- Project ---------------------------------------------------------
    QString mode() const { return m_mode; }
    QString name() const { return m_name; }
    void setName(const QString& name);

    // ---- Units -----------------------------------------------------------
    QString units() const { return m_units; }
    void setUnits(const QString& units);

    // ---- Physical size (canonical, in inches) ----------------------------
    qreal widthInches() const { return m_widthInches; }
    qreal heightInches() const { return m_heightInches; }
    // Set the physical size expressed in the given unit; stored canonically
    // in inches without rounding the physical size.
    Q_INVOKABLE void setSizeInUnit(const QString& unit, qreal width, qreal height);
    // Read the physical size expressed in the given unit (for display).
    Q_INVOKABLE qreal widthInUnit(const QString& unit) const;
    Q_INVOKABLE qreal heightInUnit(const QString& unit) const;

    // ---- Resolution ------------------------------------------------------
    qreal dpi() const { return m_dpi; }
    void setDpi(qreal dpi);

    // ---- Background ------------------------------------------------------
    bool transparentBackground() const { return m_transparent; }
    void setTransparentBackground(bool transparent);
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor& color);

    // ---- Derived pixels (rounded) ----------------------------------------
    int widthPixels() const { return m_widthPixels; }
    int heightPixels() const { return m_heightPixels; }

    // ---- Validation ------------------------------------------------------
    bool isValid() const;
    QString validationError() const;

    // Reset every field to a sensible default illustration preset.
    Q_INVOKABLE void resetToDefaults();

    // Pure conversion helpers (testable without constructing the object).
    static qreal pixelsFromInches(qreal inches, qreal dpi);
    static qreal inchesFromPixels(qreal pixels, qreal dpi);
    static qreal convert(qreal value, const QString& from, const QString& to);
    static QStringList availableUnits();

private:
    void recomputePixels();

    QString m_mode = QStringLiteral("Illustration");
    QString m_name = QStringLiteral("Untitled");
    QString m_units = QStringLiteral("px");
    qreal   m_widthInches  = 8.5;   // US letter width
    qreal   m_heightInches = 11.0;  // US letter height
    qreal   m_dpi          = 300.0;
    bool    m_transparent  = true;
    QColor  m_backgroundColor = Qt::white;
    int     m_widthPixels  = 2550;
    int     m_heightPixels = 3300;
};

}  // namespace komapaint
