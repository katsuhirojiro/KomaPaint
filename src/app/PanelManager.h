#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace komapaint {

// PanelManager tracks which workspace panels (Layers, Color, Timeline, ...)
// are currently visible. It is exposed to QML so menu items, keyboard
// shortcuts and panel close buttons can toggle state in one place.

class PanelManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList visiblePanels READ visiblePanels NOTIFY visiblePanelsChanged)

public:
    explicit PanelManager(QObject* parent = nullptr);

    Q_INVOKABLE bool isVisible(const QString& panelId) const;
    Q_INVOKABLE void setVisible(const QString& panelId, bool visible);
    Q_INVOKABLE void toggle(const QString& panelId);
    Q_INVOKABLE void resetLayout();

    QStringList visiblePanels() const;

signals:
    void panelVisibilityChanged(const QString& panelId, bool visible);
    void visiblePanelsChanged();

private:
    // Panels that are on by default in the initial workspace.
    struct Default { QString id; bool visible; };
    static const QList<Default> kDefaults;

    QMap<QString, bool> m_state;
};

}  // namespace komapaint
