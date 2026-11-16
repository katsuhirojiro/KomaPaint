#pragma once
#include "BrushLibraryStore.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <vector>

namespace komapaint {
class BrushSettings;

// The sub tools (brush presets) behind each painting tool.
//
// Pen, pencil, airbrush and eraser each manage a compact group of presets,
// while Painting Brushes ("brush") owns two completely separate, persistent
// engine collections: Digital and Realistic. Each engine collection contains
// persistent user-manageable tabs and independent brush presets whose settings
// and pigment mixtures never overwrite one another.
class SubToolLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(bool paintTool READ isPaintTool NOTIFY toolChanged)
    Q_PROPERTY(bool brushTool READ isBrushTool NOTIFY toolChanged)
    Q_PROPERTY(QString activeEngine READ activeEngine NOTIFY tabsChanged)
    Q_PROPERTY(QVariantList subTools READ subTools NOTIFY subToolsChanged)
    Q_PROPERTY(QVariantList engineTabs READ engineTabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTabIndex READ currentTabIndex NOTIFY tabsChanged)
    Q_PROPERTY(QString currentTabId READ currentTabId NOTIFY tabsChanged)
    Q_PROPERTY(QString currentTabName READ currentTabName NOTIFY tabsChanged)
    Q_PROPERTY(QString defaultTabId READ defaultTabId NOTIFY tabsChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY subToolsChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
    Q_PROPERTY(QString loadError READ loadError CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentBrushId READ currentBrushId NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(bool currentBrushBuiltIn READ currentBrushBuiltIn NOTIFY currentChanged)
    Q_PROPERTY(bool currentRequiresRealistic READ currentRequiresRealistic NOTIFY currentChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY subToolsChanged)
public:
    using Values = BrushPresetValues;
    explicit SubToolLibrary(BrushSettings* brush, QObject* parent = nullptr);

    bool initializePersistence(const QString& filePath);
    QString persistencePath() const { return m_persistencePath; }

    QString tool() const { return m_tool; }
    bool isPaintTool() const { return m_tool == QStringLiteral("brush") || m_group >= 0; }
    bool isBrushTool() const { return m_tool == QStringLiteral("brush"); }
    QString activeEngine() const;
    QVariantList subTools() const;
    QVariantList engineTabs() const;
    int currentTabIndex() const;
    QString currentTabId() const;
    QString currentTabName() const;
    QString defaultTabId() const;
    QStringList categories() const;
    QString searchText() const { return m_searchText; }
    QString categoryFilter() const { return m_categoryFilter; }
    QString loadError() const { return m_loadError; }
    QString lastError() const { return m_lastError; }
    int currentIndex() const;
    QString currentBrushId() const;
    QString currentName() const;
    bool currentBrushBuiltIn() const;
    bool currentRequiresRealistic() const;
    bool isModified() const;

    Q_INVOKABLE bool paints(const QString& tool) const;
    Q_INVOKABLE QStringList paintTools() const;
    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void selectById(const QString& brushId);
    Q_INVOKABLE void resetCurrent();
    Q_INVOKABLE void resetBrush(const QString& brushId);

    // Persistent tab CRUD within the active engine collection ("digital" or "realistic").
    Q_INVOKABLE void selectTab(int index);
    Q_INVOKABLE void selectTabById(const QString& tabId);
    Q_INVOKABLE QString createTab(const QString& name);
    Q_INVOKABLE bool renameTab(const QString& tabId, const QString& newName);
    Q_INVOKABLE bool moveTab(int fromIndex, int toIndex);
    Q_INVOKABLE bool moveTabById(const QString& tabId, int delta);
    Q_INVOKABLE bool setDefaultTab(const QString& tabId);
    Q_INVOKABLE bool deleteTab(const QString& tabId);

    // Persistent brush preset CRUD & cross-tab organization.
    Q_INVOKABLE QString createBrushFromCurrent(const QString& name = QString());
    Q_INVOKABLE QString duplicateBrush(int index);
    Q_INVOKABLE QString duplicateBrushById(const QString& brushId);
    Q_INVOKABLE bool renameBrush(const QString& brushId, const QString& newName);
    Q_INVOKABLE bool deleteBrush(const QString& brushId);
    Q_INVOKABLE bool moveBrushToTab(const QString& brushId, const QString& targetTabId);
    Q_INVOKABLE QString copyBrushToTab(const QString& brushId, const QString& targetTabId);
    Q_INVOKABLE bool reorderBrush(int fromIndex, int toIndex);

public slots:
    void setSearchText(const QString& text);
    void setCategoryFilter(const QString& category);
    void setTool(const QString& tool);

signals:
    void toolChanged();
    void tabsChanged();
    void subToolsChanged();
    void currentChanged();
    void filterChanged();
    void lastErrorChanged();

private:
    struct Entry { QString name; QString category; Values defaults; Values current; };
    struct Group { QString tool; bool erase = false; std::vector<Entry> entries; int current = 0; };

    int groupIndex(const QString& tool) const;
    EngineCollectionData& activeCollection();
    const EngineCollectionData& activeCollection() const;
    int findTabIndex(const EngineCollectionData& col, const QString& tabId) const;
    BrushTabData* currentTabPtr();
    const BrushTabData* currentTabPtr() const;
    int currentBrushIndexInTab(const BrushTabData* tab) const;
    void apply();
    void capture();
    Values captureFromBrush(bool traditionalMedia) const;
    void applyValuesToBrush(const Values& values, bool erase);
    bool persist();
    void setLastError(const QString& message);
    QString uniqueBrushName(const BrushTabData& tab, const QString& baseName) const;

    BrushSettings* m_brush;
    std::vector<Group> m_groups;
    BrushLibraryDocument m_bundledCollections;
    BrushLibraryDocument m_collections;
    QString m_persistencePath;
    QString m_tool;
    QString m_searchText;
    QString m_categoryFilter;
    QString m_loadError;
    QString m_lastError;
    int m_group = -1;
    bool m_applying = false;
};
} // namespace komapaint
