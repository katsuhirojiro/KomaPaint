#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <functional>
#include "app/DocumentRecovery.h"
#include "app/WorkspaceState.h"
#include "files/DocumentFile.h"
#include "layers/Layer.h"
using namespace komapaint;

class TestWorkspaceRecovery : public QObject {
    Q_OBJECT
private slots:
    void restartAndIsolation();
    void failureAndCorruption();
    void documentTypesAndRegistry();
    void unknownTypeIsRejectedAndRetained();
    void cleanupOnlyAfterValidIndex();
    void recoveryIntervalScheduling();
};

static bool paint(WorkspaceState& ws, quint32 color)
{
    auto* layer = ws.layers()->layerAt(0);
    if (!ws.history()->beginStroke(layer)) return false;
    if (layer->tiles().setPixel({2, 3}, color) != SparseTileStore::WriteResult::Changed) return false;
    return ws.history()->commitStroke();
}
void TestWorkspaceRecovery::restartAndIsolation()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto recovery = dir.filePath("recovery");
    const auto original = dir.filePath("original.koma");
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject("First", 100, 100, true, "white", 72));
        QVERIFY(paint(ws, 0xffff0000U));
        QVERIFY(ws.saveToPath(original));
        QVERIFY(paint(ws, 0xff00ff00U));
        auto* first = ws.document(); const int firstTab = ws.activeTab();
        QVERIFY(ws.newProject("Unsaved", 90, 80, true, "white", 144));
        QVERIFY(paint(ws, 0xff0000ffU));
        ws.layers()->addLayer(); ws.layers()->layerAt(1)->setVisible(false);
        ws.layers()->layerAt(1)->setOpacity(0.42);
        ws.setActiveTool("eraser"); ws.setZoom(250.0);
        QVERIFY(ws.persistWorkspace());
        QVERIFY(ws.activateTab(firstTab)); QCOMPARE(ws.document(), first);
        QCOMPARE(ws.layers()->layerAt(0)->tiles().pixel({2, 3}).value(), 0xff00ff00U);
        QVERIFY(ws.history()->canUndo());
    }
    {
        QString error;
        std::unique_ptr<CanvasDocument> saved(DocumentFile::open(original, &error)); QVERIFY2(saved, qPrintable(error));
        QCOMPARE(saved->layers()->layerAt(0)->tiles().pixel({2, 3}).value(), 0xffff0000U);
    }
    {
        WorkspaceState reopened; QVERIFY(reopened.initializeRecovery(recovery)); QVERIFY(reopened.recoveryAvailable());
        QVERIFY(reopened.restoreWorkspace());
        QCOMPARE(reopened.projectTabs().size(), 2);
        QCOMPARE(reopened.document()->title(), QStringLiteral("Unsaved"));
        QVERIFY(reopened.currentFile().isEmpty());
        QVERIFY(reopened.document()->isModified());
        QCOMPARE(reopened.layers()->rowCount(), 2);
        QVERIFY(!reopened.layers()->layerAt(1)->isVisible());
        QCOMPARE(reopened.layers()->layerAt(1)->opacity(), 0.42);
        QCOMPARE(reopened.activeTool(), QStringLiteral("eraser"));
        QCOMPARE(reopened.zoom(), 250.0);
        QCOMPARE(reopened.layers()->layerAt(0)->tiles().pixel({2, 3}).value(), 0xff0000ffU);
        QVERIFY(!reopened.history()->canUndo());
        QVERIFY(reopened.activateTab(0));
        QCOMPARE(reopened.currentFile(), original);
        QCOMPARE(reopened.layers()->layerAt(0)->tiles().pixel({2, 3}).value(), 0xff00ff00U);
        QVERIFY(reopened.document()->isModified());
        QVERIFY(!reopened.history()->canUndo());
        QVERIFY(paint(reopened, 0xffffffffU));
        reopened.history()->undo();
        QCOMPARE(reopened.layers()->layerAt(0)->tiles().pixel({2, 3}).value(), 0xff00ff00U);
    }
}
void TestWorkspaceRecovery::failureAndCorruption()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto recovery = dir.filePath("recovery");
    QByteArray initial;
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject("First", 100, 100, true, "white", 72));
        QVERIFY(paint(ws, 0xffff0000U)); QVERIFY(ws.persistWorkspace());
        QFile index(recovery + "/workspace.json"); QVERIFY(index.open(QIODevice::ReadOnly)); initial = index.readAll(); index.close();
        QVERIFY(paint(ws, 0xff00ff00U));
        for (int stage : {1, 2}) {
            ws.setRecoveryFailureForTests(stage); QVERIFY(!ws.persistWorkspace());
            QVERIFY(index.open(QIODevice::ReadOnly)); QCOMPARE(index.readAll(), initial); index.close();
            QCOMPARE(ws.layers()->layerAt(0)->tiles().pixel({2, 3}).value(), 0xff00ff00U);
        }
    }
    const auto entry = QJsonDocument::fromJson(initial).object().value("projects").toArray().first().toObject();
    QFile snapshot(recovery + "/" + entry.value("snapshot").toString());
    QVERIFY(snapshot.open(QIODevice::WriteOnly | QIODevice::Truncate)); QCOMPARE(snapshot.write("bad"), qint64(3)); snapshot.close();
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        auto* before = ws.document();
        QSignalSpy error(&ws, &WorkspaceState::fileErrorOccurred);
        QVERIFY(!ws.restoreWorkspace()); QCOMPARE(ws.document(), before); QVERIFY(ws.recoveryAvailable()); QVERIFY(!error.isEmpty());
        QFile index(recovery + "/workspace.json"); QVERIFY(index.open(QIODevice::ReadOnly)); QCOMPARE(index.readAll(), initial);
    }
}
namespace {
// Deliberately a separate native type: a future animation/comic adapter can
// register itself the same way without WorkspaceState knowing about it.
class SecondTypeAdapter final : public DocumentRecoveryAdapter
{
public:
    QString typeId() const override { return QStringLiteral("test-second-type"); }
    QString displayName() const override { return QStringLiteral("Test second type"); }
    bool supports(const CanvasDocument& document) const override
    {
        return document.documentType() == typeId();
    }
    bool save(const CanvasDocument& document, const QString& path, QString* error) const override
    {
        return DocumentFile::save(document, path, error);
    }
    CanvasDocument* open(const QString& path, QString* error) const override
    {
        auto* document = DocumentFile::open(path, error);
        if (document) document->setDocumentType(typeId());
        return document;
    }
};

bool rewriteIndex(const QString& path, const std::function<void(QJsonObject&)>& edit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root.remove(QStringLiteral("sha256"));
    edit(root);
    root.insert(QStringLiteral("sha256"),
                QString::fromLatin1(QCryptographicHash::hash(
                    QJsonDocument(root).toJson(QJsonDocument::Compact),
                    QCryptographicHash::Sha256).toHex()));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) > 0;
}
}  // namespace

