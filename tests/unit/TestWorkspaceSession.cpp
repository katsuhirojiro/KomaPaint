// Phase 3: multi-document workspace, recovery snapshots and session restore.
//
// Every test here drives the real WorkspaceState with real documents, real
// .koma files and a real recovery folder in a temporary directory. A "crash"
// is simulated by destroying the workspace without the shutdown step, which
// leaves exactly what a killed process leaves on disk.
#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <functional>
#include <memory>

#include "app/SessionStore.h"
#include "app/WorkspaceState.h"
#include "files/DocumentFile.h"
#include "layers/Layer.h"

using namespace komapaint;

namespace {

const quint32 kRed = 0xffff0000U;
const quint32 kGreen = 0xff00ff00U;
const quint32 kBlue = 0xff0000ffU;
const quint32 kWhite = 0xffffffffU;

bool paint(WorkspaceState& ws, QPoint at, quint32 color)
{
    auto* layer = ws.layers()->layerAt(ws.layers()->activeLayerIndex());
    if (!ws.history()->beginStroke(layer)) return false;
    if (layer->tiles().setPixel({at.x(), at.y()}, color) != SparseTileStore::WriteResult::Changed) {
        ws.history()->cancelStroke();
        return false;
    }
    return ws.history()->commitStroke();
}

quint32 pixel(const WorkspaceState& ws, QPoint at, int layer = 0)
{
    return ws.layers()->layerAt(layer)->tiles().pixel({at.x(), at.y()}).value();
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QStringList snapshotFiles(const QString& recovery)
{
    return QDir(recovery).entryList({QStringLiteral("*.koma")}, QDir::Files, QDir::Name);
}

QJsonObject indexRoot(const QString& recovery)
{
    return QJsonDocument::fromJson(fileBytes(recovery + QStringLiteral("/workspace.json"))).object();
}

QJsonArray indexProjects(const QString& recovery)
{
    return indexRoot(recovery).value(QStringLiteral("projects")).toArray();
}

QString tabId(const WorkspaceState& ws, int index)
{
    return ws.projectTabs().at(index).toMap().value(QStringLiteral("id")).toString();
}

QString tabTitle(const WorkspaceState& ws, int index)
{
    return ws.projectTabs().at(index).toMap().value(QStringLiteral("title")).toString();
}

QVariantMap entryNamed(const WorkspaceState& ws, const QString& title)
{
    for (const auto& value : ws.recoveryEntries()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("title")).toString() == title) return item;
    }
    return {};
}

QString snapshotOf(const QString& recovery, const QString& title)
{
    for (const auto& value : indexProjects(recovery)) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("title")).toString() == title)
            return recovery + QLatin1Char('/') + item.value(QStringLiteral("snapshot")).toString();
    }
    return {};
}

// Rewrites the index with a correct checksum, the way the application would.
bool rewriteIndex(const QString& recovery, const std::function<void(QJsonObject&)>& edit)
{
    auto root = indexRoot(recovery);
    root.remove(QStringLiteral("sha256"));
    edit(root);
    root.insert(QStringLiteral("sha256"),
                QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(root).toJson(QJsonDocument::Compact),
                                                             QCryptographicHash::Sha256).toHex()));
    return writeBytes(recovery + QStringLiteral("/workspace.json"), QJsonDocument(root).toJson(QJsonDocument::Compact));
}

}  // namespace

class TestWorkspaceSession : public QObject
{
    Q_OBJECT
private slots:
    // A. documents and tabs
    void documentsAreIndependent();
    void viewStateBelongsToEachDocument();
    void saveAndSaveAsAffectOnlyTheActiveDocument();
    void closingOneDocumentLeavesTheOthers();
    // B/C. snapshots and recovery
    void snapshotsCoverUnsavedAndModifiedSavedDocuments();
    void recoversEachEntryIndependently();
    void discardsOneEntryAndKeepsTheOthers();
    void damagedEntriesDoNotBlockHealthyOnes();
    void interruptedWritesKeepThePreviousSnapshot();
    void damagedOrMissingIndexStillOffersSnapshots();
    void missingOriginalRecoversDetached();
    void changedOriginalRecoversDetached();
    void openOriginalRecoversDetached();
    void legacyVersion1IndexIsReadable();
    void postponedEntriesSurviveNewSnapshots();
    void obsoleteSnapshotsAreCleaned();
    // F. resources
    void lowStorageSkipsOnlyTheDocumentThatDoesNotFit();
    void unavailableRecoveryLocationFailsSafely();
    // D/E. shutdown and session
    void shutdownKeepsSavedDropsDiscarded();
    void sessionRestoresTabsActiveDocumentAndViews();
    void sessionReopenFailureIsReportedAndKeepsRecoveryData();
    void sessionFileIsValidatedAndReplacedAtomically();
};

// ---------------------------------------------------------------- A

void TestWorkspaceSession::documentsAreIndependent()
{
    WorkspaceState ws;
    QVERIFY(ws.newProject(QStringLiteral("Alpha"), 100, 80, true, QStringLiteral("white"), 72));
    auto* alpha = ws.document();
    auto* alphaLayers = ws.layers();
    auto* alphaHistory = ws.history();
    QVERIFY(paint(ws, {2, 3}, kRed));
    ws.layers()->addLayer();
    ws.layers()->layerAt(1)->setOpacity(0.25);

    QVERIFY(ws.newProject(QStringLiteral("Beta"), 64, 48, false, QStringLiteral("#336699"), 144));
    auto* beta = ws.document();
    QVERIFY(beta != alpha);
    QVERIFY(ws.layers() != alphaLayers);
    QVERIFY(ws.history() != alphaHistory);
    // A new document starts empty: nothing leaked from Alpha.
    QCOMPARE(ws.layers()->rowCount(), 1);
    QCOMPARE(pixel(ws, {2, 3}), 0U);
    QVERIFY(!ws.history()->canUndo());
    QVERIFY(!ws.document()->isModified());
    QVERIFY(paint(ws, {2, 3}, kBlue));
    QVERIFY(paint(ws, {4, 4}, kGreen));

    QVERIFY(ws.newProject(QStringLiteral("Gamma"), 300, 200, true, QStringLiteral("white"), 300));
    QCOMPARE(ws.projectTabs().size(), 3);
    QCOMPARE(ws.activeTab(), 2);
    QVERIFY(!ws.document()->isModified());

    // Alpha is exactly as it was left.
    QVERIFY(ws.activateTab(0));
    QCOMPARE(ws.document(), alpha);
    QCOMPARE(ws.document()->title(), QStringLiteral("Alpha"));
    QCOMPARE(ws.document()->size(), QSize(100, 80));
    QCOMPARE(ws.document()->dpi(), 72.0);
    QVERIFY(ws.document()->hasTransparentBackground());
    QCOMPARE(ws.layers()->rowCount(), 2);
    QCOMPARE(ws.layers()->layerAt(1)->opacity(), 0.25);
    QCOMPARE(pixel(ws, {2, 3}), kRed);
    QCOMPARE(pixel(ws, {4, 4}), 0U);
    QVERIFY(ws.document()->isModified());

    // Beta is exactly as it was left.
    QVERIFY(ws.activateTab(1));
    QCOMPARE(ws.document(), beta);
    QCOMPARE(ws.document()->size(), QSize(64, 48));
    QCOMPARE(ws.document()->dpi(), 144.0);
    QVERIFY(!ws.document()->hasTransparentBackground());
    QCOMPARE(ws.document()->backgroundColor(), QColor(QStringLiteral("#336699")));
    QCOMPARE(ws.layers()->rowCount(), 1);
    QCOMPARE(pixel(ws, {2, 3}), kBlue);

    // Undo in Beta undoes Beta's last stroke only.
    ws.history()->undo();
    QCOMPARE(pixel(ws, {4, 4}), 0U);
    QCOMPARE(pixel(ws, {2, 3}), kBlue);
    QVERIFY(ws.history()->canRedo());
    QVERIFY(ws.activateTab(0));
    QCOMPARE(pixel(ws, {2, 3}), kRed);
    QVERIFY(ws.history()->canUndo());
    QVERIFY(!ws.history()->canRedo());  // Beta's redo step is not Alpha's
    ws.history()->undo();
    QCOMPARE(pixel(ws, {2, 3}), 0U);
    QVERIFY(ws.history()->canRedo());
    // Alpha's undo did not move Beta's history or artwork.
    QVERIFY(ws.activateTab(1));
    QCOMPARE(pixel(ws, {2, 3}), kBlue);
    QVERIFY(ws.history()->canUndo());
    QVERIFY(ws.history()->canRedo());
    ws.history()->redo();
    QCOMPARE(pixel(ws, {4, 4}), kGreen);
    QVERIFY(ws.activateTab(0));
    QCOMPARE(pixel(ws, {2, 3}), 0U);
    ws.history()->redo();
    QCOMPARE(pixel(ws, {2, 3}), kRed);

    // The untouched third document stayed untouched through all of that.
    QVERIFY(ws.activateTab(2));
    QCOMPARE(ws.document()->title(), QStringLiteral("Gamma"));
    QVERIFY(!ws.document()->isModified());
    QVERIFY(!ws.history()->canUndo());
    QCOMPARE(pixel(ws, {2, 3}), 0U);

    // A stroke in progress pins the active document.
    QVERIFY(ws.history()->beginStroke(ws.layers()->layerAt(0)));
    QSignalSpy errors(&ws, &WorkspaceState::fileErrorOccurred);
    QVERIFY(!ws.activateTab(0));
    QCOMPARE(ws.activeTab(), 2);
    QCOMPARE(errors.size(), 1);
    QVERIFY(ws.history()->cancelStroke());
    QVERIFY(ws.activateTab(0));
}

