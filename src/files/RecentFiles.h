#pragma once

#include <QObject>
#include <QStringList>

namespace komapaint {

// Keeps the most recently opened document paths for the File menu.
// Persists in Settings.

class Settings;

class RecentFiles : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList files READ files NOTIFY filesChanged)
    Q_PROPERTY(int        limit READ limit WRITE setLimit NOTIFY limitChanged)

public:
    explicit RecentFiles(Settings* settings, QObject* parent = nullptr);

    QStringList files() const { return m_files; }
    int         limit() const { return m_limit; }

    Q_INVOKABLE void add(const QString& path);
    Q_INVOKABLE void remove(const QString& path);
    Q_INVOKABLE void clear();

public slots:
    void setLimit(int limit);

signals:
    void filesChanged();
    void limitChanged();

private:
    void load();
    void save();

    Settings*   m_settings;
    QStringList m_files;
    int         m_limit = 10;
};

}  // namespace komapaint
