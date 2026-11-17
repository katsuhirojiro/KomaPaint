#include "ShortcutManager.h"

#include "../core/Settings.h"

#include <QKeySequence>
#include <QSet>

namespace komapaint {

namespace {

const std::vector<ShortcutManager::ActionEntry>& defaultActionTable()
{
    static const std::vector<ShortcutManager::ActionEntry> table = {
        // File
        { QStringLiteral("file.new"),        QStringLiteral("New Project"),            QStringLiteral("File"),   QStringLiteral("Ctrl+N"),       QStringLiteral("Ctrl+N") },
        { QStringLiteral("file.open"),       QStringLiteral("Open Illustration"),      QStringLiteral("File"),   QStringLiteral("Ctrl+O"),       QStringLiteral("Ctrl+O") },
        { QStringLiteral("file.save"),       QStringLiteral("Save"),                   QStringLiteral("File"),   QStringLiteral("Ctrl+S"),       QStringLiteral("Ctrl+S") },
        { QStringLiteral("file.saveAs"),     QStringLiteral("Save As"),                QStringLiteral("File"),   QStringLiteral("Ctrl+Shift+S"), QStringLiteral("Ctrl+Shift+S") },
        // Edit
        { QStringLiteral("edit.undo"),       QStringLiteral("Undo"),                   QStringLiteral("Edit"),   QStringLiteral("Ctrl+Z"),       QStringLiteral("Ctrl+Z") },
        { QStringLiteral("edit.redo"),       QStringLiteral("Redo"),                   QStringLiteral("Edit"),   QStringLiteral("Ctrl+Y"),       QStringLiteral("Ctrl+Y") },
        { QStringLiteral("edit.redoAlt"),    QStringLiteral("Redo (Alternate)"),       QStringLiteral("Edit"),   QStringLiteral("Ctrl+Shift+Z"), QStringLiteral("Ctrl+Shift+Z") },
        // Select
        { QStringLiteral("select.all"),      QStringLiteral("Select All"),             QStringLiteral("Select"), QStringLiteral("Ctrl+A"),       QStringLiteral("Ctrl+A") },
        { QStringLiteral("select.deselect"), QStringLiteral("Deselect"),               QStringLiteral("Select"), QStringLiteral("Ctrl+D"),       QStringLiteral("Ctrl+D") },
        { QStringLiteral("select.invert"),   QStringLiteral("Invert Selection"),       QStringLiteral("Select"), QStringLiteral("Ctrl+Shift+I"), QStringLiteral("Ctrl+Shift+I") },
        // View
        { QStringLiteral("view.zoomIn"),       QStringLiteral("Zoom In"),              QStringLiteral("View"),   QStringLiteral("Ctrl++"),       QStringLiteral("Ctrl++") },
        { QStringLiteral("view.zoomOut"),      QStringLiteral("Zoom Out"),             QStringLiteral("View"),   QStringLiteral("Ctrl+-"),       QStringLiteral("Ctrl+-") },
        { QStringLiteral("view.actualPixels"), QStringLiteral("Actual Pixels"),        QStringLiteral("View"),   QStringLiteral("Ctrl+1"),       QStringLiteral("Ctrl+1") },
        { QStringLiteral("view.fitToWindow"),  QStringLiteral("Fit to Window"),        QStringLiteral("View"),   QStringLiteral("Ctrl+0"),       QStringLiteral("Ctrl+0") },
        { QStringLiteral("view.resetRotation"),QStringLiteral("Reset Canvas Rotation"),QStringLiteral("View"),   QStringLiteral("Ctrl+Shift+0"), QStringLiteral("Ctrl+Shift+0") },
        { QStringLiteral("view.togglePanels"), QStringLiteral("Toggle Side Panels"),   QStringLiteral("View"),   QStringLiteral("Tab"),          QStringLiteral("Tab") },
        // Tools
        { QStringLiteral("tool.zoom"),       QStringLiteral("Zoom Tool"),              QStringLiteral("Tools"),  QStringLiteral("Z"),            QStringLiteral("Z") },
        { QStringLiteral("tool.hand"),       QStringLiteral("Hand Tool"),              QStringLiteral("Tools"),  QStringLiteral("H"),            QStringLiteral("H") },
        { QStringLiteral("tool.eyedropper"), QStringLiteral("Eyedropper"),             QStringLiteral("Tools"),  QStringLiteral("I"),            QStringLiteral("I") },
        { QStringLiteral("tool.move"),       QStringLiteral("Move Tool"),              QStringLiteral("Tools"),  QStringLiteral("V"),            QStringLiteral("V") },
        { QStringLiteral("tool.transform"),  QStringLiteral("Transform Tool"),         QStringLiteral("Tools"),  QStringLiteral("K"),            QStringLiteral("K") },
        { QStringLiteral("tool.selection"),  QStringLiteral("Selection Tool"),         QStringLiteral("Tools"),  QStringLiteral("M"),            QStringLiteral("M") },
        { QStringLiteral("tool.lasso"),      QStringLiteral("Lasso Tool"),             QStringLiteral("Tools"),  QStringLiteral("L"),            QStringLiteral("L") },
        { QStringLiteral("tool.wand"),       QStringLiteral("Auto Select (Wand)"),     QStringLiteral("Tools"),  QStringLiteral("W"),            QStringLiteral("W") },
        { QStringLiteral("tool.pen"),        QStringLiteral("Pen Tool"),               QStringLiteral("Tools"),  QStringLiteral("P"),            QStringLiteral("P") },
        { QStringLiteral("tool.pencil"),     QStringLiteral("Pencil Tool"),            QStringLiteral("Tools"),  QStringLiteral("N"),            QStringLiteral("N") },
        { QStringLiteral("tool.brush"),      QStringLiteral("Brush Tool"),             QStringLiteral("Tools"),  QStringLiteral("B"),            QStringLiteral("B") },
        { QStringLiteral("tool.airbrush"),   QStringLiteral("Airbrush Tool"),          QStringLiteral("Tools"),  QStringLiteral("A"),            QStringLiteral("A") },
        { QStringLiteral("tool.eraser"),     QStringLiteral("Eraser Tool"),            QStringLiteral("Tools"),  QStringLiteral("E"),            QStringLiteral("E") },
        { QStringLiteral("tool.smudge"),     QStringLiteral("Blend / Smudge Tool"),    QStringLiteral("Tools"),  QStringLiteral("S"),            QStringLiteral("S") },
        { QStringLiteral("tool.fill"),       QStringLiteral("Fill Tool"),              QStringLiteral("Tools"),  QStringLiteral("G"),            QStringLiteral("G") },
        { QStringLiteral("tool.gradient"),   QStringLiteral("Gradient Tool"),          QStringLiteral("Tools"),  QStringLiteral("Shift+G"),      QStringLiteral("Shift+G") },
        { QStringLiteral("tool.line"),       QStringLiteral("Line Tool"),              QStringLiteral("Tools"),  QStringLiteral("Shift+L"),      QStringLiteral("Shift+L") },
        { QStringLiteral("tool.curveline"),  QStringLiteral("Curve Tool"),             QStringLiteral("Tools"),  QStringLiteral("Shift+C"),      QStringLiteral("Shift+C") },
        { QStringLiteral("tool.rectangle"),  QStringLiteral("Rectangle Tool"),         QStringLiteral("Tools"),  QStringLiteral("U"),            QStringLiteral("U") },
        { QStringLiteral("tool.circle"),     QStringLiteral("Ellipse Tool"),           QStringLiteral("Tools"),  QStringLiteral("O"),            QStringLiteral("O") },
        { QStringLiteral("tool.text"),       QStringLiteral("Text Tool"),              QStringLiteral("Tools"),  QStringLiteral("T"),            QStringLiteral("T") },
        // Brush & Colour
        { QStringLiteral("brush.sizeDown"),  QStringLiteral("Decrease Brush Size"),    QStringLiteral("Brush"),  QStringLiteral("["),            QStringLiteral("[") },
        { QStringLiteral("brush.sizeUp"),    QStringLiteral("Increase Brush Size"),    QStringLiteral("Brush"),  QStringLiteral("]"),            QStringLiteral("]") },
        { QStringLiteral("color.swap"),      QStringLiteral("Swap Foreground / Background"), QStringLiteral("Brush"), QStringLiteral("X"),         QStringLiteral("X") },
        { QStringLiteral("color.reset"),     QStringLiteral("Reset Default Colours"),  QStringLiteral("Brush"),  QStringLiteral("D"),            QStringLiteral("D") },
        // Layers
        { QStringLiteral("layer.newRaster"), QStringLiteral("New Raster Layer"),       QStringLiteral("Layers"), QStringLiteral("Ctrl+Shift+N"), QStringLiteral("Ctrl+Shift+N") },
    };
    return table;
}

bool isBareModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_unknown:
    case 0:
        return true;
    default:
        return false;
    }
}

} // namespace