void TestWorkspaceSession::viewStateBelongsToEachDocument()
{
    WorkspaceState ws;
    QVERIFY(ws.newProject(QStringLiteral("Alpha"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(!ws.activeViewStored());  // never shown: the canvas fits it
    ws.setActiveTool(QStringLiteral("eraser"));
    ws.storeActiveView(250.0, 10.5, 20.25);
    QVERIFY(ws.activeViewStored());
    QCOMPARE(ws.zoom(), 250.0);

    QVERIFY(ws.newProject(QStringLiteral("Beta"), 64, 48, true, QStringLiteral("white"), 72));
    // A new project keeps the tool in the user's hand but has no view yet.
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QVERIFY(!ws.activeViewStored());
    QCOMPARE(ws.zoom(), 100.0);
    ws.setActiveTool(QStringLiteral("fill"));
    ws.storeActiveView(50.0, 5.0, 6.0);

    QSignalSpy toolSpy(&ws, &WorkspaceState::activeToolChanged);
    QSignalSpy zoomSpy(&ws, &WorkspaceState::zoomChanged);
    QVERIFY(ws.activateTab(0));
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QCOMPARE(ws.zoom(), 250.0);
    QCOMPARE(ws.activeViewCenter(), QPointF(10.5, 20.25));
    QCOMPARE(toolSpy.size(), 1);
    QCOMPARE(zoomSpy.size(), 1);

    // Changing Alpha's view now must not move Beta's.
    ws.zoomIn();
    ws.setActiveTool(QStringLiteral("brush"));
    ws.storeActiveView(ws.zoom(), 77.0, 88.0);
    QVERIFY(ws.activateTab(1));
    QCOMPARE(ws.activeTool(), QStringLiteral("fill"));
    QCOMPARE(ws.zoom(), 50.0);
    QCOMPARE(ws.activeViewCenter(), QPointF(5.0, 6.0));
    QVERIFY(ws.activateTab(0));
    QCOMPARE(ws.activeTool(), QStringLiteral("brush"));
    QCOMPARE(ws.zoom(), 312.5);
    QCOMPARE(ws.activeViewCenter(), QPointF(77.0, 88.0));

    // Garbage from a broken caller is ignored rather than stored.
    ws.storeActiveView(qQNaN(), 1.0, 1.0);
    ws.storeActiveView(100.0, qInf(), 1.0);
    QCOMPARE(ws.zoom(), 312.5);
    QCOMPARE(ws.activeViewCenter(), QPointF(77.0, 88.0));
}

void TestWorkspaceSession::saveAndSaveAsAffectOnlyTheActiveDocument()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString a = dir.filePath(QStringLiteral("a.koma"));
    const QString a2 = dir.filePath(QStringLiteral("a-copy.koma"));
    const QString b = dir.filePath(QStringLiteral("b.koma"));
    WorkspaceState ws;
    QStringList errors;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&errors](const QString& m) { errors << m; });
    QVERIFY(ws.newProject(QStringLiteral("Alpha"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kRed));
    QVERIFY(ws.newProject(QStringLiteral("Beta"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kBlue));

    // Save Alpha: Beta stays unsaved, modified and without a file.
    QVERIFY(ws.activateTab(0));
    QVERIFY(ws.saveToPath(a));
    QCOMPARE(ws.currentFile(), a);
    QVERIFY(!ws.document()->isModified());
    QVERIFY(ws.history()->canUndo());
    QVERIFY(ws.activateTab(1));
    QVERIFY(ws.currentFile().isEmpty());
    QVERIFY(ws.document()->isModified());
    QVERIFY(!QFile::exists(b));
    QVERIFY(ws.saveToPath(b));
    const QByteArray bBytes = fileBytes(b);

    // Save As moves only Alpha to the new file; the first file is untouched.
    QVERIFY(ws.activateTab(0));
    const QByteArray aBytes = fileBytes(a);
    QVERIFY(paint(ws, {2, 2}, kGreen));
    QVERIFY(ws.saveToPath(a2));
    QCOMPARE(ws.currentFile(), a2);
    QCOMPARE(fileBytes(a), aBytes);
    QCOMPARE(fileBytes(b), bBytes);
    {
        std::unique_ptr<CanvasDocument> first(DocumentFile::open(a));
        std::unique_ptr<CanvasDocument> second(DocumentFile::open(a2));
        QVERIFY(first && second);
        QCOMPARE(first->layers()->layerAt(0)->tiles().pixel({2, 2}).value(), 0U);
        QCOMPARE(second->layers()->layerAt(0)->tiles().pixel({2, 2}).value(), kGreen);
        QCOMPARE(second->layers()->layerAt(0)->tiles().pixel({1, 1}).value(), kRed);
    }

    // One file cannot be claimed by two tabs.
    QVERIFY(ws.activateTab(1));
    QVERIFY(paint(ws, {3, 3}, kWhite));
    errors.clear();
    QVERIFY(!ws.saveToPath(a2));
    QCOMPARE(errors.size(), 1);
    QCOMPARE(ws.currentFile(), b);
    QVERIFY(ws.document()->isModified());
    {
        std::unique_ptr<CanvasDocument> still(DocumentFile::open(a2));
        QVERIFY(still);
        QCOMPARE(still->layers()->layerAt(0)->tiles().pixel({1, 1}).value(), kRed);  // not Beta's blue
    }

    // Opening a file that is already open shows its tab; opening another
    // file adds a tab.
    QCOMPARE(ws.projectTabs().size(), 2);
    QVERIFY(ws.openFromPath(a2));
    QCOMPARE(ws.projectTabs().size(), 2);
    QCOMPARE(ws.activeTab(), 0);
    QVERIFY(ws.openFromPath(a));
    QCOMPARE(ws.projectTabs().size(), 3);
    QCOMPARE(ws.currentFile(), a);
    QVERIFY(!ws.history()->canUndo());
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QCOMPARE(pixel(ws, {2, 2}), 0U);
}

void TestWorkspaceSession::closingOneDocumentLeavesTheOthers()
{
    WorkspaceState ws;
    QVERIFY(ws.newProject(QStringLiteral("Alpha"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kRed));
    auto* alpha = ws.document();
    QVERIFY(ws.newProject(QStringLiteral("Beta"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kBlue));
    QVERIFY(ws.newProject(QStringLiteral("Gamma"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kGreen));
    auto* gamma = ws.document();

    // A modified document is never closed without an explicit decision.
    QVERIFY(!ws.closeTab(1));
    QCOMPARE(ws.projectTabs().size(), 3);
    QVERIFY(ws.closeTab(1, true));
    QCOMPARE(ws.projectTabs().size(), 2);
    QCOMPARE(tabTitle(ws, 0), QStringLiteral("Alpha"));
    QCOMPARE(tabTitle(ws, 1), QStringLiteral("Gamma"));
    // The active document (Gamma) is still active and intact.
    QCOMPARE(ws.document(), gamma);
    QCOMPARE(pixel(ws, {1, 1}), kGreen);
    QVERIFY(ws.history()->canUndo());
    QVERIFY(ws.activateTab(0));
    QCOMPARE(ws.document(), alpha);
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QVERIFY(ws.history()->canUndo());
    ws.history()->undo();
    QCOMPARE(pixel(ws, {1, 1}), 0U);

    // Closing the active tab activates a neighbour; closing the last tab
    // leaves an empty untitled document rather than no document.
    QVERIFY(ws.closeTab(0, true));
    QCOMPARE(ws.document(), gamma);
    QVERIFY(ws.closeTab(0, true));
    QCOMPARE(ws.projectTabs().size(), 1);
    QVERIFY(ws.document() != gamma);
    QVERIFY(!ws.document()->isModified());
    QVERIFY(!ws.history()->canUndo());
    QVERIFY(!ws.closeTab(5));
    QVERIFY(!ws.closeTab(-1));
}

// ---------------------------------------------------------------- B / C

void TestWorkspaceSession::snapshotsCoverUnsavedAndModifiedSavedDocuments()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString saved = dir.filePath(QStringLiteral("saved.koma"));
    const QString clean = dir.filePath(QStringLiteral("clean.koma"));
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    // The untouched start-up document holds no work and gets no snapshot.
    QVERIFY(ws.persistWorkspace());
    QVERIFY(snapshotFiles(recovery).isEmpty());

    QVERIFY(ws.newProject(QStringLiteral("Unsaved"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kRed));
    QVERIFY(ws.newProject(QStringLiteral("Saved"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kGreen));
    QVERIFY(ws.saveToPath(saved));
    QVERIFY(paint(ws, {2, 2}, kBlue));  // newer than the file
    QVERIFY(ws.newProject(QStringLiteral("Clean"), 100, 80, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kWhite));
    QVERIFY(ws.saveToPath(clean));
    const QByteArray savedBytes = fileBytes(saved);
    const QByteArray cleanBytes = fileBytes(clean);

    QVERIFY(ws.persistWorkspace());
    // One independent snapshot per document that has work outside a file.
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    const auto projects = indexProjects(recovery);
    QCOMPARE(projects.size(), 2);
    QCOMPARE(indexRoot(recovery).value(QStringLiteral("version")).toInt(), 2);
    QStringList titles;
    for (const auto& value : projects) titles << value.toObject().value(QStringLiteral("title")).toString();
    QVERIFY(titles.contains(QStringLiteral("Unsaved")));
    QVERIFY(titles.contains(QStringLiteral("Saved")));
    // Snapshots are separate files; the user's project files were not written.
    QCOMPARE(fileBytes(saved), savedBytes);
    QCOMPARE(fileBytes(clean), cleanBytes);
    QVERIFY(snapshotOf(recovery, QStringLiteral("Saved")) != saved);

    // A snapshot holds the artwork, layers and settings, and no undo history:
    // it is an ordinary document file.
    std::unique_ptr<CanvasDocument> snapshot(DocumentFile::open(snapshotOf(recovery, QStringLiteral("Saved"))));
    QVERIFY(snapshot);
    QCOMPARE(snapshot->size(), QSize(100, 80));
    QCOMPARE(snapshot->layers()->layerAt(0)->tiles().pixel({1, 1}).value(), kGreen);
    QCOMPARE(snapshot->layers()->layerAt(0)->tiles().pixel({2, 2}).value(), kBlue);
    // Taking the snapshot changed nothing in the live documents.
    QVERIFY(ws.activateTab(1));
    QVERIFY(ws.document()->isModified());
    QVERIFY(ws.history()->canUndo());
    QCOMPARE(ws.currentFile(), saved);
}

void TestWorkspaceSession::recoversEachEntryIndependently()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString saved = dir.filePath(QStringLiteral("saved.koma"));
    QByteArray savedBytes;
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Unsaved"), 90, 70, true, QStringLiteral("white"), 144));
        QVERIFY(paint(ws, {1, 1}, kRed));
        ws.layers()->addLayer();
        ws.layers()->layerAt(1)->setVisible(false);
        ws.setActiveTool(QStringLiteral("eraser"));
        ws.storeActiveView(400.0, 12.0, 34.0);
        QVERIFY(ws.newProject(QStringLiteral("Saved"), 100, 80, false, QStringLiteral("#102030"), 72));
        QVERIFY(paint(ws, {1, 1}, kGreen));
        QVERIFY(ws.saveToPath(saved));
        savedBytes = fileBytes(saved);
        QVERIFY(paint(ws, {1, 1}, kBlue));
        QVERIFY(ws.persistWorkspace());
        // crash: no shutdown step
    }
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    QVERIFY(ws.recoveryAvailable());
    QCOMPARE(ws.recoveryEntries().size(), 2);
    // Nothing is opened until the user decides.
    QCOMPARE(ws.projectTabs().size(), 1);
    QVERIFY(!ws.document()->isModified());

    // The list tells saved from never-saved work and names the original.
    const auto unsavedEntry = entryNamed(ws, QStringLiteral("Unsaved"));
    const auto savedEntry = entryNamed(ws, QStringLiteral("Saved"));
    QVERIFY(!unsavedEntry.isEmpty() && !savedEntry.isEmpty());
    QCOMPARE(unsavedEntry.value(QStringLiteral("saved")).toBool(), false);
    QCOMPARE(unsavedEntry.value(QStringLiteral("fileName")).toString(), QString());
    QCOMPARE(unsavedEntry.value(QStringLiteral("originalState")).toString(), QString());
    QCOMPARE(savedEntry.value(QStringLiteral("saved")).toBool(), true);
    QCOMPARE(savedEntry.value(QStringLiteral("fileName")).toString(), QStringLiteral("saved.koma"));
    QCOMPARE(savedEntry.value(QStringLiteral("originalState")).toString(), QStringLiteral("unchanged"));
    QCOMPARE(savedEntry.value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
    QVERIFY(!savedEntry.value(QStringLiteral("snapshotTime")).toString().isEmpty());

    // Recover only the saved project.
    QVERIFY(ws.recoverEntry(savedEntry.value(QStringLiteral("key")).toString()));
    QCOMPARE(ws.projectTabs().size(), 1);  // replaced the empty start-up tab
    QCOMPARE(ws.document()->title(), QStringLiteral("Saved"));
    QCOMPARE(ws.currentFile(), saved);
    QVERIFY(ws.document()->isModified());          // it differs from the file
    QCOMPARE(pixel(ws, {1, 1}), kBlue);            // the newer, unsaved state
    QVERIFY(!ws.document()->hasTransparentBackground());
    QCOMPARE(ws.document()->backgroundColor(), QColor(QStringLiteral("#102030")));
    QVERIFY(!ws.history()->canUndo());             // undo history is never recovered
    QCOMPARE(fileBytes(saved), savedBytes);        // the original was not written
    QVERIFY(messages.isEmpty());
    // The other entry is still waiting, untouched.
    QVERIFY(ws.recoveryAvailable());
    QCOMPARE(ws.recoveryEntries().size(), 1);

    // Recover the never-saved project.
    QVERIFY(ws.recoverEntry(unsavedEntry.value(QStringLiteral("key")).toString()));
    QVERIFY(!ws.recoveryAvailable());
    QCOMPARE(ws.projectTabs().size(), 2);
    QCOMPARE(ws.document()->title(), QStringLiteral("Unsaved"));
    QVERIFY(ws.currentFile().isEmpty());
    QVERIFY(ws.document()->isModified());
    QCOMPARE(ws.document()->size(), QSize(90, 70));
    QCOMPARE(ws.document()->dpi(), 144.0);
    QCOMPARE(ws.layers()->rowCount(), 2);
    QVERIFY(!ws.layers()->layerAt(1)->isVisible());
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QVERIFY(!ws.history()->canUndo());
    // Its own tool and view came back with it.
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QCOMPARE(ws.zoom(), 400.0);
    QCOMPARE(ws.activeViewCenter(), QPointF(12.0, 34.0));

    // Recovered documents are ordinary documents: new undo works, and an
    // explicit Save by the user is what finally writes the file.
    QVERIFY(ws.activateTab(0));
    QVERIFY(paint(ws, {5, 5}, kWhite));
    ws.history()->undo();
    QCOMPARE(pixel(ws, {5, 5}), 0U);
    QVERIFY(ws.saveToPath(QString()));
    QVERIFY(fileBytes(saved) != savedBytes);
    QVERIFY(!ws.recoverEntry(QStringLiteral("no-such-entry")));
}

void TestWorkspaceSession::discardsOneEntryAndKeepsTheOthers()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Keep"), 50, 50, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.newProject(QStringLiteral("Drop"), 50, 50, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kBlue));
        QVERIFY(ws.persistWorkspace());
    }
    const QString keepFile = snapshotOf(recovery, QStringLiteral("Keep"));
    const QString dropFile = snapshotOf(recovery, QStringLiteral("Drop"));
    QVERIFY(QFile::exists(keepFile) && QFile::exists(dropFile));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QCOMPARE(ws.recoveryEntries().size(), 2);
        QVERIFY(ws.discardEntry(entryNamed(ws, QStringLiteral("Drop")).value(QStringLiteral("key")).toString()));
        QVERIFY(!QFile::exists(dropFile));
        QVERIFY(QFile::exists(keepFile));
        QCOMPARE(indexProjects(recovery).size(), 1);
        QVERIFY(ws.recoveryAvailable());
        QCOMPARE(ws.recoveryEntries().size(), 1);
        QCOMPARE(ws.projectTabs().size(), 1);  // discarding opens nothing
        QVERIFY(!ws.discardEntry(QStringLiteral("no-such-entry")));
        // "Decide later": the remaining entry is simply left alone.
    }
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QCOMPARE(ws.recoveryEntries().size(), 1);
        QVERIFY(ws.restoreWorkspace());
        QCOMPARE(ws.document()->title(), QStringLiteral("Keep"));
        QCOMPARE(pixel(ws, {1, 1}), kRed);
        QVERIFY(!ws.recoveryAvailable());
    }
    // Discard everything at once.
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.recoveryAvailable());      // the crash before left it again
        QVERIFY(ws.discardRecovery());
        QVERIFY(!ws.recoveryAvailable());
        QVERIFY(snapshotFiles(recovery).isEmpty());
        QCOMPARE(indexProjects(recovery).size(), 0);
        QVERIFY(!ws.discardRecovery());
    }
}

