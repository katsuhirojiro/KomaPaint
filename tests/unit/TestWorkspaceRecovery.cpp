#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "app/WorkspaceState.h"
#include "files/DocumentFile.h"
#include "layers/Layer.h"
using namespace komapaint;

class TestWorkspaceRecovery : public QObject {
    Q_OBJECT
private slots:
    void restartAndIsolation();
    void failureAndCorruption();
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
QTEST_APPLESS_MAIN(TestWorkspaceRecovery)
#include "TestWorkspaceRecovery.moc"
