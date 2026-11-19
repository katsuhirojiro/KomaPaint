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

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <cstddef>
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

    // ---- document ---------------------------------------------------------
    const brush::BrushDocument& document() const { return m_document; }
    // Replaces the whole document as one undoable step. Used by "New",
    // "Duplicate" and by opening a .kb from the library.
    void setDocument(brush::BrushDocument document);
    bool load(const QString& path);
    bool save(const QString& path);

    QString name() const { return QString::fromStdString(m_document.name); }
    void setName(const QString& value);
    QString engine() const;
    void setEngine(const QString& value);
    QString description() const { return QString::fromStdString(m_document.description); }
    void setDescription(const QString& value);

    // ---- components -------------------------------------------------------
    QVariantList components() const;
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
    // Every mutating call funnels through here, so undo and the change signals
    // cannot be forgotten by one of them.
    template <typename Fn>
    void edit(const char* label, Fn&& change) {
        // Inside a step the pre-step snapshot is the one that matters, so a
        // gesture that makes ten edits is one undo.
        if (!m_inStep) pushUndo(label);
        change();
        m_modified = true;
        emit documentChanged();
        emit componentsChanged();
        emit exposureChanged();
        emit historyChanged();
    }
    template <typename T>
    bool changeSetting(const char* label, T& field, T value) {
        if (field != value) edit(label, [&] { field = std::move(value); });
        clearError();
        return true;
    }
    brush::BrushComponent* editableComponent(const QString& id);
    void pushUndo(const char* label);
    bool fail(const QString& message);
    void clearError();
    void syncSelection();

    brush::BrushDocument m_document;
    QString m_error;
    int m_selectedIndex = -1;
    bool m_modified = false;

    struct Step {
        brush::BrushDocument document;
        int selectedIndex = -1;
        QString label;
        std::size_t bytes = 0;
    };
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    std::size_t m_historyBytes = 0;
    // Snapshots hold whole documents, images included, so the history is
    // bounded by memory rather than by an arbitrary step count. Oldest first.
    static constexpr std::size_t HistoryBudget = 96u * 1024u * 1024u;
    bool m_inStep = false;
};

} // namespace komapaint