void TestWorkspaceRecovery::documentTypesAndRegistry()
{
    auto& registry = DocumentRecoveryRegistry::instance();
    QVERIFY(registry.adapterForType(DocumentRecoveryRegistry::illustrationTypeId()));
    QVERIFY(registry.typeIds().contains(QStringLiteral("illustration")));

    CanvasDocument illustration;
    QCOMPARE(illustration.documentType(), QStringLiteral("illustration"));
    const auto* adapter = registry.adapterForDocument(illustration);
    QVERIFY(adapter);
    QCOMPARE(adapter->typeId(), registry.illustrationTypeId());

    CanvasDocument unknown;
    unknown.setDocumentType(QStringLiteral("not-a-real-type"));
    QVERIFY(!registry.adapterForDocument(unknown));

    auto second = std::make_unique<SecondTypeAdapter>();
    QVERIFY(registry.registerAdapter(std::move(second)));
    QVERIFY(!registry.registerAdapter(std::make_unique<SecondTypeAdapter>()));  // duplicate
    CanvasDocument future;
    future.setDocumentType(QStringLiteral("test-second-type"));
    QVERIFY(registry.adapterForDocument(future));
    QCOMPARE(registry.adapterForDocument(future)->displayName(), QStringLiteral("Test second type"));

    // The adapter owns serialization; the shared layer only stores its id.
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("future-snapshot.koma");
    QString error;
    QVERIFY2(registry.adapterForType(QStringLiteral("test-second-type"))->save(future, path, &error), qPrintable(error));
    std::unique_ptr<CanvasDocument> reopened(registry.adapterForType(QStringLiteral("test-second-type"))->open(path, &error));
    QVERIFY2(reopened, qPrintable(error));
    QCOMPARE(reopened->documentType(), QStringLiteral("test-second-type"));
}

