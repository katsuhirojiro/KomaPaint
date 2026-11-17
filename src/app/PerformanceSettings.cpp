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
        setUndoHistoryLimit(migratedUndoHistoryLimit());
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
    // 0 = unlimited. There is no upper bound: a large number is a legitimate
    // choice, and history is limited by real memory rather than a fixed quota.
    const int clamped = std::max(steps, 0);
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
        { QStringLiteral("undoHistoryLimit"), 0 },
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

// Earlier builds wrote 60 into every profile as the forced default of a 10-200
// stroke cap, whether or not the user ever chose it. Exactly 60 therefore means
// "never deliberately set", and migrates once to unlimited (0). Any other value
// is a real choice and is kept verbatim.
int PerformanceSettings::migratedUndoHistoryLimit()
{
    constexpr int legacyForcedDefault = 60;
    constexpr auto migratedKey = "performance/undoUnlimitedMigrated";
    int stored = m_settings ? m_settings->value(QStringLiteral("performance/undoHistoryLimit"), 0).toInt() : 0;
    if (m_settings && !m_settings->value(QStringLiteral(migratedKey), false).toBool()) {
        if (stored == legacyForcedDefault) stored = 0;
        m_settings->setValue(QStringLiteral(migratedKey), true);
        m_settings->setValue(QStringLiteral("performance/undoHistoryLimit"), stored);
    }
    return std::max(stored, 0);
}

void PerformanceSettings::restoreDefaults()
{
    applyVariantMap(defaultVariantMap());
}

} // namespace komapaint
