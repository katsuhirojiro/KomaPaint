#include "PanelManager.h"

#include "DockModel.h"

namespace komapaint {

PanelManager::PanelManager(QObject* parent) : QObject(parent)
{
    m_owned = std::make_unique<DockModel>();
    setDockModel(m_owned.get());
}

PanelManager::~PanelManager() = default;

void PanelManager::setDockModel(DockModel* model)
{
    if (!model || model == m_dock) return;
    if (m_dock) disconnect(m_dock, nullptr, this, nullptr);
    m_dock = model;
    m_visible = m_dock->visiblePanels();
    connect(m_dock, &DockModel::layoutChanged, this, &PanelManager::syncVisibility);
    emit visiblePanelsChanged();
}

void PanelManager::syncVisibility()
{
    if (!m_dock) return;
    const QStringList before = m_visible;
    m_visible = m_dock->visiblePanels();
    for (const auto& entry : DockModel::panelTable()) {
        const bool was = before.contains(entry.id);
        const bool now = m_visible.contains(entry.id);
        if (was != now) emit panelVisibilityChanged(entry.id, now);
    }
    if (before != m_visible) emit visiblePanelsChanged();
}

bool PanelManager::isVisible(const QString& panelId) const
{
    return m_dock ? m_dock->isVisible(panelId) : false;
}

void PanelManager::setVisible(const QString& panelId, bool visible)
{
    if (!m_dock) return;
    if (isVisible(panelId) == visible) return;
    if (visible) m_dock->showPanel(panelId);
    else m_dock->hidePanel(panelId);
}

void PanelManager::toggle(const QString& panelId)
{
    setVisible(panelId, !isVisible(panelId));
}

void PanelManager::resetLayout()
{
    if (m_dock) m_dock->resetToDefault();
}

QVariantList PanelManager::areas() const
{
    return m_dock ? m_dock->areas() : QVariantList();
}

QStringList PanelManager::collapsedAreas() const
{
    return m_dock ? m_dock->collapsedAreas() : QStringList();
}

bool PanelManager::hasArea(const QString& side) const
{
    return m_dock && m_dock->hasArea(side);
}

QString PanelManager::areaSideOf(int nodeId) const
{
    return m_dock ? m_dock->areaSideOf(nodeId) : QString();
}

QString PanelManager::areaSideOfChild(int nodeId) const
{
    return m_dock ? m_dock->areaSideOfChild(nodeId) : QString();
}

bool PanelManager::isAreaCollapsed(const QString& side) const
{
    return m_dock && m_dock->isAreaCollapsed(side);
}

bool PanelManager::setAreaCollapsed(const QString& side, bool collapsed)
{
    return m_dock && m_dock->setAreaCollapsed(side, collapsed);
}

bool PanelManager::toggleArea(const QString& side)
{
    return m_dock && m_dock->toggleArea(side);
}

QStringList PanelManager::visiblePanels() const
{
    return m_dock ? m_dock->visiblePanels() : QStringList();
}

}  // namespace komapaint