ShortcutManager::ShortcutManager(QObject* parent)
    : QObject(parent), m_actions(defaultActionTable())
{
}

void ShortcutManager::resetToDefaultsInternal()
{
    for (auto& entry : m_actions) {
        entry.sequence = entry.defaultSequence;
    }
}

void ShortcutManager::bindSettings(Settings* settings)
{
    m_settings = settings;
    resetToDefaultsInternal();
    if (m_settings) {
        for (auto& entry : m_actions) {
            const QString key = QStringLiteral("shortcuts/") + entry.id;
            const QVariant stored = m_settings->value(key);
            if (stored.isValid()) {
                entry.sequence = normalizeSequence(stored.toString());
            }
        }
    }
    emit shortcutsChanged();
}

bool ShortcutManager::saveToSettings() const
{
    if (!m_settings) return true;
    for (const auto& entry : m_actions) {
        m_settings->setValue(QStringLiteral("shortcuts/") + entry.id, entry.sequence);
    }
    return m_settings->sync();
}

QVariantList ShortcutManager::actions() const
{
    QVariantList out;
    out.reserve(static_cast<int>(m_actions.size()));
    for (const auto& entry : m_actions) {
        out.push_back(QVariantMap{
            { QStringLiteral("id"), entry.id },
            { QStringLiteral("label"), entry.label },
            { QStringLiteral("category"), entry.category },
            { QStringLiteral("defaultSequence"), entry.defaultSequence },
            { QStringLiteral("sequence"), entry.sequence }
        });
    }
    return out;
}

