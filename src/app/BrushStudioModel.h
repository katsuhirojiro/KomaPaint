#pragma once
// The Qt face of a brush document, for Brush Studio.
//
// The document model, the evaluator and the format are Qt-free on purpose: they
// are data and decisions, and they are tested without a GUI. This class is the
// thin layer the window talks to -- it exposes the document as QVariantLists of
// plain maps, keeps a selection, and makes every edit an undoable step.
//
// It owns no rendering and no layout: the preview paints through the production
// engines, and the window arranges itself. Anything that looks like a brush
// decision belongs in the model, not here.
#include "../brush/BrushFormat.h"
#include "../brush/BrushModel.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <list>
#include <type_traits>
#include <utility>
#include <vector>

namespace komapaint {

class BrushStudioModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY documentChanged)
    Q_PROPERTY(QString engine READ engine WRITE setEngine NOTIFY documentChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY documentChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList components READ components NOTIFY componentsChanged)
    Q_PROPERTY(QVariantMap selectedComponent READ selectedComponent NOTIFY componentsChanged)
    Q_PROPERTY(int componentCount READ componentCount NOTIFY componentsChanged)
    Q_PROPERTY(int hiddenComponentCount READ hiddenComponentCount NOTIFY componentsChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY componentsChanged)
    Q_PROPERTY(QVariantList parameters READ parameters NOTIFY componentsChanged)
    Q_PROPERTY(QVariantList exposedProperties READ exposedProperties NOTIFY exposureChanged)
    Q_PROPERTY(QVariantList componentKinds READ componentKinds CONSTANT)
    Q_PROPERTY(QVariantList selectionModes READ selectionModes CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool modified READ isModified NOTIFY documentChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString validationSummary READ validationSummary NOTIFY documentChanged)
public:
    explicit BrushStudioModel(QObject* parent = nullptr);
    // Per-model admission check: normal construction observes system memory.
    // The alternate check lets tests simulate pressure without allocating it.
    using MemoryCheck = bool (*)(void*, std::size_t) noexcept;
    BrushStudioModel(QObject* parent, MemoryCheck check, void* context);

    // ---- document ---------------------------------------------------------
    const brush::BrushDocument& document() const { return m_document; }
    // Replaces the whole document as one undoable step. Used by "New",
    // "Duplicate" and by opening a .kb from the library.
    bool setDocument(const brush::BrushDocument& document);
    bool setDocument(brush::BrushDocument&& document);
    bool load(const QString& path);
    bool save(const QString& path);
    Q_INVOKABLE bool newDraft();
    Q_INVOKABLE bool duplicateDraft();
    Q_INVOKABLE bool openDraft(const QUrl& url);
    Q_INVOKABLE bool saveDraft(const QUrl& url);
    Q_INVOKABLE bool exportDraft(const QUrl& url); // writes a copy without changing the save point

    QString name() const { return QString::fromStdString(m_document.name); }
    void setName(const QString& value);
    QString engine() const;
    void setEngine(const QString& value);
    QString description() const { return QString::fromStdString(m_document.description); }
    void setDescription(const QString& value);

    // ---- components -------------------------------------------------------
    QVariantList components() const;
    QVariantMap selectedComponent() const;
    const brush::BrushResource* selectedResource() const;
    int componentCount() const { return static_cast<int>(m_document.components.size()); }
    int hiddenComponentCount() const;
    Q_INVOKABLE bool selectComponentInMode(const QString& mode);
    QVariantList groups() const;
    QVariantList componentKinds() const;
    QVariantList selectionModes() const;
    QString selectedId() const;
    int selectedIndex() const;
    void setSelectedIndex(int index);

    Q_INVOKABLE QString addComponent(int kind);
    Q_INVOKABLE QString duplicateComponent(const QString& id);
    Q_INVOKABLE bool removeComponent(const QString& id);
    Q_INVOKABLE bool moveComponent(const QString& id, int newIndex);
    Q_INVOKABLE bool setComponentName(const QString& id, const QString& value);
    Q_INVOKABLE bool setComponentVisible(const QString& id, bool visible);
    Q_INVOKABLE bool setComponentOpacity(const QString& id, double opacity);
    Q_INVOKABLE bool setComponentBlend(const QString& id, int blend);
    Q_INVOKABLE bool setComponentErases(const QString& id, bool erases);
    Q_INVOKABLE bool setComponentTransform(const QString& id, const QString& key, double value);
    Q_INVOKABLE bool setComponentWeight(const QString& id, double weight);
    Q_INVOKABLE QString createGroup(const QString& name);
    Q_INVOKABLE bool assignToGroup(const QString& componentId, const QString& groupId);
    Q_INVOKABLE bool setGroupMode(const QString& groupId, int mode);
    Q_INVOKABLE bool setGroupMembersPerStamp(const QString& groupId, int count);
    Q_INVOKABLE bool removeGroup(const QString& groupId);

    // ---- settings ---------------------------------------------------------
    // Every registry parameter with its group, range, unit and current value.
    // Component-scoped settings read and write the selected component.
    QVariantList parameters() const;
    Q_INVOKABLE double parameterValue(const QString& key) const;
    Q_INVOKABLE bool setParameterValue(const QString& key, double value);
    Q_INVOKABLE QVariantMap parameterInfo(const QString& key) const;

    // ---- Sub-Tool exposure (Part 10) --------------------------------------
    QVariantList exposedProperties() const;
    Q_INVOKABLE bool isExposed(const QString& key) const;
    Q_INVOKABLE bool expose(const QString& key);
    Q_INVOKABLE bool unexpose(const QString& key);
    Q_INVOKABLE bool setExposure(const QString& key, const QVariantMap& fields);
    Q_INVOKABLE bool moveExposure(const QString& key, int delta);
    // Puts the exposure list back to what the brush documents by default, which
    // is the only sane meaning of "reset" for a list an artist curated.
    Q_INVOKABLE void resetExposure();

    // ---- history ----------------------------------------------------------
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Opens a step: every edit between beginStep() and commitStep() lands in one
    // undo entry. Edits outside a step are their own step.
    void beginStep(const QString& label);
    void commitStep();

    struct StorageUse {
        std::size_t metadataBytes; // active document + undo/redo node and metadata capacities
        std::size_t pixelBytes;    // each retained pixel buffer counted only once
        std::size_t historyStates;
    };
    // Capacity accounting, not process RSS: allocator bookkeeping and Qt/UI
    // caches are excluded. Explicit diagnostic; editing doesn't scan history.
    StorageUse retainedStorage() const;

    bool isModified() const { return m_modified; }
    void setModified(bool modified);
    QString error() const { return m_error; }
    QString validationSummary() const;