void TestWorkspaceSession::damagedEntriesDoNotBlockHealthyOnes()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        for (const auto& name : {QStringLiteral("Flipped"), QStringLiteral("Truncated"), QStringLiteral("Healthy")}) {
            QVERIFY(ws.newProject(name, 60, 60, true, QStringLiteral("white"), 72));
            QVERIFY(paint(ws, {1, 1}, kRed));
        }
        QVERIFY(ws.persistWorkspace());
    }
    // Checksum failure: same length, different content.
    const QString flipped = snapshotOf(recovery, QStringLiteral("Flipped"));
    QByteArray bytes = fileBytes(flipped);
    QVERIFY(bytes.size() > 40);
    bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x5a);
    QVERIFY(writeBytes(flipped, bytes));
    // Incomplete write: the file stops half way.
    const QString truncated = snapshotOf(recovery, QStringLiteral("Truncated"));
    const QByteArray whole = fileBytes(truncated);
    QVERIFY(writeBytes(truncated, whole.left(whole.size() / 2)));
    const QByteArray indexBefore = fileBytes(recovery + QStringLiteral("/workspace.json"));

    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    QCOMPARE(ws.recoveryEntries().size(), 3);
    QVERIFY(!ws.restoreWorkspace());  // not everything could be recovered...
    // ...but the healthy project was.
    QCOMPARE(ws.projectTabs().size(), 1);
    QCOMPARE(ws.document()->title(), QStringLiteral("Healthy"));
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages.first().contains(QStringLiteral("Flipped")));
    QVERIFY(messages.first().contains(QStringLiteral("Truncated")));
    // The damaged entries are marked, still listed and still on disk.
    QVERIFY(ws.recoveryAvailable());
    QCOMPARE(ws.recoveryEntries().size(), 2);
    QCOMPARE(entryNamed(ws, QStringLiteral("Flipped")).value(QStringLiteral("status")).toString(), QStringLiteral("damaged"));
    QCOMPARE(entryNamed(ws, QStringLiteral("Truncated")).value(QStringLiteral("status")).toString(), QStringLiteral("damaged"));
    QVERIFY(!entryNamed(ws, QStringLiteral("Flipped")).value(QStringLiteral("message")).toString().isEmpty());
    QVERIFY(QFile::exists(flipped) && QFile::exists(truncated));
    QCOMPARE(fileBytes(recovery + QStringLiteral("/workspace.json")), indexBefore);
    // Trying a damaged entry again fails the same safe way.
    QVERIFY(!ws.recoverEntry(entryNamed(ws, QStringLiteral("Flipped")).value(QStringLiteral("key")).toString()));
    QCOMPARE(ws.projectTabs().size(), 1);
    // Work continues: new snapshots are written and the damaged entries are
    // carried along until the user discards them.
    QVERIFY(paint(ws, {2, 2}, kBlue));
    QVERIFY(ws.persistWorkspace());
    QVERIFY(QFile::exists(flipped) && QFile::exists(truncated));
    QCOMPARE(indexProjects(recovery).size(), 3);
    QVERIFY(ws.discardEntry(entryNamed(ws, QStringLiteral("Flipped")).value(QStringLiteral("key")).toString()));
    QVERIFY(!QFile::exists(flipped));
    QVERIFY(QFile::exists(truncated));
}

