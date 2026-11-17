#pragma once
#include "BrushLibraryStore.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <vector>

namespace komapaint {
class BrushSettings;
class ToolSettings;

// Universal Sub-Tool & Collection manager for the entire KomaPaint tool ecosystem.
//
// Every compatible tool family (Pen, Pencil, Brush [Digital & Realistic],
// Airbrush, Eraser, Fill, Lasso, Magic Wand, Selection, Move, Transform,
// Gradient, Figure shapes, Text, Animation & future tools) manages unlimited,
// persistent, user-created collection tabs and independent presets.
//
// Collection organization is decoupled from the underlying executable tool:
// presets preserve their `executableTool`, realistic/material settings,
// pigment mixtures, and `toolParams` regardless of which tab or compatible
// collection they are moved into.
class SubToolLibrary : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString tool READ tool NOTIFY toolChanged)
    Q_PROPERTY(bool paintTool READ isPaintTool NOTIFY toolChanged)
    Q_PROPERTY(bool brushTool READ isBrushTool NOTIFY toolChanged)
    Q_PROPERTY(bool hasCollections READ hasCollections NOTIFY toolChanged)
    Q_PROPERTY(QString executableTool READ executableTool NOTIFY currentChanged)
    Q_PROPERTY(QString activeEngine READ activeEngine NOTIFY tabsChanged)
    Q_PROPERTY(QVariantList subTools READ subTools NOTIFY subToolsChanged)
    Q_PROPERTY(QVariantList engineTabs READ engineTabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTabIndex READ currentTabIndex NOTIFY tabsChanged)
    Q_PROPERTY(QString currentTabId READ currentTabId NOTIFY tabsChanged)
    Q_PROPERTY(QString currentTabName READ currentTabName NOTIFY tabsChanged)
    Q_PROPERTY(QString defaultTabId READ defaultTabId NOTIFY tabsChanged)
    Q_PROPERTY(bool canUndoTabDelete READ canUndoTabDelete NOTIFY tabsChanged)
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

    void setToolSettings(ToolSettings* toolSettings);
    bool initializePersistence(const QString& filePath);
    QString persistencePath() const { return m_persistencePath; }

    QString tool() const { return m_tool; }
    // True for brush-stroke painting tools (pen, pencil, brush, airbrush, eraser).
    bool isPaintTool() const;
    bool isBrushTool() const { return m_tool == QStringLiteral("brush"); }
    // True for ANY tool family managed by the universal Sub-Tool collection system.
    bool hasCollections() const;
    QString executableTool() const;
    QString activeEngine() const;
    QVariantList subTools() const;
    QVariantList engineTabs() const;
    int currentTabIndex() const;
    QString currentTabId() const;
    QString currentTabName() const;
    QString defaultTabId() const;
    bool canUndoTabDelete() const { return m_hasDeletedTabBackup; }
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

    // `paints(tool)` stays true strictly for stroke-painting tools (pen, pencil,
    // brush, airbrush, eraser). `usesSubTools(tool)` is true for ALL tools
    // integrated with the universal Sub-Tool collection architecture.
    Q_INVOKABLE bool paints(const QString& tool) const;
    Q_INVOKABLE bool usesSubTools(const QString& tool) const;
    Q_INVOKABLE QStringList paintTools() const;
    Q_INVOKABLE QStringList supportedTools() const;
    Q_INVOKABLE QStringList compatibleToolsFor(const QString& toolOrExecutable) const;

    Q_INVOKABLE void select(int index);
    Q_INVOKABLE void selectById(const QString& brushId);
    Q_INVOKABLE void resetCurrent();
    Q_INVOKABLE void resetBrush(const QString& brushId);

    // Persistent tab CRUD within the active tool/engine collection.
    Q_INVOKABLE void selectTab(int index);
    Q_INVOKABLE void selectTabById(const QString& tabId);
    Q_INVOKABLE QString createTab(const QString& name);
    Q_INVOKABLE bool renameTab(const QString& tabId, const QString& newName);
    Q_INVOKABLE bool moveTab(int fromIndex, int toIndex);
    Q_INVOKABLE bool moveTabById(const QString& tabId, int delta);
    Q_INVOKABLE bool setDefaultTab(const QString& tabId);
    Q_INVOKABLE bool deleteTab(const QString& tabId);
    Q_INVOKABLE bool undoDeleteTab();

    // Persistent sub-tool preset CRUD & cross-tab / cross-tool organization.
    Q_INVOKABLE QString createBrushFromCurrent(const QString& name = QString());
    Q_INVOKABLE QString duplicateBrush(int index);
    Q_INVOKABLE QString duplicateBrushById(const QString& brushId);
    Q_INVOKABLE bool renameBrush(const QString& brushId, const QString& newName);
    Q_INVOKABLE bool deleteBrush(const QString& brushId);
    Q_INVOKABLE bool moveBrushToTab(const QString& brushId, const QString& targetTabId);
    Q_INVOKABLE QString copyBrushToTab(const QString& brushId, const QString& targetTabId);
    Q_INVOKABLE bool moveSubToolTo(const QString& brushId, const QString& targetTool,
                                   const QString& targetTabId);
    Q_INVOKABLE QString copySubToolTo(const QString& brushId, const QString& targetTool,
                                      const QString& targetTabId);
    Q_INVOKABLE QVariantList tabsForTool(const QString& targetTool) const;
    Q_INVOKABLE bool reorderBrush(int fromIndex, int toIndex);

    // Allows future tools to register an initial default collection on demand.
    Q_INVOKABLE bool ensureToolCollection(const QString& toolFamily);

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
    struct DeletedTabBackup {
        QString collectionKey; // "brush:digital", "brush:realistic", or tool id
        int index = 0;
        BrushTabData tab;
        bool wasDefault = false;
        bool wasCurrent = false;
    };

    static bool isStrokePaintToolName(const QString& t);
    static bool isKnownViewOnlyTool(const QString& t);
    QString currentCollectionKey() const;
    EngineCollectionData* collectionForTool(const QString& toolFamily, const QString& engineOverride = QString());
    const EngineCollectionData* collectionForTool(const QString& toolFamily, const QString& engineOverride = QString()) const;
    EngineCollectionData* activeCollectionPtr();
    const EngineCollectionData* activeCollectionPtr() const;
    int findTabIndex(const EngineCollectionData& col, const QString& tabId) const;
    BrushTabData* currentTabPtr();
    const BrushTabData* currentTabPtr() const;
    int currentBrushIndexInTab(const BrushTabData* tab) const;
    void apply();
    void capture();
    Values captureFromSettings(bool traditionalMedia, const QString& execToolHint, bool eraseHint) const;
    void applyValuesToSettings(const Values& values, bool fallbackErase);
    bool persist();
    void setLastError(const QString& message);
    QString uniqueBrushName(const BrushTabData& tab, const QString& baseName) const;

    BrushSettings* m_brush = nullptr;
    ToolSettings* m_toolSettings = nullptr;
    BrushLibraryDocument m_bundledCollections;
    BrushLibraryDocument m_collections;
    QString m_persistencePath;
    QString m_tool;
    QString m_searchText;
    QString m_categoryFilter;
    QString m_loadError;
    QString m_lastError;
    bool m_applying = false;
    bool m_hasDeletedTabBackup = false;
    DeletedTabBackup m_deletedTabBackup;
};
} // namespace komapaint