signals:
    void documentChanged();
    void componentsChanged();
    void selectionChanged();
    void exposureChanged();
    void historyChanged();
    void errorChanged();

private:
    enum class EditMode { Change, Replace, LoadSaved };
    // A non-owning call wrapper: don't allocate to type-erase a lambda before
    // the transaction's admission and exception guard.
    template <typename Fn>
    bool edit(const char* label, Fn&& change, EditMode mode = EditMode::Change,
              const brush::BrushDocument* incomingCopy = nullptr) {
        using Change = std::remove_reference_t<Fn>;
        return editTransaction(label, &change, [](void* arg) {
            auto& fn = *static_cast<Change*>(arg);
            if constexpr (std::is_same_v<std::invoke_result_t<Change&>, bool>) return fn();
            else { fn(); return true; }
        }, mode, incomingCopy);
    }
    template <typename T>
    bool changeSetting(const char* label, T& field, T value) {
        if (field != value) return edit(label, [&] { field = std::move(value); });
        clearError();
        return true;
    }
    bool editTransaction(const char* label, void* context, bool (*apply)(void*), EditMode mode,
                         const brush::BrushDocument* incomingCopy);
    bool replaceDocument(brush::BrushDocument&& document, EditMode mode, const char* label = "Open brush");
    bool writeBrush(const QString& path);
    QVariantMap componentRow(int index) const;
    const brush::BrushComponent* selectedComponentData() const;
    brush::BrushComponent* editableComponent(const QString& id);
    bool fail(const QString& message);
    void clearError();
    bool historyMemoryError();
    void markSaved() noexcept;

    brush::BrushDocument m_document;
    QString m_error;
    int m_selectedIndex = -1;
    bool m_modified = false;

    struct Step {
        Step(const brush::BrushDocument& doc, int selection, bool dirty)
            : document(doc), selectedIndex(selection), modified(dirty) {}
        brush::BrushDocument document;
        int selectedIndex;
        bool modified;
    };
    // Stored metadata is cheap to copy; pixel blocks are shared. Lists let
    // replay transfer existing nodes without allocating under pressure.
    using History = std::list<Step>;
    static constexpr std::size_t StepBytes = sizeof(Step) + 2 * sizeof(void*);
    void swapCurrent(Step& step) noexcept;
    void replay(History& source, History& destination);
    History m_undo;
    History m_redo;
    MemoryCheck m_memoryCheck;
    void* m_memoryContext;
    bool m_inStep = false;
    bool m_stepHasEdit = false;
    QString m_stepLabel;
};

} // namespace komapaint
