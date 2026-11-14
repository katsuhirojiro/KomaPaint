#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>

namespace komapaint {

class DockModel;

// Compatibility façade over DockModel.
//
// The workspace layout is owned by DockModel (one tree of splits, tab groups
// and floating windows). PanelManager keeps the small API the rest of the
// application already used — isVisible / setVisible / toggle / resetLayout and
// the visibility signals — and forwards it to that single model, so there is
// exactly one source of truth. It can also stand alone (it then owns a private
// DockModel), which keeps the existing unit tests and any embedder working
// without an application object.
class PanelManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList visiblePanels READ visiblePanels NOTIFY visiblePanelsChanged)

public:
    explicit PanelManager(QObject* parent = nullptr);
    ~PanelManager() override;

    // The application passes its shared model (with persistence attached).
    void setDockModel(DockModel* model);
    DockModel* dockModel() const { return m_dock; }

    Q_INVOKABLE bool isVisible(const QString& panelId) const;
    Q_INVOKABLE void setVisible(const QString& panelId, bool visible);
    Q_INVOKABLE void toggle(const QString& panelId);
    Q_INVOKABLE void resetLayout();

    // Dock areas: the bands of panels around the canvas. Collapsing one is a
    // hide of the whole band, so the menus can drive it from QML while the
    // model keeps the panels, tabs and splitter fractions untouched.
    Q_INVOKABLE QVariantList areas() const;
    Q_INVOKABLE QStringList collapsedAreas() const;
    Q_INVOKABLE bool hasArea(const QString& side) const;
    Q_INVOKABLE QString areaSideOf(int nodeId) const;
    Q_INVOKABLE QString areaSideOfChild(int nodeId) const;
    Q_INVOKABLE bool isAreaCollapsed(const QString& side) const;
    Q_INVOKABLE bool setAreaCollapsed(const QString& side, bool collapsed);
    Q_INVOKABLE bool toggleArea(const QString& side);

    QStringList visiblePanels() const;

signals:
    void panelVisibilityChanged(const QString& panelId, bool visible);
    void visiblePanelsChanged();

private:
    void syncVisibility();

    std::unique_ptr<DockModel> m_owned;
    DockModel* m_dock = nullptr;
    QStringList m_visible;
};

}  // namespace komapaint
