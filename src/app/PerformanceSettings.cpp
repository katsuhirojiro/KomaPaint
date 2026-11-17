#include "PerformanceSettings.h"

#include "../core/Settings.h"

#include <algorithm>

namespace komapaint {

PerformanceSettings::PerformanceSettings(QObject* parent)
    : QObject(parent)
{
}

void PerformanceSettings::bindSettings(Settings* settings)
{
    m_settings = settings;
    if (m_settings) {
        setTileCacheLimitMB(m_settings->value(QStringLiteral("performance/tileCacheLimitMB"), 256).toInt());
        setUndoHistoryLimit(m_settings->value(QStringLiteral("performance/undoHistoryLimit"), 60).toInt());
        setWetSimEnabled(m_settings->value(QStringLiteral("performance/wetSimEnabled"), true).toBool());
        setWetSimIntervalMs(m_settings->value(QStringLiteral("performance/wetSimIntervalMs"), 200).toInt());
    }
}

bool PerformanceSettings::saveToSettings() const
{
    if (!m_settings) return true;
    m_settings->setValue(QStringLiteral("performance/tileCacheLimitMB"), m_tileCacheLimitMB);
    m_settings->setValue(QStringLiteral("performance/undoHistoryLimit"), m_undoHistoryLimit);
    m_settings->setValue(QStringLiteral("performance/wetSimEnabled"), m_wetSimEnabled);
    m_settings->setValue(QStringLiteral("performance/wetSimIntervalMs"), m_wetSimIntervalMs);
    return m_settings->sync();
}

void PerformanceSettings::setTileCacheLimitMB(int mb)
{
    const int clamped = std::clamp(mb, 64, 2048);
    if (m_tileCacheLimitMB == clamped) return;
    m_tileCacheLimitMB = clamped;
    emit changed();
}

void PerformanceSettings::setUndoHistoryLimit(int steps)
{
    const int clamped = std::clamp(steps, 10, 200);
    if (m_undoHistoryLimit == clamped) return;
    m_undoHistoryLimit = clamped;
    emit changed();
}

void PerformanceSettings::setWetSimEnabled(bool enabled)
{
    if (m_wetSimEnabled == enabled) return;
    m_wetSimEnabled = enabled;
    emit changed();
}

void PerformanceSettings::setWetSimIntervalMs(int ms)
{
    const int clamped = std::clamp(ms, 100, 1000);
    if (m_wetSimIntervalMs == clamped) return;
    m_wetSimIntervalMs = clamped;
    emit changed();
}

QVariantMap PerformanceSettings::toVariantMap() const
{
    return QVariantMap{
        { QStringLiteral("tileCacheLimitMB"), m_tileCacheLimitMB },
        { QStringLiteral("undoHistoryLimit"), m_undoHistoryLimit },
        { QStringLiteral("wetSimEnabled"), m_wetSimEnabled },
        { QStringLiteral("wetSimIntervalMs"), m_wetSimIntervalMs }
    };
}

QVariantMap PerformanceSettings::defaultVariantMap() const
{
    return QVariantMap{
        { QStringLiteral("tileCacheLimitMB"), 256 },
        { QStringLiteral("undoHistoryLimit"), 60 },
        { QStringLiteral("wetSimEnabled"), true },
        { QStringLiteral("wetSimIntervalMs"), 200 }
    };
}

bool PerformanceSettings::applyVariantMap(const QVariantMap& map)
{
    setTileCacheLimitMB(map.value(QStringLiteral("tileCacheLimitMB"), m_tileCacheLimitMB).toInt());
    setUndoHistoryLimit(map.value(QStringLiteral("undoHistoryLimit"), m_undoHistoryLimit).toInt());
    setWetSimEnabled(map.value(QStringLiteral("wetSimEnabled"), m_wetSimEnabled).toBool());
    setWetSimIntervalMs(map.value(QStringLiteral("wetSimIntervalMs"), m_wetSimIntervalMs).toInt());
    return saveToSettings();
}

void PerformanceSettings::restoreDefaults()
{
    applyVariantMap(defaultVariantMap());
}

} // namespace komapaint
