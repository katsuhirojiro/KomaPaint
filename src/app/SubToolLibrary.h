#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <vector>

namespace komapaint {
class BrushSettings;

// The sub tools (brush presets) behind each painting tool.
//
// Pen, pencil, brush, airbrush and eraser all paint with the same brush
// engine; what differs is the set of engine settings. Each tool offers a few
// named sub tools. Selecting one loads its settings into BrushSettings, and
// every later change made in the Tool Property panel is remembered for that
// sub tool, so switching tools and coming back restores what was set. A sub
// tool can be reset to the values it was shipped with.
//
// The library only ever stores settings the engine reads; it has no setting
// of its own that painting ignores. Changes are kept for the running session.
class SubToolLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    // True when the active tool paints with the brush engine.
    Q_PROPERTY(bool paintTool READ isPaintTool NOTIFY toolChanged)
    // Sub tools of the active tool: [{ name, size, textured, hardness, modified }].
    Q_PROPERTY(QVariantList subTools READ subTools NOTIFY subToolsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    // True when the current sub tool differs from the values it shipped with.
    Q_PROPERTY(bool modified READ isModified NOTIFY subToolsChanged)
public:
    struct Values {
        int size = 16, opacity = 100, flow = 100, spacing = 15, hardness = 100, stabilization = 0;
        bool textured = false;
        bool operator==(const Values&) const = default;
    };
    explicit SubToolLibrary(BrushSettings* brush, QObject* parent = nullptr);

    QString tool() const { return m_tool; }
    bool isPaintTool() const { return m_group >= 0; }
    QVariantList subTools() const;
    int currentIndex() const;
    QString currentName() const;
    bool isModified() const;

    Q_INVOKABLE bool paints(const QString& tool) const;
    Q_INVOKABLE QStringList paintTools() const;
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void resetCurrent();

public slots:
    // Called whenever the active tool changes (tool palette, tab switch, restore).
    void setTool(const QString& tool);

signals:
    void toolChanged();
    void subToolsChanged();
    void currentChanged();

private:
    struct Entry { QString name; Values defaults; Values current; };
    struct Group { QString tool; bool erase = false; std::vector<Entry> entries; int current = 0; };
    int groupIndex(const QString& tool) const;
    void apply();          // current entry -> BrushSettings
    void capture();        // BrushSettings -> current entry

    BrushSettings* m_brush;
    std::vector<Group> m_groups;
    QString m_tool;
    int m_group = -1;
    bool m_applying = false;
};
} // namespace komapaint
