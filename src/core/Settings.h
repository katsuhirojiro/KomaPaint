#pragma once

#include <QObject>
#include <QSize>
#include <QString>
#include <QVariant>

class QSettings;

namespace komapaint {

// Thin wrapper around QSettings that exposes typed getters/setters for the
// application's user-configurable preferences. All persistence goes through
// this class so the rest of the codebase never touches QSettings directly.

class Settings : public QObject
{
    Q_OBJECT
    // Window geometry is bound directly from QML (MainWindow.qml), so it
    // must be exposed as real properties — plain C++ methods are invisible
    // to the QML engine.
    Q_PROPERTY(QSize windowSize      READ windowSize      WRITE setWindowSize      NOTIFY windowSizeChanged)
    Q_PROPERTY(bool  windowMaximized READ windowMaximized WRITE setWindowMaximized NOTIFY windowMaximizedChanged)

public:
    explicit Settings(const QString& organization, const QString& application,
                      QObject* parent = nullptr);
    ~Settings() override;

    Q_INVOKABLE QVariant value(const QString& key,
                               const QVariant& defaultValue = {}) const;
    Q_INVOKABLE void     setValue(const QString& key, const QVariant& value);

    // Convenience accessors for values used by the shell.
    QSize windowSize() const;
    void  setWindowSize(const QSize& size);

    bool   windowMaximized() const;
    void   setWindowMaximized(bool maximized);

    QString language() const;
    void    setLanguage(const QString& locale);

    QString lastOpenDirectory() const;
    void    setLastOpenDirectory(const QString& path);

signals:
    void valueChanged(const QString& key);
    void windowSizeChanged();
    void windowMaximizedChanged();

private:
    QSettings* const d;
};

}  // namespace komapaint
