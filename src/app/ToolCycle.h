#pragma once

#include <QString>
#include <QStringList>

namespace komapaint::toolcycle {

// One keyboard shortcut can stand for a family of tools, the way Clip Studio
// Paint cycles Brush -> Airbrush on repeated presses of B. The first press from
// anywhere outside the family selects the family's primary tool; further presses
// walk through the rest and then wrap around.
//
// The table is intentionally small and data-only so the behaviour can be tested
// without a window, a document or an event loop.
inline QStringList groupForAction(const QString& actionId)
{
    // Paint-brush family (B).
    if (actionId == QStringLiteral("tool.brush"))
        return { QStringLiteral("brush"), QStringLiteral("airbrush") };
    // Ink family (P): pen and pencil share one key, as in Clip Studio Paint.
    if (actionId == QStringLiteral("tool.pen"))
        return { QStringLiteral("pen"), QStringLiteral("pencil") };
    // Figure family (U).
    if (actionId == QStringLiteral("tool.rectangle"))
        return { QStringLiteral("rectangle"), QStringLiteral("circle"),
                 QStringLiteral("line"), QStringLiteral("curveline") };
    // Fill is a family of one palette tool whose members are sub-tools, so the
    // single entry tells the caller to walk presets instead of switching tools.
    if (actionId == QStringLiteral("tool.fill"))
        return { QStringLiteral("fill") };
    return {};
}

// Executable sub-tools that belong to the family of an action. Usually these are
// the palette tools themselves; Fill is the exception, because "Smart Fill" is a
// fill sub-tool rather than a separate palette tool. Pressing G while holding the
// Fill tool therefore walks the configured fill group.
inline QStringList executablesForAction(const QString& actionId)
{
    if (actionId == QStringLiteral("tool.fill"))
        return { QStringLiteral("fill"), QStringLiteral("smart_fill") };
    return groupForAction(actionId);
}

// Returns the tool that the next press should select. When `current` is not a
// member of the family the first entry wins, so switching from the eraser to B
// always lands on the brush rather than on whichever sibling was last used.
inline QString nextTool(const QStringList& group, const QString& current)
{
    if (group.isEmpty()) return {};
    const int index = group.indexOf(current);
    if (index < 0) return group.front();
    return group.at((index + 1) % group.size());
}

} // namespace komapaint::toolcycle
