#include <QtTest/QtTest>

#include "core/NewProjectSettings.h"
#include "app/WorkspaceState.h"

using namespace komapaint;

class TestNewProject : public QObject
{
    Q_OBJECT
private slots:
    void defaultPresetIsValid();
    void unitConversionRoundTrip();
    void repeatedUnitSwitchingDoesNotDrift();
    void dpiCalculation();
    void validationRejectsInvalidInput();
    void whiteAndTransparentBackgrounds();
    void creatingProjectReplacesCurrentProject();
    void creatingProjectWhileAnotherOpenClearsHistory();
};

void TestNewProject::defaultPresetIsValid()
{
    NewProjectSettings settings;
    QVERIFY(settings.isValid());
    QCOMPARE(settings.units(), QString("px"));
    QCOMPARE(settings.mode(), QString("Illustration"));
    QVERIFY(settings.transparentBackground());
    QCOMPARE(settings.backgroundColor(), QString("#FFFFFF"));
}

void TestNewProject::unitConversionRoundTrip()
{
    NewProjectSettings settings;
    settings.setUnits("inches");
    // 8.5 in at 300 dpi -> 2550 px
    settings.setSizeInUnit("inches", 8.5, 11.0);
    settings.setDpi(300.0);
    QCOMPARE(settings.widthPixels(), 2550);
    QCOMPARE(settings.heightPixels(), 3300);
    QCOMPARE(settings.widthInUnit("inches"), 8.5);
    QCOMPARE(settings.heightInUnit("inches"), 11.0);

    // Same physical size expressed in mm: 8.5 in = 215.9 mm
    settings.setUnits("mm");
    QVERIFY(qFuzzyCompare(settings.widthInUnit("mm"), 215.9));
    QVERIFY(qFuzzyCompare(settings.heightInUnit("mm"), 279.4));
    // Pixels must not change when switching units.
    QCOMPARE(settings.widthPixels(), 2550);
    QCOMPARE(settings.heightPixels(), 3300);
}

void TestNewProject::repeatedUnitSwitchingDoesNotDrift()
{
    NewProjectSettings settings;
    settings.setDpi(72.0);
    // 6 x 4 inches at 72 dpi = 432 x 288 px exactly.
    settings.setSizeInUnit("inches", 6.0, 4.0);
    QCOMPARE(settings.widthPixels(), 432);
    QCOMPARE(settings.heightPixels(), 288);

    // Switch units back and forth many times; physical size must be stable.
    for (int i = 0; i < 50; ++i) {
        settings.setUnits("px");
        settings.setUnits("mm");
        settings.setUnits("cm");
        settings.setUnits("inches");
    }
    QVERIFY(qFuzzyCompare(settings.widthInUnit("inches"), 6.0));
    QVERIFY(qFuzzyCompare(settings.heightInUnit("inches"), 4.0));
    QCOMPARE(settings.widthPixels(), 432);
    QCOMPARE(settings.heightPixels(), 288);

    // Round-trip through cm: 6 in = 15.24 cm, 4 in = 10.16 cm.
    settings.setSizeInUnit("cm", 15.24, 10.16);
    QVERIFY(qFuzzyCompare(settings.widthInUnit("inches"), 6.0));
    QVERIFY(qFuzzyCompare(settings.heightInUnit("inches"), 4.0));
}

void TestNewProject::dpiCalculation()
{
    NewProjectSettings settings;
    settings.setSizeInUnit("inches", 2.0, 2.0);
    settings.setDpi(72.0);
    QCOMPARE(settings.widthPixels(), 144);
    QCOMPARE(settings.heightPixels(), 144);

    settings.setDpi(150.0);
    QCOMPARE(settings.widthPixels(), 300);
    QCOMPARE(settings.heightPixels(), 300);
}

void TestNewProject::validationRejectsInvalidInput()
{
    NewProjectSettings settings;
    // Empty name is invalid.
    settings.setName("");
    QVERIFY(!settings.isValid());
    QVERIFY(!settings.validationError().isEmpty());

    // Zero dimensions are rejected without changing valid settings.
    settings.setName("Valid");
    QVERIFY(settings.isValid());
    settings.setSizeInUnit("px", 0, 100);
    QVERIFY(settings.isValid()); // invalid input is ignored, settings unchanged
    QCOMPARE(settings.widthInches(), 8.5);
    QCOMPARE(settings.widthPixels(), 2550);

    // Negative DPI is invalid.
    settings.setSizeInUnit("px", 100, 100);
    settings.setDpi(-72.0);
    QVERIFY(!settings.isValid());

    // Oversized dimensions are invalid.
    settings.setDpi(72.0);
    settings.setSizeInUnit("px", NewProjectSettings::MaxDimension + 1, 100);
    QVERIFY(!settings.isValid());
}

void TestNewProject::whiteAndTransparentBackgrounds()
{
    NewProjectSettings settings;
    settings.setTransparentBackground(true);
    QVERIFY(settings.transparentBackground());
    QCOMPARE(settings.backgroundColor(), QString("#FFFFFF"));

    settings.setTransparentBackground(false);
    settings.setBackgroundColor("#123456");
    QVERIFY(!settings.transparentBackground());
    QCOMPARE(settings.backgroundColor(), QString("#123456"));

    // Transparent background keeps an opaque background colour on record.
    settings.setTransparentBackground(true);
    QCOMPARE(settings.backgroundColor(), QString("#123456"));
}

void TestNewProject::creatingProjectReplacesCurrentProject()
{
    WorkspaceState workspace;
    // Draw something on the current document.
    QVERIFY(workspace.newDocument(300, 200, true, QColor("white")));
    auto* original = workspace.document();
    workspace.layers()->addLayer();

    QSignalSpy changed(&workspace, &WorkspaceState::documentChanged);
    QVERIFY(workspace.newProject("My Illustration", 800, 600, false, "#123456", 150.0));
    QCOMPARE(changed.count(), 1);
    QVERIFY(workspace.document() != original);
    QCOMPARE(workspace.document()->title(), QString("My Illustration"));
    QCOMPARE(workspace.document()->size(), QSize(800, 600));
    QCOMPARE(workspace.document()->dpi(), 150.0);
    QVERIFY(!workspace.document()->hasTransparentBackground());
    QCOMPARE(workspace.document()->backgroundColor(), QColor("#123456"));
    // Fresh document: one layer, no history.
    QCOMPARE(workspace.layers()->rowCount(), 1);
    QVERIFY(!workspace.history()->canUndo());
    QVERIFY(workspace.currentFile().isEmpty());
}

void TestNewProject::creatingProjectWhileAnotherOpenClearsHistory()
{
    WorkspaceState workspace;
    QVERIFY(workspace.newDocument(100, 100, true, QColor("white")));
    workspace.layers()->addLayer();
    QVERIFY(workspace.history()->beginStroke(workspace.layers()->layerAt(0)));
    workspace.layers()->layerAt(0)->tiles().setPixel({0, 0}, 0xffffffffU);
    QVERIFY(workspace.history()->commitStroke());
    QVERIFY(workspace.history()->canUndo());

    QVERIFY(workspace.newProject("Second", 200, 200, true, "white", 72.0));
    QVERIFY(!workspace.history()->canUndo());
    QVERIFY(!workspace.history()->canRedo());
    QCOMPARE(workspace.document()->title(), QString("Second"));
}

QTEST_APPLESS_MAIN(TestNewProject)
#include "TestNewProject.moc"
