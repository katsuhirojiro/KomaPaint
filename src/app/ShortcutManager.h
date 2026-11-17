#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

namespace komapaint {

class Settings;

class ShortcutManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList actions READ actions NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantMap bindings READ bindingsMap NOTIFY shortcutsChanged)

public:
    struct ActionEntry {
        QString id;
        QString label;
        QString category;
        QString defaultSequence;
        QString sequence;
    };

    explicit ShortcutManager(QObject* parent = nullptr);

    void bindSettings(Settings* settings);
    bool saveToSettings() const;

    QVariantList actions() const;
    QVariantMap bindingsMap() const;
    Q_INVOKABLE QVariantMap defaultBindingsMap() const;

    Q_INVOKABLE QString keyFor(const QString& actionId) const;
    Q_INVOKABLE QString defaultKeyFor(const QString& actionId) const;
    Q_INVOKABLE QString labelFor(const QString& actionId) const;
    Q_INVOKABLE QString normalizeSequence(const QString& sequence) const;
    Q_INVOKABLE QString formatKeyEvent(int key, int modifiers) const;
    Q_INVOKABLE QString findConflict(const QString& actionId,
                                     const QString& sequence,
                                     const QVariantMap& draftMap = QVariantMap()) const;
    Q_INVOKABLE bool applyBindings(const QVariantMap& bindings);
    Q_INVOKABLE void restoreDefaults();

signals:
    void shortcutsChanged();

private:
    void resetToDefaultsInternal();

    Settings* m_settings = nullptr;
    std::vector<ActionEntry> m_actions;
};

} // namespace komapaint
