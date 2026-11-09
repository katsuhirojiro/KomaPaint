#include <QtTest/QtTest>
#include "app/WorkspaceState.h"

using namespace komapaint;

class TestDocumentCreation : public QObject
{
    Q_OBJECT
private slots:
    void createsIndependentDocument();
    void rejectsInvalidInputWithoutChangingState();
};

void TestDocumentCreation::createsIndependentDocument()
{
    WorkspaceState workspace;
    auto* old = workspace.document();
    workspace.layers()->addLayer();
    workspace.history()->push("test");
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

QTEST_APPLESS_MAIN(TestDocumentCreation)
#include "TestDocumentCreation.moc"