void TestWorkspaceRecovery::unknownTypeIsRejectedAndRetained()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto recovery = dir.filePath("recovery");
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject("Typed", 40, 40, true, "white", 72));
        QVERIFY(paint(ws, 0xff112233U));
        QVERIFY(ws.persistWorkspace());
    }
    const auto indexPath = recovery + "/workspace.json";
    QFile index(indexPath); QVERIFY(index.open(QIODevice::ReadOnly));
    const auto originalBytes = index.readAll(); index.close();
    QFile snapshotFile(recovery + "/" + QJsonDocument::fromJson(originalBytes).object()
        .value("projects").toArray().first().toObject().value("snapshot").toString());
    QVERIFY(snapshotFile.exists());
    QVERIFY(rewriteIndex(indexPath, [](QJsonObject& root) {
        auto projects = root.value("projects").toArray();
        auto first = projects.first().toObject();
        first.insert("type", "unimplemented-animation-v9");
        projects.replace(0, first);
        root.insert("projects", projects);
    }));
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        auto* before = ws.document();
        QSignalSpy error(&ws, &WorkspaceState::fileErrorOccurred);
        QVERIFY(!ws.restoreWorkspace());
        QCOMPARE(ws.document(), before);
        QVERIFY(ws.recoveryAvailable());
        QVERIFY(!error.isEmpty());
        QVERIFY(snapshotFile.exists());  // never deleted because the type was unknown
        QFile retained(indexPath); QVERIFY(retained.open(QIODevice::ReadOnly));
        QVERIFY(retained.readAll() != originalBytes);  // the tampered index is retained, not replaced
    }
}

void TestWorkspaceRecovery::cleanupOnlyAfterValidIndex()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto recovery = dir.filePath("recovery");
    {
        WorkspaceState ws; QVERIFY(ws.initializeRecovery(recovery));
        QVERIFY(ws.newProject("One", 40, 40, true, "white", 72));
        QVERIFY(ws.persistWorkspace());
        QVERIFY(ws.newProject("Two", 40, 40, true, "white", 72));
        QVERIFY(ws.persistWorkspace());
        QVERIFY(ws.persistWorkspace());
        QDir recoveryDir(recovery);
        QCOMPARE(recoveryDir.entryList({"*.koma"}, QDir::Files).size(), 2);
        QCOMPARE(recoveryDir.entryList({"workspace.json"}, QDir::Files).size(), 1);
    }
    QDir recoveryDir(recovery);
    QVERIFY(QFile::copy(recoveryDir.filePath("workspace.json"), recoveryDir.filePath("orphan.koma")));
    {
        WorkspaceState reopened; QVERIFY(reopened.initializeRecovery(recovery));
        QCOMPARE(recoveryDir.entryList({"*.koma"}, QDir::Files).size(), 2);
        QVERIFY(!QFile::exists(recoveryDir.filePath("orphan.koma")));
        QVERIFY(reopened.restoreWorkspace());
        QCOMPARE(reopened.projectTabs().size(), 2);
        QCOMPARE(reopened.document()->documentType(), QStringLiteral("illustration"));
    }
}

void TestWorkspaceRecovery::recoveryIntervalScheduling()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    WorkspaceState ws; QVERIFY(ws.initializeRecovery(dir.filePath("recovery")));
    QVERIFY(!ws.recoveryTimerActive());
    for (int minutes : {5, 10, 15, 30, 60}) {
        ws.setRecoveryInterval(minutes);
        QVERIFY(ws.recoveryTimerActive());
    }
    ws.setRecoveryInterval(0);  // Never
    QVERIFY(!ws.recoveryTimerActive());
}

QTEST_GUILESS_MAIN(TestWorkspaceRecovery)
#include "TestWorkspaceRecovery.moc"