void TestWorkspaceSession::interruptedWritesKeepThePreviousSnapshot()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString indexPath = recovery + QStringLiteral("/workspace.json");
    QString firstSnapshot;
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QSignalSpy errors(&ws, &WorkspaceState::fileErrorOccurred);
        QVERIFY(ws.newProject(QStringLiteral("Work"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.persistWorkspace());
        firstSnapshot = snapshotOf(recovery, QStringLiteral("Work"));
        const QByteArray indexBytes = fileBytes(indexPath);
        const QByteArray snapshotBytes = fileBytes(firstSnapshot);
        QVERIFY(paint(ws, {1, 1}, kGreen));
        // The snapshot write fails, then the index replacement fails.
        for (int stage : {1, 2}) {
            ws.setRecoveryFailureForTests(stage);
            QVERIFY(!ws.persistWorkspace());
            QCOMPARE(fileBytes(indexPath), indexBytes);
            QCOMPARE(fileBytes(firstSnapshot), snapshotBytes);
            QCOMPARE(snapshotFiles(recovery).size(), 1);  // no half-written leftovers
            // The live document is unaffected by the failure.
            QCOMPARE(pixel(ws, {1, 1}), kGreen);
            QVERIFY(ws.document()->isModified());
            QVERIFY(ws.history()->canUndo());
        }
        QCOMPARE(errors.size(), 2);
        ws.setRecoveryFailureForTests(0);
    }
    // What a process killed in the middle of a write leaves behind:
    // temporary files, and a complete newer snapshot the index never got.
    const QString name = QFileInfo(firstSnapshot).fileName();
    QVERIFY(writeBytes(recovery + QLatin1Char('/') + name + QStringLiteral(".Xy12Zq"), QByteArray("partial")));
    QVERIFY(writeBytes(indexPath + QStringLiteral(".Ab34Cd"), QByteArray("{\"version\":2,")));
    const QString orphan = recovery + QLatin1Char('/') + name.left(37) + QStringLiteral("11111111-2222-3333-4444-555555555555.koma");
    QVERIFY(QFile::copy(firstSnapshot, orphan));

    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QCOMPARE(QDir(recovery).entryList({QStringLiteral("*.koma.*"), QStringLiteral("workspace.json.*")}, QDir::Files).size(), 0);
    QVERIFY(!QFile::exists(orphan));
    QVERIFY(QFile::exists(firstSnapshot));
    QCOMPARE(ws.recoveryEntries().size(), 1);
    QVERIFY(ws.restoreWorkspace());
    QCOMPARE(pixel(ws, {1, 1}), kRed);  // the last snapshot that was fully committed
}

