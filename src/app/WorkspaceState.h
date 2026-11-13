#pragma once

#include "../animation/TimelineState.h"
#include "BrushSettings.h"
#include "../canvas/CanvasDocument.h"
#include "../color/ColorPalette.h"
#include "../history/UndoStack.h"
#include "../layers/LayerModel.h"
#include "DocumentRecovery.h"
#include "ProjectCreationService.h"
#include "SessionStore.h"
#include "SubToolLibrary.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <vector>
#include <memory>
#include <QLockFile>
#include <QTimer>

namespace komapaint {

// Owns every open project and exposes the active one to the UI. One instance
// per running KomaPaint process; exposed to QML as "Workspace".
//
// Each tab owns its own CanvasDocument (layers, canvas, settings), its own
// UndoStack, its own file path and its own tool/zoom/viewport position. The
// properties below always describe the *active* tab; switching tabs swaps
// which tab they describe and never copies state between tabs.
//
// Four persistence mechanisms meet here and stay separate:
//   * document files   - DocumentFile (.koma), written only by Save/Save As
//   * recovery         - snapshots + index in the recovery folder
//   * session          - SessionStore: tab list, per-tab view, window layout
//   * undo history     - UndoStack, RAM only, never written to disk

class Settings;

class WorkspaceState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BrushSettings* brush READ brush CONSTANT)
    Q_PROPERTY(SubToolLibrary* subTools READ subTools CONSTANT)
    Q_PROPERTY(CanvasDocument* document  READ document  NOTIFY documentChanged)
    Q_PROPERTY(LayerModel*     layers    READ layers    NOTIFY documentChanged)
    Q_PROPERTY(TimelineState*  timeline  READ timeline  CONSTANT)
    Q_PROPERTY(ColorPalette*   colors    READ colors    CONSTANT)
    Q_PROPERTY(UndoStack*      history   READ history   NOTIFY documentChanged)
    Q_PROPERTY(QVariantList projectTabs READ projectTabs NOTIFY tabsChanged)
    Q_PROPERTY(int activeTab READ activeTab NOTIFY tabsChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryAvailableChanged)
    Q_PROPERTY(bool recoveryReady READ recoveryReady NOTIFY recoveryAvailableChanged)
    Q_PROPERTY(QVariantList recoveryEntries READ recoveryEntries NOTIFY recoveryEntriesChanged)
    Q_PROPERTY(QString         currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QString         activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(qreal           zoom      READ zoom      WRITE setZoom      NOTIFY zoomChanged)

public:
    explicit WorkspaceState(QObject* parent = nullptr);

    BrushSettings* brush() const { return m_brush; }
    SubToolLibrary* subTools() const { return m_subTools; }
    CanvasDocument* document() const { return m_document; }
    LayerModel*     layers()   const { return m_document->layers(); }
    TimelineState*  timeline() const { return m_timeline; }
    ColorPalette*   colors()   const { return m_colors; }
    UndoStack*      history()  const { return m_history; }

    QString activeTool() const { return m_activeTool; }
    qreal   zoom()       const { return m_zoom; }
    QString currentFile() const { return m_currentFile; }
    QVariantList projectTabs() const;
    int activeTab() const { return m_activeTab; }
    Q_INVOKABLE bool activateTab(int index);
    // Discard must be explicit; the UI is responsible for confirmation.
    Q_INVOKABLE bool closeTab(int index, bool discard = false);

    // ---- Per-project view state --------------------------------------
    // Tool, zoom and viewport position belong to the tab, not the window.
    // The canvas reports its position here; the workspace hands it back when
    // the tab is shown again (and after a restart, through the session).
    Q_INVOKABLE void storeActiveView(qreal zoom, qreal centerX, qreal centerY);
    Q_INVOKABLE bool activeViewStored() const;
    Q_INVOKABLE QPointF activeViewCenter() const;

    // ---- Recovery (crash safety; separate from Save and from undo) -----
    bool initializeRecovery(const QString& directory);
    // True while at least one recovery entry is waiting for a decision.
    bool recoveryAvailable() const { return !m_pendingRecovery.empty(); }
    bool recoveryReady() const { return bool(m_recoveryLock); }
    // One map per recoverable project: key, title, saved (had a file),
    // originalPath, fileName, originalState ("unchanged", "changed",
    // "missing" or empty for never-saved work), snapshotTime, status
    // ("ready" or "damaged") and message.
    QVariantList recoveryEntries() const;
    // Recovers one entry into a new tab. The original file is never written.
    // If the original changed or is missing, or is already open, the result
    // is a detached project without a filename (Save As). A failure marks
    // only that entry as damaged and keeps its data.
    Q_INVOKABLE bool recoverEntry(const QString& key);
    // Permanently removes one entry and its snapshot. Explicit user choice.
    Q_INVOKABLE bool discardEntry(const QString& key);
    // Recovers every waiting entry; an entry that fails stays waiting and
    // does not stop the others. True only when all entries were recovered.
    Q_INVOKABLE bool restoreWorkspace();
    // Discards every waiting entry.
    Q_INVOKABLE bool discardRecovery();
    // Writes one independent, verified snapshot per project that has work
    // not present in a saved file (modified, or never saved). discardIds:
    // tabs whose recovery data the user explicitly gave up (Don't Save).
    // excludeIds: tabs to leave out of this pass. A project whose snapshot
    // cannot be written keeps its previous valid snapshot.
    Q_INVOKABLE bool persistWorkspace(const QStringList& discardIds = {}, const QStringList& excludeIds = {});
    Q_INVOKABLE void setRecoveryInterval(int minutes);
    // True while periodic recovery snapshots are scheduled. Never (0) and an
    // unavailable recovery location leave this false.
    bool recoveryTimerActive() const { return m_recoveryTimer.isActive(); }
    // Test hooks. Stage 1: snapshot writing fails. Stage 2: index replacement
    // fails. The storage probe replaces the real free-space query.
    void setRecoveryFailureForTests(int stage) { m_recoveryFailureForTests = stage; }
    void setRecoveryStorageProbeForTests(std::function<qint64()> probe) { m_storageProbe = std::move(probe); }

    // ---- Session (tab list, per-tab view, window layout) ---------------
    // Remembers where the session file lives and reads it. Nothing is
    // reopened and nothing is written until restoreSession() has run.
    bool initializeSession(const QString& filePath);
    bool sessionReady() const { return !m_sessionPath.isEmpty(); }
    // Reopens the saved projects of the previous session, each with its own
    // tool, zoom and viewport position, and re-activates the previous active
    // tab. A project that cannot be reopened is reported and skipped; its
    // recovery data, if any, is not touched. Projects that have a recovery
    // entry waiting are left to the recovery decision instead.
    Q_INVOKABLE bool restoreSession();
    Q_INVOKABLE bool saveSession();
    // Window layout values (panel visibility, dock sizes) kept in the session.
    Q_INVOKABLE QVariant sessionValue(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void setSessionValue(const QString& key, const QVariant& value);
    // Last step of closing the application, after every Save / Don't Save /
    // Cancel decision has been made: writes the session and removes recovery
    // data for the projects in discardIds. Never blocks the shutdown.
    Q_INVOKABLE void finishShutdown(const QStringList& discardIds = {});

    Q_INVOKABLE bool newDocument(int width, int height, bool transparent, const QColor& background);
    // Creates a new illustration project from the New Project dialog: applies
    // name, canvas dimensions, DPI and background, then opens an independent
    // project tab with its own session history. Returns
    // false and leaves the current project untouched on invalid input. The
    // failure reason is available via creationError().
    Q_INVOKABLE bool newProject(const QString& name, int width, int height, bool transparent,
                                const QString& backgroundColor, qreal dpi);
    // Human-readable reason for the last failed project creation, empty on
    // success. Notified via creationErrorChanged().
    Q_PROPERTY(QString creationError READ creationError NOTIFY creationErrorChanged)
    QString creationError() const { return m_creationError; }
    // Opens a document in an independent tab (retains the previous tab).
    void replaceDocument(CanvasDocument* next);
    // Saves the current document. pathOrUrl may be a plain local path or a
    // file:// URL (as delivered by QML FileDialog). Empty path saves to the
    // current file when one is set. On failure the file on disk is untouched
    // and fileErrorOccurred carries a user-facing message.
    Q_INVOKABLE bool saveToPath(const QString& pathOrUrl);
    // Loads the document at pathOrUrl, fully validates it, then atomically
    // opens it in a new tab. On any failure the current
    // document is left completely unchanged and fileErrorOccurred fires.
    Q_INVOKABLE bool openFromPath(const QString& pathOrUrl);
    // Per-user Windows registration so double-clicking a .koma file opens it
    // in this build. Emits fileAssociationCompleted with a user-readable
    // message; never modifies system-wide settings. Not supported off Windows.
    Q_INVOKABLE void associateWithKomaFiles();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomFit();
    Q_INVOKABLE void resetZoom();
    // Diagnostic logging for New Project troubleshooting. Appends a timestamped
    // message to a log file in the user's Documents folder so it can be
    // retrieved without a terminal. Returns the log file path.
    Q_INVOKABLE QString diagnosticLogPath() const;
    Q_INVOKABLE void writeDiagnosticLog(const QString& message);
    Q_INVOKABLE void clearDiagnosticLog();

public slots:
    void setActiveTool(const QString& tool);
    void setZoom(qreal z);

signals:
    void documentChanged();
    void activeToolChanged(const QString& tool);
    void zoomChanged(qreal zoom);
    void currentFileChanged();
    void tabsChanged();
    void recoveryAvailableChanged();
    void recoveryEntriesChanged();
    void creationErrorChanged();
    void fileErrorOccurred(const QString& message);
    void fileAssociationCompleted(const QString& message, bool ok);

private:
    // Clears artwork history when a layer is deleted, because layer deletion
    // is not yet undoable. Connected to every document we own or swap in.
    void connectLayerDeletion(const CanvasDocument* document, UndoStack* history);
    struct Tab {
        CanvasDocument* document;
        UndoStack* history;
        QString path;
        QUuid id;
        QByteArray savedHash;
        // Recovery type id recorded when the tab was created. The workspace
        // never guesses a serializer at snapshot time.
        QString typeId;
        // View state owned by this project.
        QString tool = QStringLiteral("brush");
        qreal zoom = 100.0;
        bool hasView = false;
        QPointF viewCenter;
    };
    // A recovery snapshot found at startup that the user has not decided on.
    struct RecoveryEntry {
        QString key;            // snapshot file name; unique in the folder
        QJsonObject json;       // index entry, carried forward unchanged
        QUuid tabId;
        QString title;
        QString path;           // original file, empty for never-saved work
        QString typeId;
        QString originalState;  // "", "unchanged", "changed", "missing"
        QString status;         // "ready" or "damaged"
        QString message;
        QDateTime snapshotTime;
    };
    Tab makeTab(CanvasDocument* document, UndoStack* history, const QString& path) const;
    int tabIndexForPath(const QString& path) const;
    int tabIndexForId(const QUuid& id) const;
    bool isPristinePlaceholder(const Tab& tab) const;
    bool tabNeedsSnapshot(const Tab& tab) const;
    void applyActiveTab();
    void loadPendingRecovery();
    void addDocument(CanvasDocument* document, const QString& path = {},
                     const std::function<void(Tab&)>& prepare = {});
    bool recoverEntryInternal(const QString& key, QString* error, QString* notice);
    bool persistInternal(const QStringList& discardIds, const QStringList& excludeIds, QString* error);
    bool commitRecoveryIndex(const QList<QJsonObject>& entries, const QString& activeId, QString* error);
    bool dropRecoveryEntriesForTabs(const QStringList& tabIds, QString* error);
    qint64 recoveryStorageAvailable() const;

    std::vector<Tab> m_tabs;
    int m_activeTab = 0;
    bool m_initialPlaceholder = true;
    QString m_recoveryDirectory;
    std::unique_ptr<QLockFile> m_recoveryLock;
    QTimer m_recoveryTimer;
    std::vector<RecoveryEntry> m_pendingRecovery;
    QString m_recoveryActiveId;
    QString m_lastTimerRecoveryError;
    std::function<qint64()> m_storageProbe;
    int m_recoveryFailureForTests = 0;
    QString m_sessionPath;
    SessionData m_session;
    bool m_sessionLoaded = false;      // a valid session file was read
    bool m_sessionRestorePending = false;  // restoreSession() has not run yet
    bool m_sessionBatch = false;       // suppress saves while restoring
    BrushSettings* m_brush;
    SubToolLibrary* m_subTools;
    CanvasDocument* m_document;
    TimelineState*  m_timeline;
    ColorPalette*   m_colors;
    UndoStack*      m_history;
    ProjectCreationService* m_creationService;
    QString         m_activeTool = QStringLiteral("brush");
    QString         m_currentFile;
    QString         m_creationError;
    qreal           m_zoom       = 100.0;
};

}  // namespace komapaint