QVariantMap ShortcutManager::bindingsMap() const
{
    QVariantMap map;
    for (const auto& entry : m_actions) {
        map.insert(entry.id, entry.sequence);
    }
    return map;
}

QVariantMap ShortcutManager::defaultBindingsMap() const
{
    QVariantMap map;
    for (const auto& entry : m_actions) {
        map.insert(entry.id, entry.defaultSequence);
    }
    return map;
}

QString ShortcutManager::keyFor(const QString& actionId) const
{
    for (const auto& entry : m_actions) {
        if (entry.id == actionId) return entry.sequence;
    }
    return QString();
}

QString ShortcutManager::defaultKeyFor(const QString& actionId) const
{
    for (const auto& entry : m_actions) {
        if (entry.id == actionId) return entry.defaultSequence;
    }
    return QString();
}

QString ShortcutManager::labelFor(const QString& actionId) const
{
    for (const auto& entry : m_actions) {
        if (entry.id == actionId) return entry.label;
    }
    return actionId;
}

QString ShortcutManager::normalizeSequence(const QString& sequence) const
{
    const QString trimmed = sequence.trimmed();
    if (trimmed.isEmpty()) return QString();
    const QKeySequence ks = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
    if (ks.isEmpty()) return QString();
    return ks.toString(QKeySequence::PortableText);
}

QString ShortcutManager::formatKeyEvent(int key, int modifiers) const
{
    if (isBareModifierKey(key)) return QString();
    const int filteredMods = modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const QKeySequence ks(filteredMods | key);
    if (ks.isEmpty()) return QString();
    return ks.toString(QKeySequence::PortableText);
}

QString ShortcutManager::findConflict(const QString& actionId,
                                      const QString& sequence,
                                      const QVariantMap& draftMap) const
{
    const QString norm = normalizeSequence(sequence);
    if (norm.isEmpty()) return QString();
    for (const auto& entry : m_actions) {
        if (entry.id == actionId) continue;
        const QString otherSeq = draftMap.contains(entry.id)
            ? normalizeSequence(draftMap.value(entry.id).toString())
            : entry.sequence;
        if (!otherSeq.isEmpty() && otherSeq == norm) {
            return entry.label;
        }
    }
    return QString();
}

bool ShortcutManager::applyBindings(const QVariantMap& bindings)
{
    std::vector<ActionEntry> next = m_actions;
    QSet<QString> seen;
    for (auto& entry : next) {
        if (bindings.contains(entry.id)) {
            const QString raw = bindings.value(entry.id).toString().trimmed();
            const QString norm = raw.isEmpty() ? QString() : normalizeSequence(raw);
            if (!raw.isEmpty() && norm.isEmpty()) return false;
            entry.sequence = norm;
        }
        if (!entry.sequence.isEmpty()) {
            if (seen.contains(entry.sequence)) return false;
            seen.insert(entry.sequence);
        }
    }
    m_actions = std::move(next);
    const bool saved = saveToSettings();
    emit shortcutsChanged();
    return saved;
}

void ShortcutManager::restoreDefaults()
{
    resetToDefaultsInternal();
    saveToSettings();
    emit shortcutsChanged();
}

} // namespace komapaint