void TestWorkspaceSession::damagedOrMissingIndexStillOffersSnapshots()
{
    for (const bool removeIndex : {false, true}) {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString recovery = dir.filePath(QStringLiteral("recovery"));
        const QString indexPath = recovery + QStringLiteral("/workspace.json");
        {
            WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
            QVERIFY(ws.newProject(QStringLiteral("First"), 60, 60, true, QStringLiteral("white"), 72));
            QVERIFY(paint(ws, {1, 1}, kRed));
            QVERIFY(ws.newProject(QStringLiteral("Second"), 60, 60, true, QStringLiteral("white"), 72));
            QVERIFY(paint(ws, {1, 1}, kBlue));
            QVERIFY(ws.persistWorkspace());
        }
        const QStringList before = snapshotFiles(recovery);
        QCOMPARE(before.size(), 2);
        if (removeIndex) QVERIFY(QFile::remove(indexPath));
        else QVERIFY(writeBytes(indexPath, fileBytes(indexPath).left(25)));  // cut off mid-write
        // A file that is not a snapshot is neither offered nor deleted.
        QVERIFY(writeBytes(recovery + QStringLiteral("/notes.koma"), QByteArray("not a snapshot")));

        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        // Nothing was deleted because there is no trustworthy index.
        QCOMPARE(snapshotFiles(recovery).size(), 3);
        QVERIFY(ws.recoveryAvailable());
        QCOMPARE(ws.recoveryEntries().size(), 2);
        const auto first = entryNamed(ws, QStringLiteral("First"));
        const auto second = entryNamed(ws, QStringLiteral("Second"));
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QCOMPARE(first.value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        QCOMPARE(first.value(QStringLiteral("saved")).toBool(), false);
        QVERIFY(ws.recoverEntry(first.value(QStringLiteral("key")).toString()));
        QCOMPARE(pixel(ws, {1, 1}), kRed);
        QVERIFY(ws.currentFile().isEmpty());
        QVERIFY(ws.document()->isModified());
        // The next snapshot pass writes a fresh, valid index that still
        // refers to the entry the user has not decided on.
        QVERIFY(ws.persistWorkspace());
        const auto projects = indexProjects(recovery);
        QCOMPARE(projects.size(), 2);
        QVERIFY(QFile::exists(recovery + QLatin1Char('/') + second.value(QStringLiteral("key")).toString()));
        QVERIFY(ws.recoverEntry(second.value(QStringLiteral("key")).toString()));
        QCOMPARE(pixel(ws, {1, 1}), kBlue);
        QVERIFY(!ws.recoveryAvailable());
    }
}

void TestWorkspaceSession::missingOriginalRecoversDetached()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString original = dir.filePath(QStringLiteral("gone.koma"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Gone"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.saveToPath(original));
        QVERIFY(paint(ws, {2, 2}, kGreen));
        QVERIFY(ws.persistWorkspace());
    }
    QVERIFY(QFile::remove(original));

    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    const auto entry = entryNamed(ws, QStringLiteral("Gone"));
    QCOMPARE(entry.value(QStringLiteral("saved")).toBool(), true);
    QCOMPARE(entry.value(QStringLiteral("fileName")).toString(), QStringLiteral("gone.koma"));
    QCOMPARE(entry.value(QStringLiteral("originalState")).toString(), QStringLiteral("missing"));
    QVERIFY(ws.recoverEntry(entry.value(QStringLiteral("key")).toString()));
    // Detached: the artwork is there, but it is bound to no file.
    QVERIFY(ws.currentFile().isEmpty());
    QVERIFY(ws.document()->isModified());
    QCOMPARE(ws.document()->title(), QStringLiteral("Gone (recovered)"));
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QCOMPARE(pixel(ws, {2, 2}), kGreen);
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages.first().contains(QStringLiteral("Save As")));
    // Recovery did not recreate the missing file, and a plain Save cannot.
    QVERIFY(!QFile::exists(original));
    QVERIFY(!ws.saveToPath(QString()));
    QVERIFY(!QFile::exists(original));
    QVERIFY(ws.saveToPath(dir.filePath(QStringLiteral("rescued.koma"))));
    QVERIFY(!ws.document()->isModified());
}

void TestWorkspaceSession::changedOriginalRecoversDetached()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString original = dir.filePath(QStringLiteral("shared.koma"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Shared"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.saveToPath(original));
        QVERIFY(paint(ws, {2, 2}, kGreen));
        QVERIFY(ws.persistWorkspace());
    }
    // Someone else (another program, a sync tool, a restored backup) replaced
    // the file with different, valid artwork after the snapshot was taken.
    {
        CanvasDocument other;
        other.setSize(QSize(60, 60));
        other.setTitle(QStringLiteral("Shared"));
        other.layers()->layerAt(0)->tiles().setPixel({9, 9}, kBlue);
        QVERIFY(DocumentFile::save(other, original));
    }
    const QByteArray changedBytes = fileBytes(original);

    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    const auto entry = entryNamed(ws, QStringLiteral("Shared"));
    QCOMPARE(entry.value(QStringLiteral("originalState")).toString(), QStringLiteral("changed"));
    QVERIFY(ws.recoverEntry(entry.value(QStringLiteral("key")).toString()));
    QVERIFY(ws.currentFile().isEmpty());
    QVERIFY(ws.document()->isModified());
    QCOMPARE(pixel(ws, {2, 2}), kGreen);
    QCOMPARE(pixel(ws, {9, 9}), 0U);
    QCOMPARE(messages.size(), 1);
    // The changed original is exactly as the other party left it, and
    // neither Save nor a further snapshot pass can reach it.
    QCOMPARE(fileBytes(original), changedBytes);
    QVERIFY(!ws.saveToPath(QString()));
    QVERIFY(ws.persistWorkspace());
    QCOMPARE(fileBytes(original), changedBytes);
    QVERIFY(ws.saveToPath(dir.filePath(QStringLiteral("mine.koma"))));
    QCOMPARE(fileBytes(original), changedBytes);
    // Both versions exist side by side now.
    QVERIFY(ws.openFromPath(original));
    QCOMPARE(ws.projectTabs().size(), 2);
    QCOMPARE(pixel(ws, {9, 9}), kBlue);
}

void TestWorkspaceSession::openOriginalRecoversDetached()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString original = dir.filePath(QStringLiteral("open.koma"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Open"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.saveToPath(original));
        QVERIFY(paint(ws, {2, 2}, kGreen));
        QVERIFY(ws.persistWorkspace());
    }
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    // The user postpones the decision and opens the saved file by hand.
    QVERIFY(ws.openFromPath(original));
    QCOMPARE(pixel(ws, {2, 2}), 0U);
    QVERIFY(ws.recoverEntry(entryNamed(ws, QStringLiteral("Open")).value(QStringLiteral("key")).toString()));
    // Two tabs, and only one of them owns the file.
    QCOMPARE(ws.projectTabs().size(), 2);
    QVERIFY(ws.currentFile().isEmpty());
    QCOMPARE(pixel(ws, {2, 2}), kGreen);
    QVERIFY(tabId(ws, 0) != tabId(ws, 1));
    QVERIFY(ws.activateTab(0));
    QCOMPARE(ws.currentFile(), original);
    QCOMPARE(pixel(ws, {2, 2}), 0U);
}

void TestWorkspaceSession::legacyVersion1IndexIsReadable()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Old"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.persistWorkspace());
    }
    // Rewrite the index the way the previous build wrote it: version 1, one
    // tool and zoom for the whole window, no per-project title or view.
    QVERIFY(rewriteIndex(recovery, [](QJsonObject& root) {
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("activeTool"), QStringLiteral("eraser"));
        root.insert(QStringLiteral("zoom"), 300.0);
        QJsonArray projects;
        for (const auto& value : root.value(QStringLiteral("projects")).toArray()) {
            auto item = value.toObject();
            for (const auto& key : {QStringLiteral("title"), QStringLiteral("savedAt"), QStringLiteral("tool"),
                                    QStringLiteral("zoom"), QStringLiteral("hasView"), QStringLiteral("centerX"),
                                    QStringLiteral("centerY")})
                item.remove(key);
            projects.append(item);
        }
        root.insert(QStringLiteral("projects"), projects);
    }));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QCOMPARE(ws.recoveryEntries().size(), 1);
        QVERIFY(!ws.recoveryEntries().first().toMap().value(QStringLiteral("title")).toString().isEmpty());
        QVERIFY(ws.restoreWorkspace());
        QCOMPARE(ws.document()->title(), QStringLiteral("Old"));
        QCOMPARE(pixel(ws, {1, 1}), kRed);
        QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
        QCOMPARE(ws.zoom(), 300.0);
        // The next pass upgrades the index.
        QVERIFY(ws.persistWorkspace());
        QCOMPARE(indexRoot(recovery).value(QStringLiteral("version")).toInt(), 2);
    }

    // An index written by a newer build is not understood. It is treated
    // like a damaged index: nothing is deleted, and the snapshot is offered
    // on its own as an unsaved project.
    QVERIFY(rewriteIndex(recovery, [](QJsonObject& root) { root.insert(QStringLiteral("version"), 99); }));
    const QStringList files = snapshotFiles(recovery);
    QCOMPARE(files.size(), 1);
    WorkspaceState later;
    QVERIFY(later.initializeRecovery(recovery));
    QCOMPARE(snapshotFiles(recovery), files);
    QCOMPARE(later.recoveryEntries().size(), 1);
    QVERIFY(later.restoreWorkspace());
    QCOMPARE(pixel(later, {1, 1}), kRed);
    QVERIFY(later.currentFile().isEmpty());
}

