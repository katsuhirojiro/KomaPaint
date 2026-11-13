#include "SubToolLibrary.h"
#include "BrushSettings.h"
#include <QVariantMap>

namespace komapaint {
namespace {
using V = SubToolLibrary::Values;
// size, opacity, flow (density), spacing, hardness, stabilization, textured.
// Every value lies inside the range BrushSettings accepts.
V values(int size, int opacity, int flow, int spacing, int hardness, int stabilization, bool textured = false)
{
    return V{size, opacity, flow, spacing, hardness, stabilization, textured};
}
}

SubToolLibrary::SubToolLibrary(BrushSettings* brush, QObject* parent) : QObject(parent), m_brush(brush)
{
    auto add = [this](const char* tool, bool erase, std::initializer_list<std::pair<const char*, V>> list) {
        Group group; group.tool = QString::fromLatin1(tool); group.erase = erase;
        for (const auto& [name, v] : list) group.entries.push_back({tr(name), v, v});
        m_groups.push_back(std::move(group));
    };
    add("pen", false, {
        {QT_TR_NOOP("Fine pen"),      values(4, 100, 100, 5, 100, 20)},
        {QT_TR_NOOP("Marker"),        values(18, 100, 100, 8, 100, 10)},
        {QT_TR_NOOP("Rough pen"),     values(8, 100, 90, 8, 100, 10, true)},
    });
    add("pencil", false, {
        {QT_TR_NOOP("Sketch pencil"), values(6, 90, 70, 10, 100, 0, true)},
        {QT_TR_NOOP("Soft pencil"),   values(12, 70, 45, 10, 60, 0, true)},
    });
    add("brush", false, {
        // The first brush equals the BrushSettings defaults, so a fresh
        // workspace paints exactly as it did before sub tools existed.
        {QT_TR_NOOP("Round brush"),   values(16, 100, 100, 15, 100, 0)},
        {QT_TR_NOOP("Soft brush"),    values(40, 100, 35, 10, 30, 0)},
        {QT_TR_NOOP("Dry brush"),     values(32, 90, 55, 15, 90, 0, true)},
    });
    add("airbrush", false, {
        {QT_TR_NOOP("Soft airbrush"), values(90, 100, 8, 8, 0, 0)},
        {QT_TR_NOOP("Fine spray"),    values(36, 100, 14, 8, 10, 0, true)},
    });
    add("eraser", true, {
        {QT_TR_NOOP("Hard eraser"),   values(24, 100, 100, 10, 100, 0)},
        {QT_TR_NOOP("Soft eraser"),   values(60, 100, 35, 10, 20, 0)},
    });
    connect(m_brush, &BrushSettings::changed, this, [this] {
        if (m_applying || m_group < 0) return;
        capture();
        emit subToolsChanged();
    });
}

int SubToolLibrary::groupIndex(const QString& tool) const
{
    for (std::size_t i = 0; i < m_groups.size(); ++i)
        if (m_groups[i].tool == tool) return static_cast<int>(i);
    return -1;
}

bool SubToolLibrary::paints(const QString& tool) const { return groupIndex(tool) >= 0; }

QStringList SubToolLibrary::paintTools() const
{
    QStringList list;
    for (const auto& group : m_groups) list << group.tool;
    return list;
}

void SubToolLibrary::setTool(const QString& tool)
{
    if (tool == m_tool) return;
    m_tool = tool;
    m_group = groupIndex(tool);
    // A tool that does not paint leaves the brush settings as they are.
    if (m_group >= 0) apply();
    emit toolChanged();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::select(int index)
{
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    if (index < 0 || index >= static_cast<int>(group.entries.size()) || index == group.current) return;
    group.current = index;
    apply();
    emit subToolsChanged();
    emit currentChanged();
}

void SubToolLibrary::resetCurrent()
{
    if (m_group < 0) return;
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    auto& entry = group.entries[static_cast<std::size_t>(group.current)];
    if (entry.current == entry.defaults) return;
    entry.current = entry.defaults;
    apply();
    emit subToolsChanged();
}

void SubToolLibrary::apply()
{
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    const V v = group.entries[static_cast<std::size_t>(group.current)].current;
    m_applying = true;
    m_brush->setSize(v.size);
    m_brush->setOpacity(v.opacity);
    m_brush->setFlow(v.flow);
    m_brush->setSpacing(v.spacing);
    m_brush->setHardness(v.hardness);
    m_brush->setStabilization(v.stabilization);
    m_brush->setTextured(v.textured);
    m_brush->setErase(group.erase);
    m_applying = false;
}

void SubToolLibrary::capture()
{
    auto& group = m_groups[static_cast<std::size_t>(m_group)];
    auto& v = group.entries[static_cast<std::size_t>(group.current)].current;
    v.size = m_brush->size();
    v.opacity = m_brush->opacity();
    v.flow = m_brush->flow();
    v.spacing = m_brush->spacing();
    v.hardness = m_brush->hardness();
    v.stabilization = m_brush->stabilization();
    v.textured = m_brush->textured();
}

QVariantList SubToolLibrary::subTools() const
{
    QVariantList list;
    if (m_group < 0) return list;
    for (const auto& entry : m_groups[static_cast<std::size_t>(m_group)].entries) {
        QVariantMap item;
        item.insert(QStringLiteral("name"), entry.name);
        item.insert(QStringLiteral("size"), entry.current.size);
        item.insert(QStringLiteral("textured"), entry.current.textured);
        item.insert(QStringLiteral("hardness"), entry.current.hardness);
        item.insert(QStringLiteral("modified"), !(entry.current == entry.defaults));
        list.push_back(item);
    }
    return list;
}

int SubToolLibrary::currentIndex() const
{
    return m_group < 0 ? -1 : m_groups[static_cast<std::size_t>(m_group)].current;
}

QString SubToolLibrary::currentName() const
{
    if (m_group < 0) return {};
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    return group.entries[static_cast<std::size_t>(group.current)].name;
}

bool SubToolLibrary::isModified() const
{
    if (m_group < 0) return false;
    const auto& group = m_groups[static_cast<std::size_t>(m_group)];
    const auto& entry = group.entries[static_cast<std::size_t>(group.current)];
    return !(entry.current == entry.defaults);
}
} // namespace komapaint
