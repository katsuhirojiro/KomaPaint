#include "PanelManager.h"

namespace komapaint {

const QList<PanelManager::Default> PanelManager::kDefaults = {
    { QStringLiteral("subtool"),      true  },
    { QStringLiteral("toolproperty"), true  },
    { QStringLiteral("brushsize"),    true  },
    { QStringLiteral("layers"),     true  },
    { QStringLiteral("properties"), true  },
    { QStringLiteral("color"),      true  },
    { QStringLiteral("timeline"),   false },
    { QStringLiteral("assets"),     false },
    { QStringLiteral("history"),    false },
    { QStringLiteral("navigator"),  false },
};

PanelManager::PanelManager(QObject* parent) : QObject(parent)
{
    for (const auto& d : kDefaults) {
        m_state.insert(d.id, d.visible);
    }
}

bool PanelManager::isVisible(const QString& panelId) const
{
    return m_state.value(panelId, false);
}

void PanelManager::setVisible(const QString& panelId, bool visible)
{
    if (m_state.value(panelId, false) == visible) return;
    m_state.insert(panelId, visible);
    emit panelVisibilityChanged(panelId, visible);
    emit visiblePanelsChanged();
}

void PanelManager::toggle(const QString& panelId)
{
    setVisible(panelId, !isVisible(panelId));
}

void PanelManager::resetLayout()
{
    m_state.clear();
    for (const auto& d : kDefaults) {
        m_state.insert(d.id, d.visible);
    }
    emit visiblePanelsChanged();
}

QStringList PanelManager::visiblePanels() const
{
    QStringList result;
    for (auto it = m_state.constBegin(); it != m_state.constEnd(); ++it) {
        if (it.value()) result.append(it.key());
    }
    return result;
}

}  // namespace komapaint