void TestWorkspaceSession::postponedEntriesSurviveNewSnapshots()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject(QStringLiteral("Later"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.persistWorkspace());
    }
    const QString postponed = snapshotOf(recovery, QStringLiteral("Later"));
    const QByteArray postponedBytes = fileBytes(postponed);
    {
        // "Decide later", then a whole working session with its own
        // snapshots, a tab close, and a normal shutdown.
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.recoveryAvailable());
        ws.setRecoveryInterval(10);
        QVERIFY(ws.recoveryTimerActive());  // snapshots are not held back
        QVERIFY(ws.newProject(QStringLiteral("Today"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kBlue));
        QVERIFY(ws.persistWorkspace());
        QVERIFY(ws.persistWorkspace());
        QCOMPARE(snapshotFiles(recovery).size(), 2);
        QCOMPARE(fileBytes(postponed), postponedBytes);
        QVERIFY(ws.closeTab(ws.activeTab(), true));
        QCOMPARE(snapshotFiles(recovery).size(), 1);
        ws.finishShutdown();
        QCOMPARE(fileBytes(postponed), postponedBytes);
    }
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QCOMPARE(ws.recoveryEntries().size(), 1);
    QVERIFY(ws.restoreWorkspace());
    QCOMPARE(ws.document()->title(), QStringLiteral("Later"));
    QCOMPARE(pixel(ws, {1, 1}), kRed);
}

void TestWorkspaceSession::obsoleteSnapshotsAreCleaned()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QVERIFY(ws.newProject(QStringLiteral("One"), 60, 60, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kRed));
    QVERIFY(ws.newProject(QStringLiteral("Two"), 60, 60, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kBlue));
    // Repeated passes replace snapshots; they never pile up.
    for (int pass = 0; pass < 4; ++pass) {
        QVERIFY(ws.persistWorkspace());
        QCOMPARE(snapshotFiles(recovery).size(), 2);
    }
    const QString two = snapshotOf(recovery, QStringLiteral("Two"));
    // Saving makes that project's snapshot obsolete; the other one stays.
    QVERIFY(ws.activateTab(0));
    QVERIFY(ws.saveToPath(dir.filePath(QStringLiteral("one.koma"))));
    QCOMPARE(snapshotFiles(recovery).size(), 1);
    QVERIFY(QFile::exists(two));
    // A failed save keeps the recovery snapshot.
    QVERIFY(ws.activateTab(1));
    QVERIFY(!ws.saveToPath(dir.filePath(QStringLiteral("no-such-folder/two.koma"))));
    QVERIFY(QFile::exists(two));
    QVERIFY(ws.document()->isModified());
    // A saved, unchanged project needs no snapshot on later passes either.
    QVERIFY(ws.persistWorkspace());
    QCOMPARE(snapshotFiles(recovery).size(), 1);
    QCOMPARE(indexProjects(recovery).size(), 1);
    // Editing the saved project again brings its snapshot back.
    QVERIFY(ws.activateTab(0));
    QVERIFY(paint(ws, {3, 3}, kGreen));
    QVERIFY(ws.persistWorkspace());
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    // Closing with "Don't Save" is an explicit discard of that project only.
    QVERIFY(ws.closeTab(0, true));
    QCOMPARE(snapshotFiles(recovery).size(), 1);
    QCOMPARE(indexProjects(recovery).first().toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Two"));
}

// ---------------------------------------------------------------- F

void TestWorkspaceSession::lowStorageSkipsOnlyTheDocumentThatDoesNotFit()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    QVERIFY(ws.newProject(QStringLiteral("Small"), 100, 100, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kRed));
    QVERIFY(ws.newProject(QStringLiteral("Large"), 2000, 2000, true, QStringLiteral("white"), 72));
    for (const QPoint at : {QPoint(10, 10), QPoint(600, 10), QPoint(10, 600), QPoint(1200, 1200), QPoint(1900, 300), QPoint(300, 1900)})
        QVERIFY(paint(ws, at, kBlue));
    QVERIFY(ws.persistWorkspace());
    const QString smallBefore = snapshotOf(recovery, QStringLiteral("Small"));
    const QString largeBefore = snapshotOf(recovery, QStringLiteral("Large"));
    const QByteArray largeBytes = fileBytes(largeBefore);
    QVERIFY(fileBytes(largeBefore).size() > fileBytes(smallBefore).size());

    // Both documents change, then the disk is nearly full: room for the
    // small document's snapshot, not for the large one's.
    QVERIFY(paint(ws, {700, 700}, kGreen));
    QVERIFY(ws.activateTab(0));
    QVERIFY(paint(ws, {2, 2}, kGreen));
    ws.setRecoveryStorageProbeForTests([] { return qint64(700) * 1024; });
    messages.clear();
    QVERIFY(!ws.persistWorkspace());
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages.first().contains(QStringLiteral("Large")));
    QVERIFY(messages.first().contains(QStringLiteral("disk space")));
    QVERIFY(!messages.first().contains(QStringLiteral("\"Small\"")));
    // Large keeps its previous valid snapshot, byte for byte.
    QCOMPARE(snapshotOf(recovery, QStringLiteral("Large")), largeBefore);
    QCOMPARE(fileBytes(largeBefore), largeBytes);
    // Small was updated independently.
    const QString smallAfter = snapshotOf(recovery, QStringLiteral("Small"));
    QVERIFY(smallAfter != smallBefore);
    QVERIFY(!QFile::exists(smallBefore));
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    // Nothing in memory was harmed.
    QCOMPARE(pixel(ws, {2, 2}), kGreen);
    QVERIFY(ws.activateTab(1));
    QCOMPARE(pixel(ws, {700, 700}), kGreen);
    QVERIFY(ws.history()->canUndo());

    // No room at all: nothing is written, nothing is lost.
    // (The index may be rewritten to note which project is active, but the
    // project entries and the snapshots they point at must be identical.)
    const QJsonArray entriesBefore = indexProjects(recovery);
    const QStringList filesBefore = snapshotFiles(recovery);
    ws.setRecoveryStorageProbeForTests([] { return qint64(0); });
    QVERIFY(paint(ws, {800, 800}, kRed));
    QVERIFY(!ws.persistWorkspace());
    QCOMPARE(indexProjects(recovery), entriesBefore);
    QCOMPARE(snapshotFiles(recovery), filesBefore);
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    QCOMPARE(fileBytes(largeBefore), largeBytes);

    // Space returns: the next pass catches up for every document.
    ws.setRecoveryStorageProbeForTests({});
    QVERIFY(ws.persistWorkspace());
    QVERIFY(snapshotOf(recovery, QStringLiteral("Large")) != largeBefore);
    QVERIFY(!QFile::exists(largeBefore));
    std::unique_ptr<CanvasDocument> latest(DocumentFile::open(snapshotOf(recovery, QStringLiteral("Large"))));
    QVERIFY(latest);
    QCOMPARE(latest->layers()->layerAt(0)->tiles().pixel({800, 800}).value(), kRed);
}

