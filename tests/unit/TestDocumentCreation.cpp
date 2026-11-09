#include <QtTest/QtTest>
#include "app/WorkspaceState.h"

using namespace komapaint;

class TestDocumentCreation : public QObject
{
    Q_OBJECT
private slots:
    void createsIndependentDocument();
    void rejectsInvalidInputWithoutChangingState();
    void layerStorageOwnership();
};

void TestDocumentCreation::createsIndependentDocument()
{
    WorkspaceState workspace;
    auto* old = workspace.document();
    workspace.layers()->addLayer();
    QVERIFY(workspace.history()->beginStroke(workspace.layers()->layerAt(0)));
    workspace.layers()->layerAt(0)->tiles().setPixel({0, 0}, 0xffffffffU);
    QVERIFY(workspace.history()->commitStroke());
    QSignalSpy changed(&workspace, &WorkspaceState::documentChanged);
    QVERIFY(workspace.newDocument(321, 654, false, QColor("#123456")));
    QCOMPARE(changed.count(), 1);
    QVERIFY(workspace.document() != old);
    QCOMPARE(workspace.document()->size(), QSize(321, 654));
    QCOMPARE(workspace.document()->backgroundColor(), QColor("#123456"));
    QVERIFY(!workspace.document()->hasTransparentBackground());
    QCOMPARE(workspace.layers(), workspace.document()->layers());
    QCOMPARE(workspace.layers()->rowCount(), 1);
    QVERIFY(!workspace.history()->canUndo());
    QVERIFY(workspace.newDocument(1, CanvasDocument::MaxDimension, true, Qt::white));
    QVERIFY(workspace.document()->hasTransparentBackground());
}

void TestDocumentCreation::rejectsInvalidInputWithoutChangingState()
{
    WorkspaceState workspace;
    auto* original = workspace.document();
    QSignalSpy changed(&workspace, &WorkspaceState::documentChanged);
    QVERIFY(!workspace.newDocument(0, 10, true, Qt::white));
    QVERIFY(!workspace.newDocument(10, -1, true, Qt::white));
    QVERIFY(!workspace.newDocument(CanvasDocument::MaxDimension + 1, 1, true, Qt::white));
    QVERIFY(!workspace.newDocument(1, 1, false, QColor()));
    QVERIFY(!workspace.newDocument(1, 1, false, QColor(0, 0, 0, 0)));
    QCOMPARE(workspace.document(), original);
    QCOMPARE(changed.count(), 0);
}

void TestDocumentCreation::layerStorageOwnership()
{
    WorkspaceState workspace;
    QVERIFY(workspace.newDocument(300, 200, true, Qt::white));
    auto* layer = workspace.layers()->layerAt(0);
    QCOMPARE(layer->tiles().setPixel({299, 199}, 0xffffffffU), SparseTileStore::WriteResult::Changed);
    QVERIFY(!layer->tiles().pixel({300, 0}));
    workspace.layers()->addLayer();
    QCOMPARE(workspace.layers()->layerAt(1)->tiles().pixel({299, 199}).value(), 0U);
    QVERIFY(!workspace.layers()->layerAt(1)->tiles().pixel({300, 0}));
    workspace.document()->setWidth(100); // resize must not discard existing artwork
    QCOMPARE(workspace.document()->width(), 300);
    QVERIFY(workspace.newDocument(20, 10, false, Qt::white));
    QCOMPARE(workspace.layers()->rowCount(), 1);
    QCOMPARE(workspace.layers()->layerAt(0)->tiles().allocatedTiles(), size_t(0));
    QVERIFY(!workspace.layers()->layerAt(0)->tiles().pixel({20, 0}));
}

QTEST_APPLESS_MAIN(TestDocumentCreation)
#include "TestDocumentCreation.moc"
