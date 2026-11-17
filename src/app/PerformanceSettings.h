#pragma once

#include <QObject>
#include <QVariantMap>

namespace komapaint {

class Settings;

class PerformanceSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int tileCacheLimitMB READ tileCacheLimitMB WRITE setTileCacheLimitMB NOTIFY changed)
    Q_PROPERTY(int undoHistoryLimit READ undoHistoryLimit WRITE setUndoHistoryLimit NOTIFY changed)
    Q_PROPERTY(bool wetSimEnabled READ wetSimEnabled WRITE setWetSimEnabled NOTIFY changed)
    Q_PROPERTY(int wetSimIntervalMs READ wetSimIntervalMs WRITE setWetSimIntervalMs NOTIFY changed)

public:
    explicit PerformanceSettings(QObject* parent = nullptr);

    void bindSettings(Settings* settings);
    bool saveToSettings() const;

    int tileCacheLimitMB() const { return m_tileCacheLimitMB; }
    int undoHistoryLimit() const { return m_undoHistoryLimit; }
    bool wetSimEnabled() const { return m_wetSimEnabled; }
    int wetSimIntervalMs() const { return m_wetSimIntervalMs; }

    void setTileCacheLimitMB(int mb);
    void setUndoHistoryLimit(int steps);
    void setWetSimEnabled(bool enabled);
    void setWetSimIntervalMs(int ms);

    Q_INVOKABLE QVariantMap toVariantMap() const;
    Q_INVOKABLE QVariantMap defaultVariantMap() const;
    Q_INVOKABLE bool applyVariantMap(const QVariantMap& map);
    Q_INVOKABLE void restoreDefaults();

signals:
    void changed();

private:
    Settings* m_settings = nullptr;
    int m_tileCacheLimitMB = 256;
    int m_undoHistoryLimit = 60;
    bool m_wetSimEnabled = true;
    int m_wetSimIntervalMs = 200;
};

} // namespace komapaint