void TestWorkspaceSession::unavailableRecoveryLocationFailsSafely()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    // The recovery "folder" is an ordinary file: it cannot be created.
    const QString blocked = dir.filePath(QStringLiteral("blocked"));
    QVERIFY(writeBytes(blocked, QByteArray("x")));
    WorkspaceState ws;
    QVERIFY(!ws.initializeRecovery(blocked));
    QVERIFY(!ws.initializeRecovery(QString()));
    QVERIFY(!ws.recoveryReady());
    QVERIFY(!ws.recoveryAvailable());
    QSignalSpy errors(&ws, &WorkspaceState::fileErrorOccurred);
    QVERIFY(ws.newProject(QStringLiteral("Work"), 60, 60, true, QStringLiteral("white"), 72));
    QVERIFY(paint(ws, {1, 1}, kRed));
    QVERIFY(!ws.persistWorkspace());
    QCOMPARE(errors.size(), 1);
    ws.setRecoveryInterval(5);
    QVERIFY(!ws.recoveryTimerActive());
    QVERIFY(!ws.restoreWorkspace());
    QVERIFY(!ws.discardRecovery());
    ws.finishShutdown();  // must not crash or block
    // Ordinary work and saving are unaffected.
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QVERIFY(ws.saveToPath(dir.filePath(QStringLiteral("work.koma"))));
    QCOMPARE(fileBytes(blocked), QByteArray("x"));

    // Only one running copy owns a recovery folder.
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    WorkspaceState first; QVERIFY(first.initializeRecovery(recovery));
    WorkspaceState second; QVERIFY(!second.initializeRecovery(recovery));
    QVERIFY(!second.recoveryReady());
}

// ---------------------------------------------------------------- D / E

void TestWorkspaceSession::shutdownKeepsSavedDropsDiscarded()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString session = dir.filePath(QStringLiteral("session.json"));
    const QString keepPath = dir.filePath(QStringLiteral("keep.koma"));
    const QString newPath = dir.filePath(QStringLiteral("new.koma"));
    QByteArray keepBytes;
    {
        WorkspaceState ws;
        QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.initializeSession(session));
        QVERIFY(ws.restoreSession());
        // Saved, then edited: the user will answer "Don't Save".
        QVERIFY(ws.newProject(QStringLiteral("Keep"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kRed));
        QVERIFY(ws.saveToPath(keepPath));
        keepBytes = fileBytes(keepPath);
        QVERIFY(paint(ws, {2, 2}, kGreen));
        const QString keepId = tabId(ws, ws.activeTab());
        // Never saved: the user will answer "Save".
        QVERIFY(ws.newProject(QStringLiteral("New"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kBlue));
        // Never saved: the user will answer "Don't Save".
        QVERIFY(ws.newProject(QStringLiteral("Scrap"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kWhite));
        const QString scrapId = tabId(ws, ws.activeTab());
        // Created and never touched: no question is asked about it.
        QVERIFY(ws.newProject(QStringLiteral("Blank"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(ws.persistWorkspace());
        QCOMPARE(snapshotFiles(recovery).size(), 4);

        // Cancel at this point means none of the following happens: every
        // tab, every unsaved change and every snapshot is still here.
        QCOMPARE(ws.projectTabs().size(), 4);
        QCOMPARE(indexProjects(recovery).size(), 4);

        // "Save" for New writes New only.
        QVERIFY(ws.activateTab(1));
        QVERIFY(ws.saveToPath(newPath));
        QCOMPARE(fileBytes(keepPath), keepBytes);
        QVERIFY(ws.activateTab(0));
        QVERIFY(ws.document()->isModified());  // Keep was not saved by New's Save
        // New's snapshot went away when its save succeeded; the undecided
        // projects still have theirs.
        QCOMPARE(snapshotFiles(recovery).size(), 3);
        QVERIFY(!snapshotOf(recovery, QStringLiteral("Keep")).isEmpty());
        QVERIFY(!snapshotOf(recovery, QStringLiteral("Scrap")).isEmpty());
        QVERIFY(snapshotOf(recovery, QStringLiteral("New")).isEmpty());

        // All decisions are made; only now is anything dropped.
        ws.finishShutdown({keepId, scrapId});
        QVERIFY(snapshotFiles(recovery).isEmpty());
        QCOMPARE(indexProjects(recovery).size(), 0);
        QCOMPARE(fileBytes(keepPath), keepBytes);  // "Don't Save" wrote nothing
    }
    WorkspaceState ws;
    QVERIFY(ws.initializeRecovery(recovery));
    QVERIFY(ws.initializeSession(session));
    QVERIFY(!ws.recoveryAvailable());  // a clean exit leaves nothing to recover
    QVERIFY(ws.restoreSession());
    // The two projects that live in files are back; the discarded and the
    // blank one are not.
    QCOMPARE(ws.projectTabs().size(), 2);
    QCOMPARE(tabTitle(ws, 0), QStringLiteral("Keep"));
    QCOMPARE(tabTitle(ws, 1), QStringLiteral("New"));
    QVERIFY(ws.activateTab(0));
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QCOMPARE(pixel(ws, {2, 2}), 0U);   // the edit the user chose not to save
    QVERIFY(!ws.document()->isModified());
    QVERIFY(ws.activateTab(1));
    QCOMPARE(pixel(ws, {1, 1}), kBlue);
}

void TestWorkspaceSession::sessionRestoresTabsActiveDocumentAndViews()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString session = dir.filePath(QStringLiteral("session.json"));
    const QStringList names = {QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")};
    {
        WorkspaceState ws;
        QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.initializeSession(session));
        // Nothing is written before the previous session has been looked at.
        QVERIFY(!ws.saveSession());
        QVERIFY(!QFile::exists(session));
        QVERIFY(ws.restoreSession());  // first launch: nothing to restore
        QCOMPARE(ws.projectTabs().size(), 1);
        const quint32 colors[] = {kRed, kGreen, kBlue};
        for (int i = 0; i < names.size(); ++i) {
            QVERIFY(ws.newProject(names.at(i), 80 + i * 10, 60, true, QStringLiteral("white"), 72));
            QVERIFY(paint(ws, {1, 1}, colors[i]));
            QVERIFY(ws.saveToPath(dir.filePath(names.at(i) + QStringLiteral(".koma"))));
        }
        QVERIFY(ws.activateTab(0));
        ws.setActiveTool(QStringLiteral("eraser"));
        ws.storeActiveView(250.0, 11.0, 12.0);
        QVERIFY(ws.activateTab(1));
        ws.setActiveTool(QStringLiteral("fill"));
        ws.storeActiveView(50.0, 21.0, 22.0);
        // Third is never given a view: it has not been looked at.
        ws.setSessionValue(QStringLiteral("panel/timeline"), true);
        ws.setSessionValue(QStringLiteral("panel/color"), false);
        ws.setSessionValue(QStringLiteral("layout/rightDockWidth"), 310);
        ws.finishShutdown();  // Second is the active project
    }
    // The session file is small bookkeeping, not artwork, and a clean exit
    // leaves no recovery snapshots at all.
    QVERIFY(QFile::exists(session));
    QVERIFY(QFileInfo(session).size() < 4096);
    QVERIFY(snapshotFiles(recovery).isEmpty());

    WorkspaceState ws;
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    QVERIFY(ws.initializeRecovery(recovery));
    QVERIFY(ws.initializeSession(session));
    QCOMPARE(ws.projectTabs().size(), 1);  // nothing happens until asked
    QVERIFY(ws.restoreSession());
    QVERIFY(messages.isEmpty());
    QVERIFY(!ws.restoreSession());  // once only
    QCOMPARE(ws.projectTabs().size(), 3);
    for (int i = 0; i < names.size(); ++i) QCOMPARE(tabTitle(ws, i), names.at(i));
    // The active project and its own tool, zoom and position.
    QCOMPARE(ws.activeTab(), 1);
    QCOMPARE(ws.currentFile(), dir.filePath(QStringLiteral("Second.koma")));
    QCOMPARE(ws.activeTool(), QStringLiteral("fill"));
    QCOMPARE(ws.zoom(), 50.0);
    QVERIFY(ws.activeViewStored());
    QCOMPARE(ws.activeViewCenter(), QPointF(21.0, 22.0));
    QCOMPARE(pixel(ws, {1, 1}), kGreen);
    QVERIFY(!ws.document()->isModified());
    QVERIFY(!ws.history()->canUndo());  // undo history is never part of a session
    QVERIFY(ws.activateTab(0));
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QCOMPARE(ws.zoom(), 250.0);
    QCOMPARE(ws.activeViewCenter(), QPointF(11.0, 12.0));
    QCOMPARE(ws.document()->size(), QSize(80, 60));
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    QVERIFY(ws.activateTab(2));
    QVERIFY(!ws.activeViewStored());  // will be fitted when first shown
    QCOMPARE(ws.document()->size(), QSize(100, 60));
    QCOMPARE(pixel(ws, {1, 1}), kBlue);
    // Window layout.
    QCOMPARE(ws.sessionValue(QStringLiteral("panel/timeline")).toBool(), true);
    QCOMPARE(ws.sessionValue(QStringLiteral("panel/color"), true).toBool(), false);
    QCOMPARE(ws.sessionValue(QStringLiteral("layout/rightDockWidth")).toInt(), 310);
    QVERIFY(!ws.sessionValue(QStringLiteral("panel/layers")).isValid());
    QCOMPARE(ws.sessionValue(QStringLiteral("panel/layers"), true).toBool(), true);
    // Tabs keep their identity across the restart, so recovery data written
    // later still belongs to the right project.
    SessionData stored;
    QCOMPARE(SessionStore::load(session, &stored), SessionStore::LoadResult::Loaded);
    QCOMPARE(stored.documents.size(), std::size_t(3));
    for (int i = 0; i < 3; ++i) QCOMPARE(stored.documents[static_cast<std::size_t>(i)].id, tabId(ws, i));
}

void TestWorkspaceSession::sessionReopenFailureIsReportedAndKeepsRecoveryData()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString recovery = dir.filePath(QStringLiteral("recovery"));
    const QString session = dir.filePath(QStringLiteral("session.json"));
    const QString fine = dir.filePath(QStringLiteral("fine.koma"));
    const QString lost = dir.filePath(QStringLiteral("lost.koma"));
    const QString broken = dir.filePath(QStringLiteral("broken.koma"));
    const QString edited = dir.filePath(QStringLiteral("edited.koma"));
    {
        WorkspaceState ws;
        QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.initializeSession(session));
        QVERIFY(ws.restoreSession());
        for (const auto& path : {lost, fine, broken, edited}) {
            QVERIFY(ws.newProject(QFileInfo(path).completeBaseName(), 60, 60, true, QStringLiteral("white"), 72));
            QVERIFY(paint(ws, {1, 1}, kRed));
            QVERIFY(ws.saveToPath(path));
        }
        QVERIFY(paint(ws, {2, 2}, kGreen));  // "edited" has unsaved work
        QVERIFY(ws.newProject(QStringLiteral("Unsaved"), 60, 60, true, QStringLiteral("white"), 72));
        QVERIFY(paint(ws, {1, 1}, kBlue));
        QVERIFY(ws.persistWorkspace());
        // crash
    }
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    QVERIFY(QFile::remove(lost));
    QVERIFY(writeBytes(broken, fileBytes(broken).left(30)));
    const QByteArray indexBytes = fileBytes(recovery + QStringLiteral("/workspace.json"));

    WorkspaceState ws;
    QStringList messages;
    connect(&ws, &WorkspaceState::fileErrorOccurred, &ws, [&messages](const QString& m) { messages << m; });
    QVERIFY(ws.initializeRecovery(recovery));
    QVERIFY(ws.initializeSession(session));
    QVERIFY(!ws.restoreSession());
    // One message names both files that could not be reopened.
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages.first().contains(QStringLiteral("lost.koma")));
    QVERIFY(messages.first().contains(QStringLiteral("broken.koma")));
    QVERIFY(!messages.first().contains(QStringLiteral("fine.koma")));
    // The healthy project opened; the failures did not stop it.
    QCOMPARE(ws.projectTabs().size(), 1);
    QCOMPARE(ws.currentFile(), fine);
    QCOMPARE(pixel(ws, {1, 1}), kRed);
    // The edited project was left to recovery rather than silently reopened
    // from its older file, and all recovery data is exactly as it was.
    QCOMPARE(ws.recoveryEntries().size(), 2);
    QCOMPARE(snapshotFiles(recovery).size(), 2);
    QCOMPARE(fileBytes(recovery + QStringLiteral("/workspace.json")), indexBytes);
    QCOMPARE(fileBytes(broken).size(), qsizetype(30));  // the damaged file was not touched
    QVERIFY(ws.restoreWorkspace());
    QCOMPARE(ws.projectTabs().size(), 3);
    QVERIFY(ws.activateTab(1));
    QCOMPARE(ws.currentFile(), edited);
    QCOMPARE(pixel(ws, {2, 2}), kGreen);
    QVERIFY(ws.document()->isModified());
}

void TestWorkspaceSession::sessionFileIsValidatedAndReplacedAtomically()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.json"));
    SessionData data;
    QCOMPARE(SessionStore::load(path, &data), SessionStore::LoadResult::Missing);
    data.activeId = QStringLiteral("b");
    SessionDocument one;
    one.id = QStringLiteral("a"); one.path = QStringLiteral("/tmp/one.koma"); one.title = QStringLiteral("One");
    one.tool = QStringLiteral("eraser"); one.zoom = 250.0; one.hasView = true; one.center = QPointF(1.5, -2.5);
    SessionDocument two;
    two.id = QStringLiteral("b"); two.path = QStringLiteral("/tmp/two.koma");
    data.documents = {one, two};
    data.ui.insert(QStringLiteral("panel/layers"), false);
    QString error;
    QVERIFY2(SessionStore::save(path, data, &error), qPrintable(error));
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);  // no temporary file left

    SessionData loaded;
    QCOMPARE(SessionStore::load(path, &loaded, &error), SessionStore::LoadResult::Loaded);
    QCOMPARE(loaded.activeId, QStringLiteral("b"));
    QCOMPARE(loaded.documents.size(), std::size_t(2));
    QCOMPARE(loaded.documents[0].tool, QStringLiteral("eraser"));
    QCOMPARE(loaded.documents[0].zoom, 250.0);
    QVERIFY(loaded.documents[0].hasView);
    QCOMPARE(loaded.documents[0].center, QPointF(1.5, -2.5));
    QVERIFY(!loaded.documents[1].hasView);
    QCOMPARE(loaded.ui.value(QStringLiteral("panel/layers")).toBool(), false);

    // Truncated, altered, foreign and too-new files are all refused, and a
    // refused file is left exactly as it is.
    const QByteArray good = fileBytes(path);
    auto refused = [&](const QByteArray& bytes) {
        if (!writeBytes(path, bytes)) return false;
        SessionData ignored;
        ignored.activeId = QStringLiteral("untouched");
        return SessionStore::load(path, &ignored, &error) == SessionStore::LoadResult::Invalid
            && ignored.activeId == QStringLiteral("untouched") && fileBytes(path) == bytes;
    };
    QVERIFY(refused(good.left(good.size() / 2)));
    QVERIFY(refused(QByteArray()));
    QByteArray altered = good;
    altered.replace("\"One\"", "\"Two\"");
    QVERIFY(altered != good);
    QVERIFY(refused(altered));
    auto resign = [](QJsonObject root) {
        root.remove(QStringLiteral("sha256"));
        root.insert(QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(
            QJsonDocument(root).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    };
    auto root = QJsonDocument::fromJson(good).object();
    root.insert(QStringLiteral("version"), SessionStore::FormatVersion + 1);
    QVERIFY(refused(resign(root)));
    root = QJsonDocument::fromJson(good).object();
    root.insert(QStringLiteral("format"), QStringLiteral("something-else"));
    QVERIFY(refused(resign(root)));

    // A failed save leaves the previous session file in place.
    QVERIFY(writeBytes(path, good));
    const QString blocked = dir.filePath(QStringLiteral("blocked"));
    QVERIFY(writeBytes(blocked, QByteArray("x")));
    QVERIFY(!SessionStore::save(blocked + QStringLiteral("/session.json"), data, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!SessionStore::save(QString(), data, &error));
    QCOMPARE(fileBytes(path), good);

    // A workspace given a damaged session starts empty instead of failing,
    // and replaces the damaged file at its first save.
    QVERIFY(writeBytes(path, good.left(good.size() / 2)));
    WorkspaceState ws;
    QVERIFY(ws.initializeSession(path));
    QVERIFY(ws.restoreSession());
    QCOMPARE(ws.projectTabs().size(), 1);
    QCOMPARE(SessionStore::load(path, &loaded, &error), SessionStore::LoadResult::Loaded);
    QCOMPARE(loaded.documents.size(), std::size_t(0));
}

QTEST_GUILESS_MAIN(TestWorkspaceSession)
#include "TestWorkspaceSession.moc"
