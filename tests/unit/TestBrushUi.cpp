// Icons, tool palette and the Clip Studio-style brush panels, through the
// real MainWindow.qml with real mouse events. Every check that a control
// "works" looks at the brush settings the engine reads, or at the pixels a
// stroke leaves on the layer, never only at what the control displays.
// Runs on the offscreen platform with the software renderer.
//
// Set KOMAPAINT_SCREENSHOT_DIR to a folder to also save pictures of the
// window (and KOMAPAINT_TEST_FONT to a .ttf when the machine has no fonts).
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QSGRendererInterface>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>

#include "app/BrushLibraryStore.h"
#include "app/BrushSettings.h"
#include "app/CanvasViewport.h"
#include "app/DockModel.h"
#include "app/MixerSurfaceItem.h"
#include "app/PaintMixerController.h"
#include "app/PanelManager.h"
#include "app/SubToolLibrary.h"
#include "app/ToolSettings.h"
#include "app/WorkspaceState.h"
#include "canvas/CanvasDocument.h"
#include "color/ColorPalette.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "layers/Layer.h"
#include "layers/LayerModel.h"

using namespace komapaint;

namespace {

const QStringList kToolIcons = {
    "airbrush", "brush", "circle", "curveline", "eraser", "eyedropper", "fill", "gradient", "hand", "lasso", "line",
    "move", "pen", "pencil", "rectangle", "selection", "smudge", "text", "transform", "wand", "zoom"};
const QStringList kLayerIcons = {
    "adjustment-layer", "delete", "folder", "lock", "lock-transparency", "mask", "merge-down", "move", "new-raster",
    "new-vector", "reference", "visible"};
const QStringList kProjectIcons = {"animation", "comic-page", "illustration", "multi-page-comic"};

// QML warnings of the running window are collected here (see collectWarnings).
QStringList* g_warnings = nullptr;
QtMessageHandler g_previousHandler = nullptr;

struct Running {
    ~Running() { if (g_warnings == &warnings) g_warnings = nullptr; }
    QStringList warnings;       // declared first: destroyed last, after the window
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    PanelManager panels;
    std::unique_ptr<Settings> settings;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QQuickItem* canvasItem = nullptr;
    CanvasViewport* canvas = nullptr;
};

QQuickItem* findVisual(QQuickItem* from, const QString& name)
{
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = findVisual(child, name)) return found;
    return nullptr;
}

QQuickItem* item(Running& app, const QString& name) { return findVisual(app.window->contentItem(), name); }

QPoint centre(QQuickItem* target)
{
    return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
}

void click(Running& app, QQuickItem* target, Qt::MouseButton button = Qt::LeftButton)
{
    QTest::mouseMove(app.window, centre(target));
    QTest::mouseClick(app.window, button, Qt::NoModifier, centre(target));
    QCoreApplication::processEvents();
}

// Rows live inside clipped flickables: the tool palette, the sub tool list and
// the scroll view of the Tool Property panel. A click on a row that is scrolled
// out of view lands on whatever is behind the viewport, so bring the row into
// view first — the same thing a person does with the wheel. The nearest
// scrollable ancestor is found by duck typing, because a ListView, a Flickable
// and a ScrollView's content item all expose contentY/contentHeight while the
// plain contentItem node does not.
bool scrollIntoView(QQuickItem* target)
{
    for (auto* view = target->parentItem(); view; view = view->parentItem()) {
        const QVariant contentX = view->property("contentX");
        const QVariant contentWidth = view->property("contentWidth");
        const QVariant contentY = view->property("contentY");
        const QVariant contentHeight = view->property("contentHeight");
        if (!contentY.isValid() || !contentHeight.isValid()) continue;
        bool moved = false;
        if (contentX.isValid() && contentWidth.isValid() && contentWidth.toReal() > view->width()) {
            const qreal centreX = view->mapFromItem(target, QPointF(target->width() / 2, 0)).x();
            const qreal maxX = std::max(0.0, contentWidth.toReal() - view->width());
            const qreal nextX = std::clamp(contentX.toReal() + centreX - view->width() / 2, 0.0, maxX);
            if (qAbs(nextX - contentX.toReal()) >= 0.5) {
                view->setProperty("contentX", nextX);
                moved = true;
            }
        }
        const qreal centre = view->mapFromItem(target, QPointF(0, target->height() / 2)).y();
        const qreal maximum = std::max(0.0, contentHeight.toReal() - view->height());
        const qreal next = std::clamp(contentY.toReal() + centre - view->height() / 2, 0.0, maximum);
        if (qAbs(next - contentY.toReal()) >= 0.5) {
            view->setProperty("contentY", next);
            moved = true;
        }
        if (moved) QCoreApplication::processEvents();
        return true;
    }
    return false;
}

bool clickNamed(Running& app, const QString& name)
{
    auto* target = item(app, name);
    if (!target || target->width() <= 0 || !target->isVisible()) return false;
    scrollIntoView(target);
    click(app, target);
    return true;
}

bool chooseHardness(Running& app, int choice)
{
    auto* target = item(app, QStringLiteral("brushHardnessChoice"));
    auto* scroll = item(app, QStringLiteral("toolPropertyScroll"));
    if (!target || !scroll || choice < 0 || choice > 4) return false;
    const qreal y = target->mapToItem(scroll, QPointF(0, target->height() / 2)).y();
    const qreal maximum = std::max(0.0, scroll->property("contentHeight").toReal() - scroll->height());
    scroll->setProperty("contentY", std::clamp(scroll->property("contentY").toReal() + y - scroll->height() / 2, 0.0, maximum));
    QCoreApplication::processEvents();
    const QPoint at = target->mapToScene(QPointF(target->width() * (choice + 0.5) / 5.0, target->height() / 2)).toPoint();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, at);
    QCoreApplication::processEvents();
    return true;
}

// Click a PropertySlider's bar at `position` (0 = left end, 1 = right end).
bool setSlider(Running& app, const QString& name, double position)
{
    auto* slider = item(app, name);
    if (!slider || !slider->isVisible() || slider->width() <= 0) return false;
    auto* scroll = item(app, QStringLiteral("toolPropertyScroll"));
    if (scroll) {
        const qreal y = slider->mapToItem(scroll, QPointF(0, slider->height() / 2)).y();
        const qreal maximum = std::max(0.0, scroll->property("contentHeight").toReal() - scroll->height());
        scroll->setProperty("contentY", std::clamp(scroll->property("contentY").toReal() + y - scroll->height() / 2, 0.0, maximum));
        QCoreApplication::processEvents();
    }
    const QPointF local(1 + position * (slider->width() - 2), slider->height() - 7);
    const QPoint at = slider->mapToScene(local).toPoint();
    QTest::mouseMove(app.window, at);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, at);
    QCoreApplication::processEvents();
    return true;
}

QPoint onCanvas(Running& app, double docX, double docY)
{
    const QPointF view = app.canvas->documentToViewport(docX, docY);
    return app.canvasItem->mapToScene(view).toPoint();
}

void drag(Running& app, const QList<QPoint>& path)
{
    QTest::mouseMove(app.window, path.first());
    QTest::mousePress(app.window, Qt::LeftButton, Qt::NoModifier, path.first());
    for (int i = 1; i < path.size(); ++i) QTest::mouseMove(app.window, path.at(i), 2);
    QTest::mouseRelease(app.window, Qt::LeftButton, Qt::NoModifier, path.last());
    QCoreApplication::processEvents();
    QTest::qWait(20);     // the stroke is finished on the next event-loop turn
}

quint32 pixel(Running& app, int x, int y)
{
    auto* layer = app.workspace.layers()->layerAt(app.workspace.layers()->activeLayerIndex());
    return layer->tiles().pixel({x, y}).value();
}
int alpha(Running& app, int x, int y) { return static_cast<int>(pixel(app, x, y) >> 24); }

bool hasClass(QQuickItem* from, const char* fragment)
{
    if (!from) return false;
    if (QByteArray(from->metaObject()->className()).contains(fragment)) return true;
    for (auto* child : from->childItems())
        if (hasClass(child, fragment)) return true;
    return false;
}

// The KomaTheme singleton of this window's engine.
QObject* themeOf(Running& app)
{
    return app.engine.singletonInstance<QObject*>(QStringLiteral("themes"), QStringLiteral("KP"));
}

// The KpIcon inside the button called `button` whose artwork is `iconPath`
// (e.g. "tools/pen"). KpIcon names itself "kpIcon_<path>" / "kpArt_<path>".
QQuickItem* buttonIcon(Running& app, const QString& button, const QString& iconPath)
{
    auto* owner = item(app, button);
    if (!owner) return nullptr;
    if (auto* tinted = findVisual(owner, QStringLiteral("kpIcon_") + iconPath)) return tinted;
    return findVisual(owner, QStringLiteral("kpArt_") + iconPath);
}

// WCAG relative luminance / contrast ratio, for the icon-on-panel checks.
qreal luminance(const QColor& colour)
{
    const auto channel = [](qreal value) {
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF())
         + 0.0722 * channel(colour.blueF());
}

qreal contrast(const QColor& a, const QColor& b)
{
    const qreal la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor shownColour(QQuickItem* icon)
{
    return icon ? icon->property("color").value<QColor>() : QColor();
}

void collectWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // QML complaints about the files this test covers. (MainWindow.qml,
    // ColorPanel.qml and PropertiesPanel.qml have older, known warnings that
    // are not this test's subject.)
    static const QStringList covered = {
        QStringLiteral("ToolPalette.qml"), QStringLiteral("KpIcon.qml"), QStringLiteral("IconButton.qml"),
        QStringLiteral("PropertySlider.qml"), QStringLiteral("SegmentedChoice.qml"), QStringLiteral("SubToolPanel.qml"),
        QStringLiteral("ToolPropertyPanel.qml"), QStringLiteral("BrushSizePanel.qml"), QStringLiteral("LayersPanel.qml"),
        QStringLiteral("NewProjectDialog.qml"), QStringLiteral("CanvasWorkspace.qml"), QStringLiteral("MainToolbar.qml"),
        QStringLiteral("PreferencesDialog.qml"), QStringLiteral("KomaSettingsDialog.qml"), QStringLiteral("KomaTheme.qml"),
        QStringLiteral("ColorPanel.qml"), QStringLiteral("PaintMixerView.qml"),
        QStringLiteral("qrc:/icons/")};
    if (g_warnings && type == QtWarningMsg) {
        bool relevant = message.contains(QStringLiteral("ReferenceError")) || message.contains(QStringLiteral("TypeError"));
        for (const QString& file : covered) relevant = relevant || message.contains(file);
        if (relevant) *g_warnings << message;
    }
    if (g_previousHandler) g_previousHandler(type, context, message);
}

}  // namespace

class TestBrushUi : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void iconFilesAreRegisteredSizedAndCentred();
    void subToolLibraryLoadsRemembersAndResets();
    void paletteShowsEveryToolAndSelectsOnlyWorkingOnes();
    void brushSizeGridSetsTheRealSize();
    void toolPropertyControlsDriveTheBrush();
    void strokesFollowHardnessAndTheEraserErases();
    void stabilizationSmoothsAShakyLine();
    void subToolsLoadAndKeepTheirSettings();
    void viewToolsZoomPanAndPickColour();
    void layerIconsAct();
    void thumbnailWheelResizesWithoutChangingSelection();
    void colorSelectorShapeDefaultsFollowTheme();
    void spectralMixerProducesPaintLikeColours();
    void mixerMaterialMixtureLoadsIntoBrushWithoutLosingFractions();
    void colorPanelTabsPreserveWheelAndMixerState();
    void newProjectDialogShowsTheProjectTypeIcons();
    void iconsFollowTheTheme();
    void preferencesDialogStoresThemeAndIconColours();
    void komaSettingsApplyAndCancelPersistGlobalValues();
    void brushEngineSettingsPersistAcrossRestartsAndStayNonDestructive();
    void shortcutsAndPerformanceSettingsPersistAndResolveConflicts();
    void toolShortcutsCycleThroughTheirFamilies();
    void brushLibraryTabsAndPresetsPersistAcrossRestartsAndProjects();
    void brushLibraryMigrationPreservesUserCollectionsAndUpgradesBuiltIns();
    void brushPanelsCanBeClosedAndComeBack();
    void stage11ToolsAndOverlaysWorkEndToEnd();
private:
    bool start(Running& app, const QString& name);
    void screenshot(Running& app, const QString& name);
};

void TestBrushUi::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    qmlRegisterType<MixerSurfaceItem>("KomaPaint", 1, 0, "MixerSurfaceItem");
    g_previousHandler = qInstallMessageHandler(collectWarnings);
    const QByteArray font = qgetenv("KOMAPAINT_TEST_FONT");
    if (!font.isEmpty()) {
        const int id = QFontDatabase::addApplicationFont(QString::fromLocal8Bit(font));
        if (id >= 0) QGuiApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).value(0)));
    }
}

bool TestBrushUi::start(Running& app, const QString& name)
{
    app.settings = std::make_unique<Settings>(QStringLiteral("KomaPaintTests"), name);
    app.settings->setValue(QStringLiteral("recovery/configured"), true);
    app.settings->setValue(QStringLiteral("recovery/intervalMinutes"), 10);
    app.settings->setWindowMaximized(false);
    app.settings->setWindowSize(QSize(1320, 1000));
    app.workspace.bindAppSettings(app.settings.get());
    g_warnings = &app.warnings;
    app.engine.addImportPath(QStringLiteral("qrc:/"));
    auto* context = app.engine.rootContext();
    context->setContextProperty(QStringLiteral("Settings"), app.settings.get());
    context->setContextProperty(QStringLiteral("Workspace"), &app.workspace);
    context->setContextProperty(QStringLiteral("NewProject"), &app.project);
    context->setContextProperty(QStringLiteral("App"), &app.config);
    context->setContextProperty(QStringLiteral("Panels"), &app.panels);
    context->setContextProperty(QStringLiteral("Dock"), app.panels.dockModel());
    QQmlComponent component(&app.engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    app.root.reset(component.create());
    if (!app.root) { qWarning() << component.errorString(); return false; }
    app.window = qobject_cast<QQuickWindow*>(app.root.get());
    if (!app.window) return false;
    app.window->show();
    if (!QTest::qWaitForWindowExposed(app.window)) return false;
    QTest::qWait(50);
    // The canvas lives inside the docking workspace, so it is found through
    // the visual tree rather than through QObject parents.
    app.canvas = qobject_cast<CanvasViewport*>(item(app, QStringLiteral("canvasViewport")));
    app.canvasItem = app.canvas;
    if (!app.canvas) return false;
    // A known document: 800 x 600, white paper, shown completely.
    if (!app.workspace.newProject(QStringLiteral("Brush test"), 800, 600, false, QStringLiteral("white"), 72)) return false;
    QTest::qWait(50);
    app.canvas->refreshNow();
    return app.canvas->document() == static_cast<QObject*>(app.workspace.document());
}

void TestBrushUi::screenshot(Running& app, const QString& name)
{
    const QString dir = QString::fromLocal8Bit(qgetenv("KOMAPAINT_SCREENSHOT_DIR"));
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    QTest::qWait(60);
    app.window->grabWindow().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

// ------------------------------------------------------------------ assets
void TestBrushUi::iconFilesAreRegisteredSizedAndCentred()
{
    struct Category { QString name; QStringList files; int canvas; };
    const QList<Category> categories = {
        {QStringLiteral("tools"), kToolIcons, 128},
        {QStringLiteral("layers"), kLayerIcons, 128},
        {QStringLiteral("project-types"), kProjectIcons, 256}};
    int checked = 0;
    for (const auto& category : categories) {
        for (const auto& file : category.files) {
            const QString path = QStringLiteral(":/icons/%1/%2.png").arg(category.name, file);
            QVERIFY2(QFile::exists(path), qPrintable(path));
            const QImage image = QImage(path).convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!image.isNull(), qPrintable(path));
            QCOMPARE(image.size(), QSize(category.canvas, category.canvas));
            // Artwork bounds from the alpha channel, and one flat colour.
            int left = image.width(), top = image.height(), right = -1, bottom = -1;
            QRgb ink = 0; bool oneColour = true, haveInk = false;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QRgb p = image.pixel(x, y);
                    if (qAlpha(p) <= 16) continue;
                    left = qMin(left, x); right = qMax(right, x); top = qMin(top, y); bottom = qMax(bottom, y);
                    if (!haveInk) { ink = p & 0x00ffffffU; haveInk = true; }
                    else if ((p & 0x00ffffffU) != ink) oneColour = false;
                }
            }
            QVERIFY2(haveInk, qPrintable(path));
            QVERIFY2(oneColour, qPrintable(path + QStringLiteral(": more than one colour, cannot be tinted cleanly")));
            const int padLeft = left, padRight = image.width() - 1 - right, padTop = top, padBottom = image.height() - 1 - bottom;
            const int margin = category.canvas * 9 / 128 - 1;   // artwork stays clear of the canvas edge
            QVERIFY2(padLeft >= margin && padRight >= margin && padTop >= margin && padBottom >= margin, qPrintable(path));
            // Centred: optical centring may move the bounds by at most 3 % of
            // the canvas, i.e. the two paddings differ by at most 6 % (+1 px rounding).
            const int allowed = category.canvas * 6 / 100 + 2;
            QVERIFY2(qAbs(padLeft - padRight) <= allowed, qPrintable(QStringLiteral("%1: left %2 right %3").arg(path).arg(padLeft).arg(padRight)));
            QVERIFY2(qAbs(padTop - padBottom) <= allowed, qPrintable(QStringLiteral("%1: top %2 bottom %3").arg(path).arg(padTop).arg(padBottom)));
            // Comparable visual size: the artwork fills a useful part of the canvas.
            const int longSide = qMax(right - left + 1, bottom - top + 1);
            QVERIFY2(longSide >= category.canvas * 60 / 100, qPrintable(path));
            ++checked;
        }
    }
    QCOMPARE(checked, 37);
    // The untouched uploads are kept in the repository but are not shipped.
    QVERIFY(!QDir(QStringLiteral(":/icons/originals")).exists());
    QVERIFY(QDir(QStringLiteral(":/icons/tools")).entryList(QDir::Files).size() == kToolIcons.size());
    QVERIFY(QDir(QStringLiteral(":/icons/layers")).entryList(QDir::Files).size() == kLayerIcons.size());
    QVERIFY(QDir(QStringLiteral(":/icons/project-types")).entryList(QDir::Files).size() == kProjectIcons.size());
}

// ------------------------------------------------------------- sub tools
void TestBrushUi::subToolLibraryLoadsRemembersAndResets()
{
    BrushSettings brush;
    const int defaultSize = brush.size(), defaultSpacing = brush.spacing();
    SubToolLibrary library(&brush);
    QVERIFY2(library.loadError().isEmpty(), qPrintable(library.loadError()));
    QVERIFY(!library.isPaintTool());

    // The first brush is the engine's default brush: nothing changes for a
    // workspace that starts on the brush tool.
    library.setTool(QStringLiteral("brush"));
    QVERIFY(library.isPaintTool());
    QVERIFY(library.isBrushTool());
    QCOMPARE(library.activeEngine(), QStringLiteral("digital"));
    QCOMPARE(library.currentIndex(), 0);
    QCOMPARE(brush.size(), defaultSize);
    QCOMPARE(brush.spacing(), defaultSpacing);
    QCOMPARE(brush.opacity(), 100); QCOMPARE(brush.flow(), 100); QCOMPARE(brush.hardness(), 100);
    QCOMPARE(brush.stabilization(), 0); QVERIFY(!brush.textured()); QVERIFY(!brush.erase());
    QVERIFY(!library.isModified());
    QVERIFY(library.categories().contains(QStringLiteral("Paint")));
    library.setSearchText(QStringLiteral("soft"));
    QCOMPARE(library.subTools().size(), 1);
    QCOMPARE(library.subTools().first().toMap().value(QStringLiteral("index")).toInt(), 1);
    library.select(library.subTools().first().toMap().value(QStringLiteral("index")).toInt());
    QCOMPARE(brush.size(), 40); // filtering still selects the real brush configuration
    library.setSearchText(QString());
    QCOMPARE(library.subTools().size(), 4); // Default Digital tab: Round, Soft, Dry, Flat Chisel

    // Digital tabs are separate collections from Realistic tabs.
    QVERIFY(library.engineTabs().size() >= 5);
    library.selectTab(2); // Inking tab
    QCOMPARE(library.currentTabName(), QStringLiteral("Inking"));
    QVERIFY(brush.pressureSize() > 0);
    library.selectTab(0);
    library.select(0);

    // Switching to Realistic engine loads the Realistic collection cleanly.
    brush.setBrushEngine(QStringLiteral("realistic"));
    QCOMPARE(library.activeEngine(), QStringLiteral("realistic"));
    QCOMPARE(library.currentName(), QStringLiteral("Gouache Round"));
    QVERIFY(brush.traditionalMedia());
    QVERIFY(!brush.persistentReservoir());
    QCOMPARE(brush.materialPresetId(), QStringLiteral("gouache-round"));
    QCOMPARE(brush.paintLoad(), 88);
    QCOMPARE(brush.paintBody(), 78);
    brush.setWaterAmount(64);
    QVERIFY(library.isModified());
    library.resetCurrent();
    QCOMPARE(brush.waterAmount(), 22);
    QVERIFY(!library.isModified());
    brush.setBrushEngine(QStringLiteral("digital"));
    QCOMPARE(library.activeEngine(), QStringLiteral("digital"));
    QVERIFY(!brush.traditionalMedia());

    // Every shipped value is inside the range the engine accepts, so loading
    // a sub tool never gets clamped into something else.
    for (const QString& tool : library.paintTools()) {
        library.setTool(tool);
        QVERIFY(library.subTools().size() >= 2);
        QCOMPARE(brush.erase(), tool == QStringLiteral("eraser"));
        for (int i = 0; i < library.subTools().size(); ++i) {
            library.select(i);
            const auto shown = library.subTools().at(i).toMap();
            QCOMPARE(shown.value(QStringLiteral("size")).toInt(), brush.size());
            QVERIFY(brush.size() >= BrushSettings::MinSize && brush.size() <= BrushSettings::MaxSize);
            QVERIFY(brush.spacing() >= BrushSettings::MinSpacing && brush.spacing() <= BrushSettings::MaxSpacing);
            QVERIFY2(!library.isModified(), qPrintable(tool + QStringLiteral(": a shipped value was clamped")));
        }
        library.select(0);
    }
    library.setTool(QStringLiteral("brush"));
    for (const QString& engine : {QStringLiteral("digital"), QStringLiteral("realistic")}) {
        brush.setBrushEngine(engine);
        const int tabCount = library.engineTabs().size();
        for (int t = 0; t < tabCount; ++t) {
            library.selectTab(t);
            const int count = library.subTools().size();
            for (int b = 0; b < count; ++b) {
                library.select(b);
                QVERIFY2(!library.isModified(),
                         qPrintable(engine + QStringLiteral("/") + library.currentName()
                                    + QStringLiteral(": shipped value was clamped")));
            }
        }
    }
    brush.setBrushEngine(QStringLiteral("digital"));
    library.selectTab(0);
    library.select(0);

    // A change in the Tool Property panel belongs to the current sub tool.
    library.setTool(QStringLiteral("eraser"));
    QCOMPARE(brush.size(), 24); QVERIFY(brush.erase());
    brush.setSize(50); brush.setHardness(40);
    QVERIFY(library.isModified());
    library.setTool(QStringLiteral("pen"));
    QCOMPARE(brush.size(), 4); QVERIFY(!brush.erase()); QCOMPARE(brush.stabilization(), 20);
    QVERIFY(!library.isModified());
    library.setTool(QStringLiteral("eraser"));
    QCOMPARE(brush.size(), 50); QCOMPARE(brush.hardness(), 40); QVERIFY(brush.erase());
    library.select(1);
    QCOMPARE(library.currentName(), QStringLiteral("Soft eraser"));
    QCOMPARE(brush.size(), 60); QCOMPARE(brush.hardness(), 20);
    library.select(0);
    QCOMPARE(brush.size(), 50);
    library.resetCurrent();
    QCOMPARE(brush.size(), 24); QCOMPARE(brush.hardness(), 100);
    QVERIFY(!library.isModified());

    // A non-stroke sub-tool collection (fill) loads ToolSettings without mutating brush size.
    ToolSettings toolSettings;
    library.setToolSettings(&toolSettings);
    library.setTool(QStringLiteral("fill"));
    QVERIFY(!library.isPaintTool());
    QVERIFY(library.hasCollections());
    QVERIFY(library.subTools().size() >= 3);
    QCOMPARE(brush.size(), 24);
    library.select(2); // Smart Fill
    QCOMPARE(toolSettings.executableTool(), QStringLiteral("smart_fill"));
    QCOMPARE(brush.size(), 24);

    // A view-only tool has no sub-tools and leaves the brush alone.
    library.setTool(QStringLiteral("zoom"));
    QVERIFY(!library.isPaintTool());
    QVERIFY(!library.hasCollections());
    QVERIFY(library.subTools().isEmpty());
    QCOMPARE(brush.size(), 24);
    library.select(1); library.resetCurrent();     // no effect, no crash
    QCOMPARE(brush.size(), 24);
}

// ---------------------------------------------------------- tool palette
void TestBrushUi::paletteShowsEveryToolAndSelectsOnlyWorkingOnes()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiPalette")));
    auto& ws = app.workspace;
    QCOMPARE(ws.activeTool(), QStringLiteral("brush"));
    screenshot(app, QStringLiteral("01-main-window"));

    // One button per supplied tool icon, all the same size, none overlapping.
    // All 21 tools in the palette are implemented and selectable.
    QList<QRectF> cells;
    for (const QString& tool : kToolIcons) {
        auto* button = item(app, QStringLiteral("tool_") + tool);
        QVERIFY2(button, qPrintable(tool));
        QVERIFY2(button->isVisible(), qPrintable(tool));
        QCOMPARE(button->width(), 34.0); QCOMPARE(button->height(), 34.0);
        QCOMPARE(button->property("iconName").toString(), QStringLiteral("tools/") + tool);
        QVERIFY2(button->property("available").toBool(), qPrintable(tool));
        const QRectF cell(button->mapToScene(QPointF(0, 0)), QSizeF(34, 34));
        for (const QRectF& other : cells) QVERIFY2(!cell.intersects(other), qPrintable(tool));
        cells << cell;
    }
    // One column, with groups scrolling inside the rail.
    QSet<int> columns;
    for (const QRectF& cell : cells) columns << qRound(cell.x());
    QCOMPARE(columns.size(), 1);

    QVERIFY(clickNamed(app, QStringLiteral("tool_eraser")));
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QVERIFY(ws.brush()->erase());
    QVERIFY(item(app, QStringLiteral("tool_eraser"))->property("selected").toBool());
    QVERIFY(!item(app, QStringLiteral("tool_brush"))->property("selected").toBool());

    // Every Stage 11 tool in the palette is selectable and updates activeTool.
    for (const QString& tool : {QStringLiteral("fill"), QStringLiteral("text"), QStringLiteral("lasso"),
                                QStringLiteral("wand"), QStringLiteral("selection"), QStringLiteral("gradient"),
                                QStringLiteral("move"), QStringLiteral("transform"), QStringLiteral("rectangle"),
                                QStringLiteral("circle"), QStringLiteral("line"), QStringLiteral("curveline"),
                                QStringLiteral("smudge")}) {
        QVERIFY(clickNamed(app, QStringLiteral("tool_") + tool));
        QCOMPARE(ws.activeTool(), tool);
    }
    QVERIFY(clickNamed(app, QStringLiteral("tool_pen")));
    QCOMPARE(ws.activeTool(), QStringLiteral("pen"));
    QVERIFY(!ws.brush()->erase());

    // Tool keys.
    app.canvasItem->forceActiveFocus();
    QTest::keyClick(app.window, Qt::Key_E);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QTest::keyClick(app.window, Qt::Key_G);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("fill"));
    QTest::keyClick(app.window, Qt::Key_W);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("wand"));
    QTest::keyClick(app.window, Qt::Key_L);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("lasso"));
    QTest::keyClick(app.window, Qt::Key_M);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("selection"));
    QTest::keyClick(app.window, Qt::Key_B);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("brush"));
    // [ and ] walk the Brush Size panel's sizes.
    QCOMPARE(ws.brush()->size(), 16);
    QTest::keyClick(app.window, Qt::Key_BracketRight);
    QTRY_COMPARE(ws.brush()->size(), 17);
    QTest::keyClick(app.window, Qt::Key_BracketLeft);
    QTRY_COMPARE(ws.brush()->size(), 15);
    QTest::keyClick(app.window, Qt::Key_BracketLeft);
    QTRY_COMPARE(ws.brush()->size(), 12);

    // Document commands live in File rather than occupying the command bar.
    QVERIFY(!item(app, QStringLiteral("newProjectToolbarButton")));

    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// --------------------------------------------------------- brush size grid
void TestBrushUi::brushSizeGridSetsTheRealSize()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiSizes")));
    auto* brush = app.workspace.brush();
    auto* panel = item(app, QStringLiteral("brushSizePanel"));
    QVERIFY(panel && panel->isVisible());
    const QVariantList sizes = panel->property("sizes").toList();
    QVERIFY(sizes.size() >= 20);
    int previous = 0;
    for (const QVariant& value : sizes) {
        const int size = value.toInt();
        QVERIFY(size > previous);                                      // ascending, no duplicates
        QVERIFY(size >= BrushSettings::MinSize && size <= BrushSettings::MaxSize);
        previous = size;
        auto* cell = item(app, QStringLiteral("brushSize%1").arg(size));
        QVERIFY2(cell && cell->width() > 0, qPrintable(QString::number(size)));
    }
    // Every size is reachable without scrolling in the default layout.
    auto* last = item(app, QStringLiteral("brushSize%1").arg(previous));
    QVERIFY(last->mapToItem(panel, QPointF(0, last->height())).y() <= panel->height() + 0.5);

    for (int size : {40, 3, 256, 1, 100}) {
        QVERIFY(clickNamed(app, QStringLiteral("brushSize%1").arg(size)));
        QCOMPARE(brush->size(), size);
        QVERIFY(item(app, QStringLiteral("brushSize%1").arg(size))->property("current").toBool());
        // Tool Property shows the same value: one setting, two controls.
        QCOMPARE(item(app, QStringLiteral("brushSizeSlider"))->property("value").toInt(), size);
    }
    QVERIFY(!item(app, QStringLiteral("brushSize40"))->property("current").toBool());

    // Circles grow with the size they stand for.
    double shownBefore = 0;
    for (const QVariant& value : sizes) {
        QVariant shown;
        QVERIFY(QMetaObject::invokeMethod(panel, "shownDiameter", Q_RETURN_ARG(QVariant, shown), Q_ARG(QVariant, value)));
        QVERIFY(shown.toDouble() > shownBefore);
        QVERIFY(shown.toDouble() <= 26.0);
        shownBefore = shown.toDouble();
    }

    // With a tool that does not paint, the grid does nothing.
    QVERIFY(clickNamed(app, QStringLiteral("tool_hand")));
    QVERIFY(clickNamed(app, QStringLiteral("brushSize40")));
    QCOMPARE(brush->size(), 100);
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// ------------------------------------------------------------ tool property
void TestBrushUi::toolPropertyControlsDriveTheBrush()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiProperties")));
    auto* brush = app.workspace.brush();

    QVERIFY(setSlider(app, QStringLiteral("brushOpacitySlider"), 0.5));
    QVERIFY(qAbs(brush->opacity() - 50) <= 2);
    QVERIFY(setSlider(app, QStringLiteral("brushDensitySlider"), 0.25));
    QVERIFY(qAbs(brush->flow() - 25) <= 2);
    QVERIFY(chooseHardness(app, 0));
    QCOMPARE(brush->hardness(), 0);
    QVERIFY(chooseHardness(app, 3));
    QCOMPARE(brush->hardness(), 75);
    QVERIFY(setSlider(app, QStringLiteral("brushStabilizationSlider"), 1.0));
    QCOMPARE(brush->stabilization(), 100);
    QVERIFY(!item(app, QStringLiteral("brushSpacingSlider")));
    // Return to the top of the property panel for the precision controls.
    item(app, QStringLiteral("toolPropertyScroll"))->setProperty("contentY", 0);
    QCoreApplication::processEvents();
    // Brush size: both ends of the bar are the engine's limits.
    QVERIFY(setSlider(app, QStringLiteral("brushSizeSlider"), 1.0));
    QCOMPARE(brush->size(), int(BrushSettings::MaxSize));
    QVERIFY(setSlider(app, QStringLiteral("brushSizeSlider"), 0.0));
    QCOMPARE(brush->size(), int(BrushSettings::MinSize));
    QVERIFY(setSlider(app, QStringLiteral("brushSizeSlider"), 0.5));
    // The rail is logarithmic across the engine's full range, so the middle is
    // the geometric mean of 1 and MaxSize (~90 px), not a quarter of the way in.
    QVERIFY(brush->size() > 40 && brush->size() < 200);               // the bar gives small sizes more room

    // Digital is the default Brush Engine, so physical material controls stay hidden.
    // Brush Engine selector itself belongs in Koma Settings, NOT in Tool Properties.
    QCOMPARE(app.workspace.brushEngine(), QStringLiteral("digital"));
    QVERIFY(!app.workspace.isRealisticEngine());
    QVERIFY(item(app, QStringLiteral("brushEngineStatusText")) == nullptr);
    QVERIFY(item(app, QStringLiteral("brushEngineSettingsButton")) == nullptr);
    auto* medium = item(app, QStringLiteral("paintingMediumChoice"));
    QVERIFY(medium && !medium->isVisible());
    auto* paperSection = item(app, QStringLiteral("paperSurfaceSection"));
    QVERIFY(paperSection && !paperSection->isVisible());
    auto* materialSection = item(app, QStringLiteral("materialPaintSection"));
    QVERIFY(materialSection && !materialSection->isVisible());

    // Switching to Realistic is locked until enabled; once enabled it reveals
    // physical material controls and medium choice.
    QVERIFY(!app.workspace.isRealisticEngineEnabled());
    app.workspace.setBrushEngine(QStringLiteral("realistic"));
    QCOMPARE(app.workspace.brushEngine(), QStringLiteral("digital"));
    app.workspace.setRealisticEngineEnabled(true);
    app.workspace.setBrushEngine(QStringLiteral("realistic"));
    QTRY_VERIFY(medium->isVisible());
    QTRY_VERIFY(paperSection->isVisible());
    QTRY_VERIFY(materialSection->isVisible());
    QTest::qWait(180);
    scrollIntoView(medium);
    auto clickSegment = [&](QQuickItem* segmented, int index) {
        scrollIntoView(segmented);
        QCoreApplication::processEvents();
        QList<QQuickItem*> buttons;
        for (auto* child : segmented->childItems()) {
            if (child->width() > 0 && child->height() > 0 && child->metaObject()->inherits(&QQuickItem::staticMetaObject)) {
                if (QString::fromLatin1(child->metaObject()->className()).contains(QStringLiteral("AbstractButton")))
                    buttons.append(child);
            }
        }
        QVERIFY2(index >= 0 && index < buttons.size(), qPrintable(QString::number(buttons.size())));
        scrollIntoView(buttons[index]);
        click(app, buttons[index]);
    };
    clickSegment(medium, 1); // Watercolour
    QTRY_COMPARE(brush->mediumId(), QStringLiteral("watercolour"));
    QTest::qWait(180);
    clickSegment(medium, 0); // Gouache
    QTRY_COMPARE(brush->mediumId(), QStringLiteral("gouache"));
    // Switching back to Digital hides incompatible material controls again.
    app.workspace.setBrushEngine(QStringLiteral("digital"));
    QTRY_VERIFY(!medium->isVisible());
    QTRY_VERIFY(!paperSection->isVisible());
    QTest::qWait(180);

    // Tip shape.
    auto* tip = item(app, QStringLiteral("brushTipChoice"));
    QVERIFY(tip && tip->isVisible());
    QVERIFY(!brush->textured());
    scrollIntoView(tip);
    QCoreApplication::processEvents();
    QPoint at = tip->mapToScene(QPointF(tip->width() * 0.75, tip->height() / 2)).toPoint();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, at);
    QTRY_VERIFY(brush->textured());
    scrollIntoView(tip);
    QCoreApplication::processEvents();
    at = tip->mapToScene(QPointF(tip->width() * 0.25, tip->height() / 2)).toPoint();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, at);
    QTRY_VERIFY(!brush->textured());

    // The controls follow the brush when it changes elsewhere.
    brush->setOpacity(73);
    QCOMPARE(item(app, QStringLiteral("brushOpacitySlider"))->property("value").toInt(), 73);

    // Reset returns the sub tool to what it shipped with.
    auto* reset = item(app, QStringLiteral("resetSubToolButton"));
    QVERIFY(reset && reset->isEnabled());
    click(app, reset);
    QCOMPARE(brush->opacity(), 100); QCOMPARE(brush->flow(), 100); QCOMPARE(brush->hardness(), 100);
    QCOMPARE(brush->stabilization(), 0); QCOMPARE(brush->size(), 16);
    QVERIFY(!reset->isEnabled());

    screenshot(app, QStringLiteral("02-tool-property"));
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// ------------------------------------------------------------------ painting
void TestBrushUi::strokesFollowHardnessAndTheEraserErases()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiStrokes")));
    auto& ws = app.workspace;
    ws.colors()->setForeground(QColor(200, 30, 30));

    // Hard 40 px dot, set up through the panels.
    QVERIFY(clickNamed(app, QStringLiteral("brushSize40")));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 200, 300));
    QTest::qWait(20);
    QCOMPARE(alpha(app, 200, 300), 255);
    QCOMPARE(alpha(app, 214, 300), 255);            // 14 px from the centre of a 20 px radius
    QCOMPARE(alpha(app, 226, 300), 0);

    // Same size, hardness 0: solid in the middle, fading towards the rim.
    QVERIFY(chooseHardness(app, 0));
    QCOMPARE(ws.brush()->hardness(), 0);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 400, 300));
    QTest::qWait(20);
    QVERIFY(alpha(app, 400, 300) >= 245);
    const int softMiddle = alpha(app, 410, 300), softOuter = alpha(app, 416, 300);
    QVERIFY2(softMiddle > 60 && softMiddle < 200, qPrintable(QString::number(softMiddle)));
    QVERIFY2(softOuter > 0 && softOuter < softMiddle, qPrintable(QString::number(softOuter)));

    // Density 25 %: one dab deposits a quarter.
    QVERIFY(chooseHardness(app, 4));
    item(app, QStringLiteral("toolPropertyScroll"))->setProperty("contentY", 0);
    QCoreApplication::processEvents();
    QVERIFY(setSlider(app, QStringLiteral("brushDensitySlider"), 0.25));
    const int density = ws.brush()->flow();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 600, 300));
    QTest::qWait(20);
    const int expected = qRound(255.0 * density / 100.0);
    QVERIFY2(qAbs(alpha(app, 600, 300) - expected) <= 3, qPrintable(QString::number(alpha(app, 600, 300))));

    screenshot(app, QStringLiteral("03-strokes"));

    // Eraser: rub through the hard dot.
    QVERIFY(clickNamed(app, QStringLiteral("tool_eraser")));
    QVERIFY(ws.brush()->erase());
    QVERIFY(clickNamed(app, QStringLiteral("brushSize12")));
    const quint32 before = pixel(app, 200, 300);
    drag(app, {onCanvas(app, 170, 300), onCanvas(app, 200, 300), onCanvas(app, 230, 300)});
    QCOMPARE(pixel(app, 200, 300), 0U);
    QCOMPARE(alpha(app, 200, 288), 255);            // above the eraser's path: still painted
    QVERIFY(ws.history()->canUndo());
    ws.history()->undo();
    QCOMPARE(pixel(app, 200, 300), before);         // erasing is undoable like painting
    ws.history()->redo();
    QCOMPARE(pixel(app, 200, 300), 0U);

    // Back on the brush, painting adds again.
    QVERIFY(clickNamed(app, QStringLiteral("tool_brush")));
    QVERIFY(!ws.brush()->erase());
    QVERIFY(setSlider(app, QStringLiteral("brushDensitySlider"), 1.0));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 200, 300));
    QTest::qWait(20);
    QCOMPARE(alpha(app, 200, 300), 255);
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

void TestBrushUi::stabilizationSmoothsAShakyLine()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiStabilizer")));
    auto& ws = app.workspace;
    QVERIFY(clickNamed(app, QStringLiteral("tool_brush")));
    QVERIFY(clickNamed(app, QStringLiteral("brushSize4")));

    // The same shaky hand twice: a zig-zag of +-24 document px around a line.
    auto zigzag = [&](int baseline) {
        QList<QPoint> path;
        for (int i = 0; i <= 40; ++i) path << onCanvas(app, 150 + i * 12, baseline + ((i % 2 == 0) ? -24 : 24));
        return path;
    };
    // Height of the painted band in the middle of the stroke.
    auto bandHeight = [&](int baseline) {
        int top = 100000, bottom = -1;
        for (int x = 330; x <= 470; ++x)
            for (int y = baseline - 60; y <= baseline + 60; ++y)
                if (alpha(app, x, y) > 0) { top = qMin(top, y); bottom = qMax(bottom, y); }
        return bottom < 0 ? 0 : bottom - top + 1;
    };

    QCOMPARE(ws.brush()->stabilization(), 0);
    drag(app, zigzag(150));
    const int shaky = bandHeight(150);
    QVERIFY2(shaky >= 40, qPrintable(QString::number(shaky)));

    QVERIFY(setSlider(app, QStringLiteral("brushStabilizationSlider"), 1.0));
    QCOMPARE(ws.brush()->stabilization(), 100);
    drag(app, zigzag(420));
    const int steady = bandHeight(420);
    QVERIFY2(steady > 0, "the stabilised stroke painted nothing");
    QVERIFY2(steady * 3 < shaky, qPrintable(QStringLiteral("shaky %1, stabilised %2").arg(shaky).arg(steady)));

    // A stabilised line still ends where the pointer was lifted.
    const QPoint end = zigzag(420).last();
    const QPointF endDoc = app.canvas->viewportToDocument(app.canvasItem->mapFromScene(end).x(), app.canvasItem->mapFromScene(end).y());
    QVERIFY(alpha(app, qFloor(endDoc.x()), qFloor(endDoc.y())) > 0);
    screenshot(app, QStringLiteral("04-stabilization"));
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// ---------------------------------------------------------------- sub tools
void TestBrushUi::subToolsLoadAndKeepTheirSettings()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiSubTools")));
    auto& ws = app.workspace;
    auto* brush = ws.brush();
    QVERIFY(clickNamed(app, QStringLiteral("tool_brush")));  // the list is the brush's
    auto* list = item(app, QStringLiteral("subToolList"));
    QVERIFY(list && list->isVisible());
    QTRY_COMPARE(list->property("count").toInt(), 4);       // Digital Default: Round, Soft, Dry, Flat Chisel
    auto* softRow = item(app, QStringLiteral("subTool1"));
    QVERIFY(softRow);
    QTRY_VERIFY(softRow->isVisible() && softRow->width() > 0 && softRow->height() > 0);

    // Primary engine tabs: Digital is selected by default; Realistic is locked
    // until enabled in Settings → Brush Engine.
    auto* digitalTab = item(app, QStringLiteral("primaryEngineTab_digital"));
    auto* realisticTab = item(app, QStringLiteral("primaryEngineTab_realistic"));
    QVERIFY(digitalTab && digitalTab->isVisible());
    QVERIFY(realisticTab && realisticTab->isVisible());
    QCOMPARE(ws.brushEngine(), QStringLiteral("digital"));
    QVERIFY(!ws.isRealisticEngineEnabled());

    // Clicking locked Realistic tab shows an inline unlock prompt without
    // changing the active engine or allocating physical material tiles.
    click(app, realisticTab);
    QCOMPARE(ws.brushEngine(), QStringLiteral("digital"));
    auto* lockedNotice = item(app, QStringLiteral("realisticLockedNotice"));
    QVERIFY(lockedNotice && lockedNotice->isVisible());
    drag(app, {onCanvas(app, 200, 200), onCanvas(app, 230, 200)});
    QCOMPARE(ws.layers()->layerAt(0)->materialTiles().allocatedTiles(), std::size_t(0));

    // Clicking Settings… opens Settings → Brush Engine; enabling Realistic and
    // selecting it unlocks the Realistic tab and activates the material engine.
    auto* unlockBtn = item(app, QStringLiteral("realisticUnlockSettingsButton"));
    QVERIFY(unlockBtn && unlockBtn->isVisible());
    click(app, unlockBtn);
    auto* settingsDlg = app.root->findChild<QObject*>(QStringLiteral("komaSettingsDialog"));
    QVERIFY(settingsDlg);
    QTRY_VERIFY(settingsDlg->property("opened").toBool());
    auto* enableToggle = item(app, QStringLiteral("realisticEngineEnableToggle"));
    QVERIFY(enableToggle && enableToggle->isVisible());
    click(app, enableToggle);
    auto* realisticOpt = item(app, QStringLiteral("brushEngineOption_realistic"));
    QVERIFY(realisticOpt && realisticOpt->isVisible());
    click(app, realisticOpt);
    click(app, item(app, QStringLiteral("komaSettingsApply")));
    QTRY_VERIFY(!settingsDlg->property("opened").toBool());

    QCOMPARE(ws.brushEngine(), QStringLiteral("realistic"));
    QVERIFY(ws.isRealisticEngineEnabled());
    QTRY_VERIFY(!lockedNotice->isVisible());
    auto* loadSlider = item(app, QStringLiteral("brushPaintLoadSlider"));
    QVERIFY(loadSlider);
    QTRY_VERIFY(loadSlider->isVisible());

    // Realistic has its own collection tabs (Default, Gouache, Watercolour).
    QCOMPARE(ws.subTools()->currentName(), QStringLiteral("Gouache Round"));
    QQuickItem* wcTab = nullptr;
    QTRY_VERIFY((wcTab = item(app, QStringLiteral("collectionTab_2"))) != nullptr
                && wcTab->isVisible() && wcTab->width() > 0 && wcTab->height() > 0);
    QVERIFY(clickNamed(app, QStringLiteral("collectionTab_2"))); // Watercolour tab
    QTRY_COMPARE(ws.subTools()->currentTabName(), QStringLiteral("Watercolour"));
    QTRY_COMPARE(ws.subTools()->currentName(), QStringLiteral("Watercolour Round"));
    QCOMPARE(brush->mediumId(), QStringLiteral("watercolour"));
    QCOMPARE(brush->materialPresetId(), QStringLiteral("watercolour-round"));
    auto* wcPaper = item(app, QStringLiteral("watercolourPaperChoice"));
    QVERIFY(wcPaper);
    QTRY_VERIFY(wcPaper->isVisible() && wcPaper->height() > 0
        && wcPaper->parentItem()->parentItem()->parentItem()->height() >= wcPaper->height() + 4);
    QTest::qWait(180);
    scrollIntoView(wcPaper);
    QCoreApplication::processEvents();
    QList<QQuickItem*> paperSegments;
    for (auto* child : wcPaper->childItems()) {
        if (child->width() > 0 && child->height() > 0
            && QString::fromLatin1(child->metaObject()->className()).contains(QStringLiteral("AbstractButton")))
            paperSegments.append(child);
    }
    QCOMPARE(paperSegments.size(), 3);
    scrollIntoView(paperSegments[2]);
    click(app, paperSegments[2]);
    QTRY_COMPARE(ws.document()->surfaceId(), QStringLiteral("rough-paper"));
    QVERIFY(setSlider(app, QStringLiteral("paperWetnessSlider"), 0.5));
    QVERIFY(ws.document()->surfaceWetness() >= 45 && ws.document()->surfaceWetness() <= 55);

    ws.subTools()->select(6); // Clean Water in Watercolour tab
    QTRY_COMPARE(ws.subTools()->currentName(), QStringLiteral("Clean Water"));
    QCOMPARE(brush->materialPresetId(), QStringLiteral("clean-water"));
    ws.subTools()->select(7); // Paint Lifting in Watercolour tab
    QTRY_COMPARE(ws.subTools()->currentName(), QStringLiteral("Paint Lifting"));
    QCOMPARE(brush->materialPresetId(), QStringLiteral("paint-lifting"));

    // Return to Digital via the primary Digital tab.
    click(app, digitalTab);
    QTRY_COMPARE(ws.brushEngine(), QStringLiteral("digital"));
    QVERIFY(!loadSlider->isVisible());
    QTest::qWait(50);
    QCoreApplication::processEvents();
    auto* softAgain = item(app, QStringLiteral("subTool1"));
    QVERIFY(softAgain);
    QTRY_VERIFY(softAgain->isVisible() && softAgain->width() > 0 && softAgain->height() > 0);
    QVERIFY(clickNamed(app, QStringLiteral("subTool1")));  // Soft brush
    QTRY_COMPARE(ws.subTools()->currentName(), QStringLiteral("Soft brush"));
    QCOMPARE(brush->size(), 40); QCOMPARE(brush->hardness(), 30); QCOMPARE(brush->flow(), 35);
    QVERIFY(item(app, QStringLiteral("brushSize40"))->property("current").toBool());

    // Change it, switch tool, come back: the change is still there.
    QVERIFY(clickNamed(app, QStringLiteral("brushSize70")));
    QVERIFY(clickNamed(app, QStringLiteral("tool_pencil")));
    QCOMPARE(list->property("count").toInt(), 2);
    QVERIFY(brush->textured()); QCOMPARE(brush->size(), 6);
    QVERIFY(clickNamed(app, QStringLiteral("tool_airbrush")));
    QCOMPARE(brush->hardness(), 0); QCOMPARE(brush->flow(), 8);
    QVERIFY(clickNamed(app, QStringLiteral("tool_brush")));
    QCOMPARE(ws.subTools()->currentName(), QStringLiteral("Soft brush"));
    QCOMPARE(brush->size(), 70); QCOMPARE(brush->hardness(), 30);

    // A tool without sub tools says so and offers no brush controls.
    QVERIFY(clickNamed(app, QStringLiteral("tool_zoom")));
    QVERIFY(!list->isVisible());
    QVERIFY(!item(app, QStringLiteral("brushSizeSlider"))->isVisible());
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// --------------------------------------------------------------- view tools
void TestBrushUi::viewToolsZoomPanAndPickColour()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiViewTools")));
    auto& ws = app.workspace;
    const QColor red(210, 40, 30);
    ws.colors()->setForeground(red);
    QVERIFY(clickNamed(app, QStringLiteral("brushSize40")));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 300, 300));
    QTest::qWait(20);
    QCOMPARE(alpha(app, 300, 300), 255);
    const int tilesBefore = int(ws.layers()->layerAt(0)->tiles().allocatedTiles());

    // Eyedropper: picks what is on the artwork, never paints.
    ws.colors()->setForeground(QColor(10, 20, 250));
    QVERIFY(clickNamed(app, QStringLiteral("tool_eyedropper")));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 300, 300));
    QTRY_COMPARE(ws.colors()->foreground(), red);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 600, 100));
    QTRY_COMPARE(ws.colors()->foreground(), QColor(Qt::white));   // the paper
    // Outside the picture nothing is picked.
    ws.colors()->setForeground(red);
    const QPoint outside = onCanvas(app, 300, -30);   // on the desk above the picture
    QVERIFY(app.canvasItem->contains(app.canvasItem->mapFromScene(outside)));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, outside);
    QTest::qWait(20);
    QCOMPARE(ws.colors()->foreground(), red);
    QCOMPARE(int(ws.layers()->layerAt(0)->tiles().allocatedTiles()), tilesBefore);
    // colorAt() itself: empty where there is no picture.
    QVERIFY(!app.canvas->colorAt(-5, -5).isValid());

    // Zoom tool.
    QVERIFY(clickNamed(app, QStringLiteral("tool_zoom")));
    const qreal zoom = app.canvas->zoom();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 400, 300));
    QTRY_VERIFY(qAbs(app.canvas->zoom() - zoom * 1.5) < 0.5);
    QTest::mouseClick(app.window, Qt::RightButton, Qt::NoModifier, onCanvas(app, 400, 300));
    QTRY_VERIFY(qAbs(app.canvas->zoom() - zoom) < 0.5);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::AltModifier, onCanvas(app, 400, 300));
    QTRY_VERIFY(app.canvas->zoom() < zoom - 1);

    // Hand tool: dragging moves the view, the artwork stays as it is.
    QVERIFY(clickNamed(app, QStringLiteral("tool_hand")));
    const QPointF centreBefore = app.canvas->viewCenter();
    const QPoint from = centre(app.canvasItem);
    drag(app, {from, from + QPoint(40, 10), from + QPoint(80, 20)});
    QVERIFY(app.canvas->viewCenter().x() < centreBefore.x() - 10);
    QVERIFY(app.canvas->viewCenter().y() < centreBefore.y() - 2);
    QCOMPARE(int(ws.layers()->layerAt(0)->tiles().allocatedTiles()), tilesBefore);
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// ------------------------------------------------------------------- layers
void TestBrushUi::layerIconsAct()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiLayers")));
    auto* layers = app.workspace.layers();
    QCOMPARE(layers->rowCount(), 1);

    auto* remove = item(app, QStringLiteral("deleteLayerButton"));
    QVERIFY(remove && remove->isVisible());
    QVERIFY(!remove->isEnabled());                      // the only layer cannot be deleted
    QCOMPARE(remove->property("iconName").toString(), QStringLiteral("layers/delete"));

    QVERIFY(clickNamed(app, QStringLiteral("addLayerButton")));
    QTRY_COMPARE(layers->rowCount(), 2);
    QTRY_VERIFY(remove->isEnabled());

    // The always-visible layer opacity control edits the actual selected layer.
    auto* opacitySlider = item(app, QStringLiteral("layerOpacitySlider"));
    auto* opacityField = item(app, QStringLiteral("layerOpacityField"));
    QVERIFY(opacitySlider && opacityField && opacitySlider->isVisible());
    layers->setActiveLayerIndex(0);
    layers->layerAt(0)->setOpacity(0.37);
    QTRY_COMPARE(opacityField->property("value").toInt(), 37);
    layers->setActiveLayerIndex(1);
    QTRY_COMPARE(opacityField->property("value").toInt(), 100);
    const QPoint opacityAt = opacitySlider->mapToScene(QPointF(opacitySlider->width() / 2, opacitySlider->height() / 2)).toPoint();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, opacityAt);
    QTRY_VERIFY(qAbs(layers->layerAt(1)->opacity() - 0.5) < 0.05);
    QCOMPARE(layers->layerAt(0)->opacity(), 0.37);

    // Eye and lock in the row.
    const int active = layers->activeLayerIndex();
    auto* layer = layers->layerAt(active);
    QVERIFY(layer->isVisible());
    auto* eye = item(app, QStringLiteral("layerVisible%1").arg(active));
    QVERIFY(eye && eye->width() > 0);
    click(app, eye);
    QVERIFY(!layer->isVisible());
    QTRY_VERIFY(eye->property("dimmed").toBool());
    click(app, eye);
    QVERIFY(layer->isVisible());

    auto* lock = item(app, QStringLiteral("layerLockButton"));
    QVERIFY(lock && lock->isVisible());
    QVERIFY(!layer->isLocked());
    click(app, lock);
    QVERIFY(layer->isLocked());
    QTRY_VERIFY(lock->property("selected").toBool());
    // (That a locked layer refuses paint is covered by test_canvas_viewport.)
    auto* rowLock = item(app, QStringLiteral("layerLocked%1").arg(active));
    QVERIFY(rowLock && !rowLock->property("dimmed").toBool());
    click(app, rowLock);                                // the row's own lock toggles the same flag
    QVERIFY(!layer->isLocked());
    QTRY_VERIFY(!lock->property("selected").toBool());
    click(app, lock);
    QVERIFY(layer->isLocked());
    click(app, lock);
    QVERIFY(!layer->isLocked());

    screenshot(app, QStringLiteral("05-layers"));
    click(app, remove);
    QTRY_COMPARE(layers->rowCount(), 1);
    QTRY_VERIFY(!remove->isEnabled());
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

void TestBrushUi::thumbnailWheelResizesWithoutChangingSelection()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiThumbnailWheel")));
    auto* panel = item(app, QStringLiteral("layersPanel"));
    auto* thumbnail = item(app, QStringLiteral("layerThumbnail0"));
    QVERIFY(panel && thumbnail);
    QCOMPARE(panel->property("thumbnailSize").toInt(), 32);
    QCOMPARE(qRound(thumbnail->width()), 32);
    const auto sendWheel = [&](int delta) {
        const QPoint at = centre(thumbnail);
        QTest::mouseMove(app.window, at);
        QWheelEvent event(QPointF(at), QPointF(app.window->mapToGlobal(at)), QPoint(),
                          QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &event);
        QCoreApplication::processEvents();
    };
    sendWheel(60);
    QCOMPARE(panel->property("thumbnailSize").toInt(), 32);
    sendWheel(60);
    QTRY_COMPARE(panel->property("thumbnailSize").toInt(), 48);
    // Let Qt Quick polish the RowLayout after the modeless wheel event before
    // asserting the delegate's actual thumbnail geometry.
    QTRY_COMPARE(qRound(thumbnail->width()), 48);
    QCOMPARE(app.workspace.layers()->activeLayerIndex(), 0);
    sendWheel(-120);
    QTRY_COMPARE(panel->property("thumbnailSize").toInt(), 32);
    sendWheel(-120);
    QTRY_COMPARE(panel->property("thumbnailSize").toInt(), 24);
    sendWheel(-120); // endpoint remains compact
    QCOMPARE(panel->property("thumbnailSize").toInt(), 24);
    QVERIFY(QMetaObject::invokeMethod(panel, "setThumbnailWheelEnabled", Q_ARG(QVariant, QVariant(false))));
    sendWheel(120);
    QCOMPARE(panel->property("thumbnailSize").toInt(), 24);
    QVERIFY(item(app, QStringLiteral("dockOptions_layers")));
    QVERIFY(!item(app, QStringLiteral("commandBarSizeField")));
    QVERIFY(!item(app, QStringLiteral("commandBarOpacityField")));
    QVERIFY(!item(app, QStringLiteral("preferencesToolbarButton")));
}

// The approved visual defaults are a triangle in dark mode and a square in
// light mode. The user's explicit session preference is tested by the control
// itself; this test protects the theme-dependent first-run default.
void TestBrushUi::colorSelectorShapeDefaultsFollowTheme()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiColorWheelShape")));
    auto* theme = themeOf(app);
    auto* wheel = item(app, QStringLiteral("colorWheel"));
    QVERIFY(theme && wheel);

    theme->setProperty("mode", QStringLiteral("dark"));
    QTRY_VERIFY(wheel->property("triangle").toBool());
    theme->setProperty("mode", QStringLiteral("light"));
    QTRY_VERIFY(!wheel->property("triangle").toBool());
    theme->setProperty("mode", QStringLiteral("dark"));
    QTRY_VERIFY(wheel->property("triangle").toBool());
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// The spectral mixer is deterministic and retains exact endpoints.
void TestBrushUi::spectralMixerProducesPaintLikeColours()
{
    QFile script(QStringLiteral(":/components/SpectralMix.js"));
    QVERIFY(script.open(QIODevice::ReadOnly));
    QString source = QString::fromUtf8(script.readAll());
    source.remove(QStringLiteral(".pragma library"));
    QJSEngine js;
    const QJSValue loaded = js.evaluate(source, QStringLiteral("SpectralMix.js"));
    QVERIFY2(!loaded.isError(), qPrintable(loaded.toString()));
    auto mix = js.globalObject().property(QStringLiteral("mixHex"));
    QVERIFY(mix.isCallable());
    QCOMPARE(mix.call({QStringLiteral("#002185"), QStringLiteral("#FCD200"), 0.0}).toString(), QStringLiteral("#002185"));
    QCOMPARE(mix.call({QStringLiteral("#002185"), QStringLiteral("#FCD200"), 0.5}).toString(), QStringLiteral("#3D933E"));
    QCOMPARE(mix.call({QStringLiteral("#002185"), QStringLiteral("#FCD200"), 1.0}).toString(), QStringLiteral("#FCD200"));
}

void TestBrushUi::mixerMaterialMixtureLoadsIntoBrushWithoutLosingFractions()
{
    BrushSettings brush;
    QSignalSpy changed(&brush, &BrushSettings::materialMixtureChanged);
    QVariantList pigments;
    pigments.push_back(QVariantMap{{QStringLiteral("pigmentId"), QStringLiteral("PY3")},
                                   {QStringLiteral("mass"), 0.125}});
    pigments.push_back(QVariantMap{{QStringLiteral("pigmentId"), QStringLiteral("PB15")},
                                   {QStringLiteral("mass"), 0.875}});
    const QVariantMap mixture{{QStringLiteral("mediumId"), QStringLiteral("watercolour")},
                              {QStringLiteral("pigments"), pigments},
                              {QStringLiteral("water"), 2.0}, {QStringLiteral("binder"), 0.12},
                              {QStringLiteral("filler"), 0.0}};
    QVERIFY(brush.loadMaterialMixture(mixture));
    QCOMPARE(brush.materialMixture(), mixture);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!brush.loadMaterialMixture(QVariantMap{}));
    QCOMPARE(brush.materialMixture(), mixture);
    brush.clearMaterialMixture();
    QVERIFY(brush.materialMixture().isEmpty());
    QCOMPARE(changed.count(), 2);
}

void TestBrushUi::colorPanelTabsPreserveWheelAndMixerState()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiColorPanelTabs")));
    auto* colorPanel = item(app, QStringLiteral("colorPanel"));
    auto* wheelTabBtn = item(app, QStringLiteral("colorPanelTab_wheel"));
    auto* mixerTabBtn = item(app, QStringLiteral("colorPanelTab_mixer"));
    auto* wheel = item(app, QStringLiteral("colorWheel"));
    auto* addToMixerBtn = item(app, QStringLiteral("addToPaintMixerButton"));
    QVERIFY(colorPanel && wheelTabBtn && mixerTabBtn && wheel && addToMixerBtn);

    // Choose a distinct arbitrary RGB colour in the Color Wheel, add it to the Paint Mixer.
    const QColor picked(QStringLiteral("#7A42F4"));
    app.workspace.colors()->setForeground(picked);
    QTRY_COMPARE(app.workspace.colors()->foreground(), picked);
    QVERIFY(clickNamed(app, QStringLiteral("addToPaintMixerButton")));

    auto* mixer = app.workspace.mixer();
    QVERIFY(mixer != nullptr);
    QTRY_COMPARE(mixer->deposits().size(), 1);
    QVERIFY(mixer->activeDeposit().value(QStringLiteral("containsDigitalImport")).toBool());
    QVERIFY(!app.workspace.document()->isModified());

    // Switch to the Paint Mixer tab in-place inside Colors panel.
    QVERIFY(clickNamed(app, QStringLiteral("colorPanelTab_mixer")));
    QTRY_COMPARE(colorPanel->property("activeTab").toInt(), 1);
    auto* mixerSurface = item(app, QStringLiteral("mixerSurface"));
    QVERIFY(mixerSurface && mixerSurface->isVisible());
    // Allow StackLayout polish + PanelSection height animation to settle before scrolling to library rows.
    QTest::qWait(220);

    // Add PY3 pigment to the active deposit, mix, and load into brush via UI buttons.
    auto* addPy3 = item(app, QStringLiteral("pigmentAdd_PY3"));
    QVERIFY(addPy3 && addPy3->isVisible() && addPy3->height() > 0);
    QVERIFY(scrollIntoView(addPy3));
    QTest::qWait(30);
    click(app, addPy3);
    QTRY_COMPARE(mixer->activeDeposit().value(QStringLiteral("components")).toList().size(), 2);
    QVERIFY(clickNamed(app, QStringLiteral("mixerLoadBrushButton")));
    QTRY_VERIFY(!app.workspace.brush()->materialMixture().isEmpty());
    // Loading a physical mixture must not silently switch the user's Brush Engine.
    QCOMPARE(app.workspace.brushEngine(), QStringLiteral("digital"));
    QVERIFY(!app.workspace.isRealisticEngine());

    // Switch back to Color Wheel tab -> wheel colour & state preserved; mixer state preserved.
    QVERIFY(clickNamed(app, QStringLiteral("colorPanelTab_wheel")));
    QTRY_COMPARE(colorPanel->property("activeTab").toInt(), 0);
    QCOMPARE(app.workspace.colors()->foreground(), picked);

    // Switch back to Paint Mixer -> send resulting mixed colour back to Color Wheel.
    QVERIFY(clickNamed(app, QStringLiteral("colorPanelTab_mixer")));
    QTRY_COMPARE(colorPanel->property("activeTab").toInt(), 1);
    QTest::qWait(100);
    QVERIFY(clickNamed(app, QStringLiteral("mixerToColorWheelButton")));
    QTRY_VERIFY(app.workspace.colors()->foreground() != picked);

    // Mixer undo works decoupled from document undo stack.
    QVERIFY(!app.workspace.history()->canUndo());
    QVERIFY(clickNamed(app, QStringLiteral("mixerUndoButton")));
    QTRY_COMPARE(mixer->activeDeposit().value(QStringLiteral("components")).toList().size(), 1);
    QVERIFY(!app.workspace.document()->isModified());

    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// --------------------------------------------------------------- new project
void TestBrushUi::newProjectDialogShowsTheProjectTypeIcons()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiNewProject")));
    QVERIFY(QMetaObject::invokeMethod(app.root.get(), "requestNewDocument"));
    auto* dialog = app.root->findChild<QObject*>(QStringLiteral("newProjectDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTest::qWait(50);
    const QStringList types = {"illustration", "comic-page", "multi-page-comic", "animation"};
    qreal previousX = -1, y = -1;
    for (const QString& type : types) {
        auto* tile = item(app, QStringLiteral("projectType_") + type);
        QVERIFY2(tile && tile->isVisible() && tile->width() > 80, qPrintable(type));
        const QPointF position = tile->mapToScene(QPointF(0, 0));
        QVERIFY(position.x() > previousX);              // one row, left to right
        if (y < 0) y = position.y(); else QCOMPARE(position.y(), y);
        previousX = position.x();
    }
    QVERIFY(item(app, QStringLiteral("projectType_illustration"))->property("current").toBool());
    // The project-type artwork is a finished illustration: it keeps its own
    // colours and must never be recoloured by the icon theme.
    for (const QString& type : types) {
        auto* artwork = findVisual(item(app, QStringLiteral("projectType_") + type),
                                   QStringLiteral("kpArt_project-types/") + type);
        QVERIFY2(artwork, qPrintable(type));
        QVERIFY2(!artwork->property("tinted").toBool(), qPrintable(type));
    }
    auto* tintedGlyph = buttonIcon(app, QStringLiteral("tool_brush"), QStringLiteral("tools/brush"));
    QVERIFY(tintedGlyph);
    QVERIFY(tintedGlyph->property("tinted").toBool());   // glyphs are still tinted
    screenshot(app, QStringLiteral("06-new-project"));
    // The same tiles on the light theme: the preview cards must still separate
    // the artwork from the dialog behind them.
    if (auto* theme = themeOf(app)) {
        theme->setProperty("mode", QStringLiteral("light"));
        QTest::qWait(50);
        screenshot(app, QStringLiteral("07-new-project-light"));
        theme->setProperty("mode", QStringLiteral("dark"));
        app.settings->setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
        QTest::qWait(20);
    }
    // The dialog still creates a project.
    const int tabs = app.workspace.projectTabs().size();
    QVERIFY(clickNamed(app, QStringLiteral("createProjectButton")));
    QTRY_COMPARE(app.workspace.projectTabs().size(), tabs + 1);
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// ------------------------------------------------------------------- theme
// The icons follow the theme: soft grey in the dark theme, dark grey in the
// light one, an accent treatment when selected, a subdued grey when
// unavailable — never pure white, never pure black.
void TestBrushUi::iconsFollowTheTheme()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiTheme")));
    app.settings->setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    for (const char* key : {"normal", "hover", "selected", "disabled"})
        app.settings->setValue(QStringLiteral("ui/icons/") + QLatin1String(key), QString());

    auto* theme = themeOf(app);
    QVERIFY(theme);
    QCOMPARE(theme->property("mode").toString(), QStringLiteral("dark"));

    auto* penIcon = buttonIcon(app, QStringLiteral("tool_pen"), QStringLiteral("tools/pen"));
    auto* brushIcon = buttonIcon(app, QStringLiteral("tool_brush"), QStringLiteral("tools/brush"));
    auto* textButton = item(app, QStringLiteral("tool_text"));
    auto* unavailableIcon = buttonIcon(app, QStringLiteral("tool_text"), QStringLiteral("tools/text"));
    QVERIFY(penIcon && brushIcon && textButton && unavailableIcon);
    textButton->setProperty("available", false);
    QCoreApplication::processEvents();

    const QColor darkNormal = theme->property("iconNormal").value<QColor>();
    const QColor darkHover = theme->property("iconHover").value<QColor>();
    const QColor darkSelected = theme->property("iconSelected").value<QColor>();
    const QColor darkDisabled = theme->property("iconDisabled").value<QColor>();

    // Dark theme: a soft, slightly muted light grey — not white.
    QVERIFY(darkNormal.lightness() > 120 && darkNormal.lightness() < 240);
    QVERIFY(darkHover.lightness() > darkNormal.lightness());
    QVERIFY(darkHover != QColor(Qt::white) && darkHover != QColor("#ffffff"));
    QVERIFY(darkSelected != darkNormal);                 // the accent treatment
    QVERIFY(darkDisabled.lightness() < darkNormal.lightness());
    QVERIFY(darkDisabled.lightness() > 30);              // subdued, not invisible

    // Contrast against the surfaces the icons are drawn on: the normal and
    // hover tints are comfortable on the panels, the selected tint is readable
    // on its own selected background (the two nearest blue cells), and the
    // disabled tint stays deliberately quiet.
    const QColor paneBg = theme->property("paneBg").value<QColor>();
    QVERIFY(contrast(darkNormal, paneBg) > 4.5);
    QVERIFY(contrast(darkHover, paneBg) > 6.0);
    QVERIFY(contrast(darkSelected, theme->property("toolActive").value<QColor>()) > 4.5);
    QVERIFY(contrast(darkDisabled, paneBg) > 1.5);
    QVERIFY(contrast(darkDisabled, paneBg) < 3.5);

    // The palette draws each state with the right tint.
    QCOMPARE(shownColour(penIcon), darkNormal);
    QCOMPARE(shownColour(brushIcon), darkSelected);      // brush is the current tool
    QCOMPARE(shownColour(unavailableIcon), darkDisabled);
    textButton->setProperty("available", true);
    QCoreApplication::processEvents();

    // Hover is applied by the same button, then taken away again.
    QTest::mouseMove(app.window, centre(item(app, QStringLiteral("tool_pen"))));
    QCoreApplication::processEvents();
    QTRY_COMPARE(shownColour(penIcon), darkHover);
    QTest::mouseMove(app.window, centre(item(app, QStringLiteral("canvasViewport"))));
    QTRY_COMPARE(shownColour(penIcon), darkNormal);

    screenshot(app, QStringLiteral("07-main-dark"));

    // Switch the theme: every icon re-tints without touching the icons.
    theme->setProperty("mode", QStringLiteral("light"));
    QTRY_COMPARE(theme->property("mode").toString(), QStringLiteral("light"));
    const QColor lightNormal = theme->property("iconNormal").value<QColor>();
    const QColor lightHover = theme->property("iconHover").value<QColor>();
    QTRY_COMPARE(shownColour(penIcon), lightNormal);
    QCOMPARE(shownColour(brushIcon), theme->property("iconSelected").value<QColor>());

    // Light theme: dark grey, not black; hover moderately darker; selected is
    // the accent itself; disabled a muted light grey.
    QVERIFY(lightNormal.lightness() > 20 && lightNormal.lightness() < 128);
    QVERIFY(lightNormal != QColor(Qt::black) && lightNormal != QColor("#000000"));
    QVERIFY(lightHover.lightness() < lightNormal.lightness());
    QCOMPARE(theme->property("iconSelected").value<QColor>(), theme->property("accent").value<QColor>());
    QVERIFY(theme->property("iconDisabled").value<QColor>().lightness() > lightNormal.lightness());

    // The same contrast checks hold in the light theme.
    const QColor lightPaneBg = theme->property("paneBg").value<QColor>();
    const QColor lightSelected = theme->property("iconSelected").value<QColor>();
    const QColor lightDisabled = theme->property("iconDisabled").value<QColor>();
    QVERIFY(contrast(lightNormal, lightPaneBg) > 4.5);
    QVERIFY(contrast(lightHover, lightPaneBg) > 6.0);
    QVERIFY(contrast(lightSelected, theme->property("toolActive").value<QColor>()) > 4.5);
    QVERIFY(contrast(lightDisabled, lightPaneBg) > 1.5);
    QVERIFY(contrast(lightDisabled, lightPaneBg) < 3.5);

    // The window itself is on the light palette now, not just the icons.
    QCOMPARE(app.window->property("color").value<QColor>(), theme->property("windowBg").value<QColor>());
    screenshot(app, QStringLiteral("08-main-light"));

    theme->setProperty("mode", QStringLiteral("dark"));
    app.settings->setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

// Preferences: the theme switch and the four icon colours are stored, applied
// to the live icons, and come back on the next start.
void TestBrushUi::preferencesDialogStoresThemeAndIconColours()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiPreferences")));
    app.settings->setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    for (const char* key : {"normal", "hover", "selected", "disabled"})
        app.settings->setValue(QStringLiteral("ui/icons/") + QLatin1String(key), QString());

    auto* theme = themeOf(app);
    QVERIFY(theme);
    theme->setProperty("mode", QStringLiteral("dark"));

    // An icon that is on screen the whole time: what the window actually draws.
    auto* penIcon = buttonIcon(app, QStringLiteral("tool_pen"), QStringLiteral("tools/pen"));
    QVERIFY(penIcon);
    const QColor darkNormal = theme->property("iconNormal").value<QColor>();
    const QColor darkHover = theme->property("iconHover").value<QColor>();
    QCOMPARE(shownColour(penIcon), darkNormal);
    QVERIFY(darkNormal != darkHover);

    auto* dialog = app.root->findChild<QObject*>(QStringLiteral("preferencesDialog"));
    QVERIFY(dialog);
    QVERIFY(!dialog->property("opened").toBool());
    dialog->setProperty("visible", true);
    QTRY_VERIFY(dialog->property("opened").toBool());
    QTest::qWait(50);

    auto* choice = item(app, QStringLiteral("themeChoice"));
    QVERIFY(choice && choice->isVisible() && choice->width() > 100);
    screenshot(app, QStringLiteral("09-preferences-dark"));

    // Pick "Light" (the right-hand segment).
    const QPoint lightAt = choice->mapToScene(QPointF(choice->width() * 0.75, choice->height() / 2)).toPoint();
    QTest::mouseMove(app.window, lightAt);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, lightAt);
    QTRY_COMPARE(theme->property("mode").toString(), QStringLiteral("light"));
    QTRY_COMPARE(app.settings->value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    const QColor lightNormal = theme->property("iconNormal").value<QColor>();
    QVERIFY(lightNormal != darkNormal);
    // The visible icon re-tinted with the window; nothing was reopened.
    QTRY_COMPARE(shownColour(penIcon), lightNormal);
    QTest::qWait(50);
    screenshot(app, QStringLiteral("10-preferences-light"));

    // The dialog's own colour path (what "Change..." calls on accept) re-tints
    // icons that are already on screen, immediately.
    const QColor custom(QStringLiteral("#cc8844"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "setColour",
                                      Q_ARG(QVariant, QVariant(QStringLiteral("Normal"))),
                                      Q_ARG(QVariant, QVariant(QStringLiteral("#cc8844")))));
    QTRY_COMPARE(theme->property("iconNormalPref").toString(), QStringLiteral("#cc8844"));
    QTRY_COMPARE(shownColour(penIcon), custom);
    QCOMPARE(app.settings->value(QStringLiteral("ui/icons/normal")).toString(), QStringLiteral("#cc8844"));
    QVERIFY(theme->property("iconNormalCustom").toBool());

    // Reset on that row restores the colour of the theme that is active now.
    QVERIFY(clickNamed(app, QStringLiteral("iconColourNormalReset")));
    QTRY_COMPARE(theme->property("iconNormalPref").toString(), QString());
    QTRY_COMPARE(app.settings->value(QStringLiteral("ui/icons/normal")).toString(), QString());
    QVERIFY(!theme->property("iconNormalCustom").toBool());
    QTRY_COMPARE(shownColour(penIcon), lightNormal);

    // An override survives a theme switch (it is the user's choice, not a
    // per-theme value), and only Reset returns to the defaults.
    const QColor customHover(QStringLiteral("#7a4bd0"));
    theme->setProperty("iconHoverPref", customHover.name());
    QTRY_COMPARE(theme->property("iconHover").value<QColor>(), customHover);
    theme->setProperty("mode", QStringLiteral("dark"));
    QTRY_COMPARE(theme->property("mode").toString(), QStringLiteral("dark"));
    QCOMPARE(theme->property("iconHover").value<QColor>(), customHover);
    const QColor darkHoverDefault = theme->property("iconHoverDefault").value<QColor>();
    QVERIFY(customHover != darkHoverDefault);
    theme->setProperty("iconHoverPref", QString());
    QTRY_COMPARE(theme->property("iconHover").value<QColor>(), darkHoverDefault);
    // ... and after a reset it follows whichever theme is active.
    theme->setProperty("mode", QStringLiteral("light"));
    QTRY_COMPARE(theme->property("iconHover").value<QColor>(),
                 theme->property("iconHoverDefault").value<QColor>());
    QTRY_VERIFY(theme->property("iconHover").value<QColor>() != darkHoverDefault);
    theme->setProperty("mode", QStringLiteral("dark"));

    // Reset all clears every override at once.
    theme->setProperty("iconHoverPref", QStringLiteral("#123456"));
    theme->setProperty("iconDisabledPref", QStringLiteral("#654321"));
    QVERIFY(item(app, QStringLiteral("resetIconColoursButton"))->isEnabled());
    QVERIFY(clickNamed(app, QStringLiteral("resetIconColoursButton")));
    QTRY_VERIFY(!theme->property("iconColoursCustom").toBool());
    QCOMPARE(app.settings->value(QStringLiteral("ui/icons/hover")).toString(), QString());

    QVERIFY(clickNamed(app, QStringLiteral("closePreferencesButton")));
    QTRY_VERIFY(!dialog->property("opened").toBool());

    // A new window in this session starts on the stored theme.
    theme->setProperty("mode", QStringLiteral("light"));
    QTRY_COMPARE(app.settings->value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    app.root.reset();
    app.window = nullptr;
    {
        Running second;
        QVERIFY(start(second, QStringLiteral("BrushUiPreferences")));
        auto* restored = themeOf(second);
        QVERIFY(restored);
        QCOMPARE(restored->property("mode").toString(), QStringLiteral("light"));
        QCOMPARE(second.window->property("color").value<QColor>(),
                 restored->property("windowBg").value<QColor>());
        QTest::qWait(50);
        screenshot(second, QStringLiteral("11-main-light-restored"));
        restored->setProperty("mode", QStringLiteral("dark"));
        second.settings->setValue(QStringLiteral("ui/theme"), QStringLiteral("dark"));
    }
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

void TestBrushUi::komaSettingsApplyAndCancelPersistGlobalValues()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiKomaSettings")));
    auto* dialog = app.root->findChild<QObject*>(QStringLiteral("komaSettingsDialog"));
    QVERIFY(dialog);
    dialog->setProperty("visible", true);
    QTRY_VERIFY(dialog->property("opened").toBool());

    for (const QString& category : {QStringLiteral("general"), QStringLiteral("interface"),
                                     QStringLiteral("canvas"), QStringLiteral("brushEngine"),
                                     QStringLiteral("tabletInput"), QStringLiteral("shortcuts"),
                                     QStringLiteral("performance")}) {
        QVERIFY2(app.root->findChild<QObject*>(QStringLiteral("komaSettingsPage_") + category),
                 qPrintable(category));
    }
    auto draft = dialog->property("draft").toMap();
    QCOMPARE(draft.value(QStringLiteral("brushEngine")).toString(), QStringLiteral("digital"));
    QCOMPARE(draft.value(QStringLiteral("realisticEngineEnabled")).toBool(), false);
    draft.insert(QStringLiteral("theme"), QStringLiteral("light"));
    draft.insert(QStringLiteral("defaultTool"), QStringLiteral("pen"));
    draft.insert(QStringLiteral("realisticEngineEnabled"), true);
    draft.insert(QStringLiteral("brushEngine"), QStringLiteral("realistic"));
    draft.insert(QStringLiteral("recoveryInterval"), 15);
    dialog->setProperty("draft", draft);
    auto* apply = item(app, QStringLiteral("komaSettingsApply"));
    QVERIFY(apply && apply->isVisible() && apply->isEnabled());
    click(app, apply);
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(app.settings->value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    QCOMPARE(app.settings->value(QStringLiteral("tools/defaultTool")).toString(), QStringLiteral("pen"));
    QCOMPARE(app.settings->value(QStringLiteral("tools/realisticEngineEnabled")).toBool(), true);
    QCOMPARE(app.settings->value(QStringLiteral("tools/brushEngine")).toString(), QStringLiteral("realistic"));
    QCOMPARE(app.settings->value(QStringLiteral("recovery/intervalMinutes")).toInt(), 15);
    QCOMPARE(app.workspace.defaultTool(), QStringLiteral("pen"));
    QCOMPARE(app.workspace.brushEngine(), QStringLiteral("realistic"));
    QVERIFY(app.workspace.isRealisticEngineEnabled());
    // The active document's tool is project-specific and is not overwritten.
    QCOMPARE(app.workspace.activeTool(), QStringLiteral("brush"));
    QCOMPARE(themeOf(app)->property("mode").toString(), QStringLiteral("light"));
    QVERIFY(app.workspace.newProject(QStringLiteral("Default tool check"), 64, 64, true,
                                    QStringLiteral("white"), 72));
    QCOMPARE(app.workspace.activeTool(), QStringLiteral("pen"));

    dialog->setProperty("visible", true);
    QTRY_VERIFY(dialog->property("opened").toBool());
    draft = dialog->property("draft").toMap();
    draft.insert(QStringLiteral("theme"), QStringLiteral("dark"));
    draft.insert(QStringLiteral("brushEngine"), QStringLiteral("digital"));
    dialog->setProperty("draft", draft);
    auto* cancel = item(app, QStringLiteral("komaSettingsCancel"));
    QVERIFY(cancel && cancel->isVisible());
    click(app, cancel);
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(app.settings->value(QStringLiteral("ui/theme")).toString(), QStringLiteral("light"));
    QCOMPARE(app.settings->value(QStringLiteral("tools/brushEngine")).toString(), QStringLiteral("realistic"));
    QCOMPARE(app.workspace.brushEngine(), QStringLiteral("realistic"));
    QCOMPARE(themeOf(app)->property("mode").toString(), QStringLiteral("light"));
    app.settings->setValue(QStringLiteral("tools/brushEngine"), QStringLiteral("digital"));
    app.settings->setValue(QStringLiteral("tools/realisticEngineEnabled"), false);

    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

void TestBrushUi::brushEngineSettingsPersistAcrossRestartsAndStayNonDestructive()
{
    {
        Settings fresh(QStringLiteral("KomaPaintTests"), QStringLiteral("BrushUiEngineRestart"));
        fresh.setValue(QStringLiteral("tools/realisticEngineEnabled"), false);
        fresh.setValue(QStringLiteral("tools/brushEngine"), QStringLiteral("digital"));
        fresh.sync();
    }
    {
        Running app;
        QVERIFY(start(app, QStringLiteral("BrushUiEngineRestart")));
        app.workspace.setRealisticEngineEnabled(
            app.settings->value(QStringLiteral("tools/realisticEngineEnabled"), false).toBool());
        app.workspace.setBrushEngine(app.settings->value(QStringLiteral("tools/brushEngine"),
                                                          QStringLiteral("digital")).toString());
        QCOMPARE(app.workspace.brushEngine(), QStringLiteral("digital"));
        QVERIFY(!app.workspace.isRealisticEngineEnabled());
        auto* statusEngine = item(app, QStringLiteral("statusBarBrushEngine"));
        QVERIFY(statusEngine);
        QCOMPARE(statusEngine->property("text").toString(), QStringLiteral("Digital"));

        // Color Wheel and Paint Mixer remain available in Digital mode.
        auto* colorPanelTabMixer = item(app, QStringLiteral("colorPanelTab_mixer"));
        QVERIFY(colorPanelTabMixer && colorPanelTabMixer->isVisible());
        click(app, colorPanelTabMixer);
        auto* mixerNotice = item(app, QStringLiteral("mixerEngineNotice"));
        QVERIFY(mixerNotice && mixerNotice->isVisible());

        // Opening Koma Settings on the Brush Engine page via Workspace (no longer inside Tool Properties).
        app.workspace.openBrushEngineSettings();

        auto* dialog = app.root->findChild<QObject*>(QStringLiteral("komaSettingsDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        // Attempting to select Realistic before enabling it keeps draft on Digital.
        auto* realisticOpt = item(app, QStringLiteral("brushEngineOption_realistic"));
        QVERIFY(realisticOpt && realisticOpt->isVisible());
        click(app, realisticOpt);
        QCOMPARE(dialog->property("draft").toMap().value(QStringLiteral("brushEngine")).toString(),
                 QStringLiteral("digital"));

        // Enable Realistic first, then select Realistic and apply.
        auto* enableToggle = item(app, QStringLiteral("realisticEngineEnableToggle"));
        QVERIFY(enableToggle && enableToggle->isVisible());
        click(app, enableToggle);
        click(app, realisticOpt);
        auto* apply = item(app, QStringLiteral("komaSettingsApply"));
        QVERIFY(apply);
        click(app, apply);
        QTRY_VERIFY(!dialog->property("opened").toBool());

        QVERIFY(app.workspace.isRealisticEngineEnabled());
        QCOMPARE(app.workspace.brushEngine(), QStringLiteral("realistic"));
        QCOMPARE(statusEngine->property("text").toString(), QStringLiteral("Realistic"));
        QCOMPARE(app.settings->value(QStringLiteral("tools/realisticEngineEnabled")).toBool(), true);
        QCOMPARE(app.settings->value(QStringLiteral("tools/brushEngine")).toString(), QStringLiteral("realistic"));

        // Disabling Realistic while Realistic is active is blocked until returning to Digital.
        app.workspace.openBrushEngineSettings();
        QTRY_VERIFY(dialog->property("opened").toBool());
        click(app, enableToggle);
        QCOMPARE(dialog->property("draft").toMap().value(QStringLiteral("realisticEngineEnabled")).toBool(), true);
        // Returning to Digital first allows disabling Realistic cleanly.
        auto* digitalOpt = item(app, QStringLiteral("brushEngineOption_digital"));
        QVERIFY(digitalOpt && digitalOpt->isVisible());
        click(app, digitalOpt);
        click(app, enableToggle);
        QCOMPARE(dialog->property("draft").toMap().value(QStringLiteral("realisticEngineEnabled")).toBool(), false);
        // Re-enable & select Realistic for the restart test below.
        click(app, enableToggle);
        click(app, realisticOpt);
        click(app, apply);
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
        app.root.reset();
        app.window = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    // Simulate application restart reading persisted Settings.
    {
        Running restarted;
        QVERIFY(start(restarted, QStringLiteral("BrushUiEngineRestart")));
        restarted.workspace.setRealisticEngineEnabled(
            restarted.settings->value(QStringLiteral("tools/realisticEngineEnabled"), false).toBool());
        restarted.workspace.setBrushEngine(restarted.settings->value(QStringLiteral("tools/brushEngine"),
                                                                     QStringLiteral("digital")).toString());
        QCOMPARE(restarted.workspace.brushEngine(), QStringLiteral("realistic"));
        QVERIFY(restarted.workspace.isRealisticEngineEnabled());
        auto* statusEngine = item(restarted, QStringLiteral("statusBarBrushEngine"));
        QVERIFY(statusEngine);
        QCOMPARE(statusEngine->property("text").toString(), QStringLiteral("Realistic"));
        auto* paperSection = item(restarted, QStringLiteral("paperSurfaceSection"));
        QVERIFY(paperSection && paperSection->isVisible());

        // Restore Defaults resets draft brushEngine back to Digital and locks Realistic.
        restarted.workspace.openBrushEngineSettings();
        auto* dialog = restarted.root->findChild<QObject*>(QStringLiteral("komaSettingsDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto* defaultsBtn = item(restarted, QStringLiteral("komaSettingsDefaults"));
        QVERIFY(defaultsBtn);
        click(restarted, defaultsBtn);
        auto* apply = item(restarted, QStringLiteral("komaSettingsApply"));
        QVERIFY(apply);
        click(restarted, apply);
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QCOMPARE(restarted.workspace.brushEngine(), QStringLiteral("digital"));
        QVERIFY(!restarted.workspace.isRealisticEngineEnabled());
        QCOMPARE(restarted.settings->value(QStringLiteral("tools/brushEngine")).toString(), QStringLiteral("digital"));
        QCOMPARE(restarted.settings->value(QStringLiteral("tools/realisticEngineEnabled")).toBool(), false);
        QVERIFY2(restarted.warnings.isEmpty(), qPrintable(restarted.warnings.join(QLatin1Char('\n'))));
    }
}

void TestBrushUi::shortcutsAndPerformanceSettingsPersistAndResolveConflicts()
{
    {
        Settings fresh(QStringLiteral("KomaPaintTests"), QStringLiteral("BrushUiShortcutsPerf"));
        fresh.setValue(QStringLiteral("shortcuts/tool.pen"), QStringLiteral("P"));
        fresh.setValue(QStringLiteral("performance/tileCacheLimitMB"), 256);
        fresh.setValue(QStringLiteral("performance/undoHistoryLimit"), 60);
        fresh.setValue(QStringLiteral("performance/wetSimEnabled"), true);
        fresh.setValue(QStringLiteral("performance/wetSimIntervalMs"), 200);
        fresh.sync();
    }
    {
        Running app;
        QVERIFY(start(app, QStringLiteral("BrushUiShortcutsPerf")));
        auto* shortcuts = app.workspace.shortcuts();
        auto* perf = app.workspace.performanceSettings();
        QVERIFY(shortcuts && perf);
        QCOMPARE(shortcuts->keyFor(QStringLiteral("tool.pen")), QStringLiteral("P"));
        QCOMPARE(perf->tileCacheLimitMB(), 256);
        // The old forced default of 60 steps was an arbitrary cap and migrates
        // once to 0, which means "as long as memory allows".
        QCOMPARE(perf->undoHistoryLimit(), 0);
        QVERIFY(perf->wetSimEnabled());
        QCOMPARE(perf->wetSimIntervalMs(), 200);

        // Open Koma Settings on Shortcuts page, verify conflict detection, then apply custom binding.
        QVERIFY(QMetaObject::invokeMethod(app.root->findChild<QObject*>(QStringLiteral("komaSettingsDialog")),
                                          "openCategory", Q_ARG(QVariant, QStringLiteral("shortcuts"))));
        auto* dialog = app.root->findChild<QObject*>(QStringLiteral("komaSettingsDialog"));
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());

        // Attempting to bind tool.pen to "B" detects conflict with Brush Tool.
        QVERIFY(!shortcuts->findConflict(QStringLiteral("tool.pen"), QStringLiteral("B")).isEmpty());

        QVariantMap draft = dialog->property("draft").toMap();
        QVariantMap scMap = draft.value(QStringLiteral("shortcuts")).toMap();
        scMap.insert(QStringLiteral("tool.pen"), QStringLiteral("Shift+P"));
        draft.insert(QStringLiteral("shortcuts"), scMap);

        QVariantMap perfMap = draft.value(QStringLiteral("performance")).toMap();
        perfMap.insert(QStringLiteral("tileCacheLimitMB"), 512);
        perfMap.insert(QStringLiteral("undoHistoryLimit"), 90);
        perfMap.insert(QStringLiteral("wetSimEnabled"), false);
        perfMap.insert(QStringLiteral("wetSimIntervalMs"), 450);
        draft.insert(QStringLiteral("performance"), perfMap);
        dialog->setProperty("draft", draft);

        click(app, item(app, QStringLiteral("komaSettingsApply")));
        QTRY_VERIFY(!dialog->property("opened").toBool());

        QCOMPARE(shortcuts->keyFor(QStringLiteral("tool.pen")), QStringLiteral("Shift+P"));
        QCOMPARE(perf->tileCacheLimitMB(), 512);
        QCOMPARE(perf->undoHistoryLimit(), 90);
        QVERIFY(!perf->wetSimEnabled());
        QCOMPARE(perf->wetSimIntervalMs(), 450);

        // Live MainWindow Shortcut reacts to the customized sequence.
        QTest::keyClick(app.window, Qt::Key_P, Qt::ShiftModifier);
        QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("pen"));

        // Verify ToolPropertyPanel is now docked on the right side by default, SubTool on left.
        auto* dock = app.panels.dockModel();
        QCOMPARE(dock->areaSideOf(dock->groupOf(QStringLiteral("subtool"))), QStringLiteral("left"));
        QCOMPARE(dock->areaSideOf(dock->groupOf(QStringLiteral("toolproperty"))), QStringLiteral("right"));
        QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
        app.root.reset();
        app.window = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    // Restart and verify both Shortcuts and Performance settings survived persistence.
    {
        Running restarted;
        QVERIFY(start(restarted, QStringLiteral("BrushUiShortcutsPerf")));
        QCOMPARE(restarted.workspace.shortcuts()->keyFor(QStringLiteral("tool.pen")), QStringLiteral("Shift+P"));
        QCOMPARE(restarted.workspace.performanceSettings()->tileCacheLimitMB(), 512);
        QCOMPARE(restarted.workspace.performanceSettings()->undoHistoryLimit(), 90);
        QVERIFY(!restarted.workspace.performanceSettings()->wetSimEnabled());
        QCOMPARE(restarted.workspace.performanceSettings()->wetSimIntervalMs(), 450);
        restarted.workspace.shortcuts()->restoreDefaults();
        restarted.workspace.performanceSettings()->restoreDefaults();
        QCOMPARE(restarted.workspace.shortcuts()->keyFor(QStringLiteral("tool.pen")), QStringLiteral("P"));
        QVERIFY2(restarted.warnings.isEmpty(), qPrintable(restarted.warnings.join(QLatin1Char('\n'))));
    }
}


// Clip Studio Paint behaviour: the same key selects the family first, then walks
// through it. This drives the real window so the QML Shortcut wiring, the
// ShortcutManager table and WorkspaceState all take part.
void TestBrushUi::toolShortcutsCycleThroughTheirFamilies()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiToolCycle")));
    app.workspace.setActiveTool(QStringLiteral("eraser"));

    // B enters the brush family at its first member...
    QTest::keyClick(app.window, Qt::Key_B);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("brush"));
    // ...and a second press moves to the airbrush, then wraps back.
    QTest::keyClick(app.window, Qt::Key_B);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("airbrush"));
    QTest::keyClick(app.window, Qt::Key_B);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("brush"));

    // P cycles Pen <-> Pencil.
    QTest::keyClick(app.window, Qt::Key_P);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("pen"));
    QTest::keyClick(app.window, Qt::Key_P);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("pencil"));

    // U cycles the figure family.
    QTest::keyClick(app.window, Qt::Key_U);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("rectangle"));
    QTest::keyClick(app.window, Qt::Key_U);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("circle"));

    // A key without a family still selects exactly its tool.
    QTest::keyClick(app.window, Qt::Key_E);
    QTRY_COMPARE(app.workspace.activeTool(), QStringLiteral("eraser"));
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

void TestBrushUi::brushLibraryTabsAndPresetsPersistAcrossRestartsAndProjects()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sessionPath = dir.filePath(QStringLiteral("session.json"));
    const QString libraryPath = dir.filePath(QStringLiteral("brush-library.json"));

    QString customDigitalTabId;
    QString customDigitalBrushId;
    QString movedBrushId;
    QString copiedBrushId;
    QString realisticCustomTabId;
    QString realisticBrushId;
    QVariantMap storedMixture;

    {
        Running app;
        QVERIFY(start(app, QStringLiteral("BrushLibraryPersist")));
        QVERIFY(app.workspace.initializeSession(sessionPath));
        auto* lib = app.workspace.subTools();
        auto* brush = app.workspace.brush();

        // Digital engine starts active; strokes never allocate physical material tiles.
        QCOMPARE(app.workspace.brushEngine(), QStringLiteral("digital"));
        drag(app, {onCanvas(app, 180, 180), onCanvas(app, 230, 180)});
        QCOMPARE(app.workspace.layers()->layerAt(0)->materialTiles().allocatedTiles(), std::size_t(0));

        // Create multiple user tabs, rename, reorder, and set default tab.
        customDigitalTabId = lib->createTab(QStringLiteral("Linework Pro"));
        QVERIFY(!customDigitalTabId.isEmpty());
        QVERIFY(lib->renameTab(customDigitalTabId, QStringLiteral("Comics Ink")));
        QCOMPARE(lib->currentTabName(), QStringLiteral("Comics Ink"));
        const QString tempTabId = lib->createTab(QStringLiteral("Temp Tab"));
        QVERIFY(!tempTabId.isEmpty());

        // UI deliberate confirmation modal deletes Temp Tab cleanly.
        auto* subPanel = item(app, QStringLiteral("subToolPanel"));
        QVERIFY(subPanel);
        QTest::qWait(50);
        QCoreApplication::processEvents();
        QQuickItem* tabOptBtn = nullptr;
        QTRY_VERIFY((tabOptBtn = item(app, QStringLiteral("tabOptionsButton"))) != nullptr
                    && tabOptBtn->isVisible() && tabOptBtn->width() > 0 && tabOptBtn->height() > 0);
        QSignalSpy activatedSpy(tabOptBtn, SIGNAL(activated()));
        click(app, tabOptBtn);
        auto* tabMenu = subPanel->findChild<QObject*>(QStringLiteral("collectionTabMenu"));
        QVERIFY(tabMenu);
        auto* subQuick = qobject_cast<QQuickItem*>(subPanel);
        QTRY_VERIFY2(tabMenu->property("opened").toBool(),
                     qPrintable(QStringLiteral("sub=(%1,%2 %3x%4) canvasX=%5 tabOptBtn=(%6,%7 %8x%9 en=%10 act=%11) menu(vis=%12 open=%13)")
                         .arg(subQuick ? subQuick->mapToScene(QPointF(0, 0)).x() : -1)
                         .arg(subQuick ? subQuick->mapToScene(QPointF(0, 0)).y() : -1)
                         .arg(subQuick ? subQuick->width() : -1)
                         .arg(subQuick ? subQuick->height() : -1)
                         .arg(app.canvasItem->mapToScene(QPointF(0, 0)).x())
                         .arg(tabOptBtn->mapToScene(QPointF(0, 0)).x())
                         .arg(tabOptBtn->mapToScene(QPointF(0, 0)).y())
                         .arg(tabOptBtn->width())
                         .arg(tabOptBtn->height())
                         .arg(tabOptBtn->isEnabled())
                         .arg(activatedSpy.count())
                         .arg(tabMenu->property("visible").toBool())
                         .arg(tabMenu->property("opened").toBool())));
        QQuickItem* deleteMenuItem = nullptr;
        QTRY_VERIFY2(
            ((deleteMenuItem = item(app, QStringLiteral("tabMenuDelete"))) != nullptr
             || (deleteMenuItem = tabMenu->findChild<QQuickItem*>(QStringLiteral("tabMenuDelete"))) != nullptr)
            && deleteMenuItem->isVisible() && deleteMenuItem->width() > 0 && deleteMenuItem->height() > 0,
            qPrintable(QStringLiteral("popupType=%1 found=%2 vis=%3 size=(%4,%5) sameWin=%6")
                .arg(tabMenu->property("popupType").toInt())
                .arg(deleteMenuItem != nullptr)
                .arg(deleteMenuItem ? deleteMenuItem->isVisible() : false)
                .arg(deleteMenuItem ? deleteMenuItem->width() : -1)
                .arg(deleteMenuItem ? deleteMenuItem->height() : -1)
                .arg(deleteMenuItem ? (deleteMenuItem->window() == app.window) : false)));
        if (deleteMenuItem->window() && deleteMenuItem->window() != app.window) {
            const QPoint at = deleteMenuItem->mapToScene(QPointF(deleteMenuItem->width() / 2, deleteMenuItem->height() / 2)).toPoint();
            QTest::mouseMove(deleteMenuItem->window(), at);
            QTest::mouseClick(deleteMenuItem->window(), Qt::LeftButton, Qt::NoModifier, at);
            QCoreApplication::processEvents();
        } else {
            click(app, deleteMenuItem);
        }
        auto* confirmDlg = subPanel->findChild<QObject*>(QStringLiteral("deleteTabConfirmDialog"));
        QVERIFY(confirmDlg);
        QTRY_VERIFY(confirmDlg->property("opened").toBool());
        click(app, item(app, QStringLiteral("deleteTabConfirmButton")));
        QTRY_VERIFY(!confirmDlg->property("opened").toBool());
        for (const auto& t : lib->engineTabs())
            QVERIFY(t.toMap().value(QStringLiteral("id")).toString() != tempTabId);

        // Move Comics Ink to index 0 and set as default opening tab.
        const int comicsIdx = lib->currentTabIndex();
        QVERIFY(comicsIdx > 0);
        QVERIFY(lib->moveTab(comicsIdx, 0));
        QVERIFY(lib->setDefaultTab(customDigitalTabId));
        QCOMPARE(lib->defaultTabId(), customDigitalTabId);

        // Create brush from current, customize independently, duplicate, move & copy.
        brush->setSize(27);
        brush->setTipStyle(QStringLiteral("graphite"));
        brush->setPressureSize(64);
        brush->setBlendStrength(0);
        customDigitalBrushId = lib->createBrushFromCurrent(QStringLiteral("My Pencil 2B"));
        QVERIFY(!customDigitalBrushId.isEmpty());
        const QString dupId = lib->duplicateBrushById(customDigitalBrushId);
        QVERIFY(!dupId.isEmpty());
        QVERIFY(lib->renameBrush(dupId, QStringLiteral("Soft Graphite Copy")));
        // Modifying duplicate does NOT overwrite My Pencil 2B.
        brush->setSize(52);
        lib->selectById(customDigitalBrushId);
        QCOMPARE(brush->size(), 27);

        // Move/Copy across tabs in Digital.
        movedBrushId = dupId;
        QVERIFY(lib->moveBrushToTab(movedBrushId, QStringLiteral("digital-sketching")));
        copiedBrushId = lib->copyBrushToTab(customDigitalBrushId, QStringLiteral("digital-default"));
        QVERIFY(!copiedBrushId.isEmpty());

        // Temporary search text must NOT be persisted in brush-library.json.
        lib->setSearchText(QStringLiteral("Pencil"));
        QCOMPARE(lib->searchText(), QStringLiteral("Pencil"));
        lib->setSearchText(QString());

        // Unlock & switch to Realistic: verify Paint Mixer mixture persists per brush
        // and Realistic strokes allocate material tiles without deleting Digital artwork.
        app.workspace.setRealisticEngineEnabled(true);
        app.workspace.setBrushEngine(QStringLiteral("realistic"));
        QCOMPARE(app.workspace.brushEngine(), QStringLiteral("realistic"));
        realisticCustomTabId = lib->createTab(QStringLiteral("Studio Gouache"));
        QVERIFY(!realisticCustomTabId.isEmpty());
        brush->setMediumId(QStringLiteral("gouache"));
        brush->setMaterialPresetId(QStringLiteral("gouache-round"));
        brush->setSize(22);
        QVERIFY(!app.workspace.mixer()->addPigmentAsNewDeposit(QStringLiteral("PB29"), 1.0).isEmpty());
        QVERIFY(app.workspace.mixer()->loadActiveIntoBrush());
        storedMixture = brush->materialMixture();
        QVERIFY(!storedMixture.isEmpty());
        realisticBrushId = lib->createBrushFromCurrent(QStringLiteral("Studio Cobalt"));
        QVERIFY(!realisticBrushId.isEmpty());

        // Second Realistic brush keeps its own independent empty/different mixture.
        const QString secondRealId = lib->createBrushFromCurrent(QStringLiteral("Studio Clean"));
        brush->clearMaterialMixture();
        QVERIFY(!brush->hasMaterialMixture());
        lib->selectById(realisticBrushId);
        QCOMPARE(brush->materialMixture(), storedMixture);

        // Realistic stroke allocates material tiles while previous Digital artwork remains.
        drag(app, {onCanvas(app, 280, 180), onCanvas(app, 330, 180)});
        QVERIFY(app.workspace.layers()->layerAt(0)->materialTiles().allocatedTiles() > 0);
        const auto digitalPx = app.workspace.layers()->layerAt(0)->tiles().pixel({200, 180});
        QVERIFY(digitalPx.has_value() && (*digitalPx >> 24) > 0);

        // Switching projects preserves brush collections and engine state.
        QVERIFY(app.workspace.newProject(QStringLiteral("Second Canvas"), 128, 128, true,
                                        QStringLiteral("white"), 72));
        QCOMPARE(lib->currentTabId(), realisticCustomTabId);
        QCOMPARE(app.workspace.brushEngine(), QStringLiteral("realistic"));
        QVERIFY(app.workspace.activateTab(0));
        QVERIFY(app.workspace.layers()->layerAt(0)->materialTiles().allocatedTiles() > 0);

        // Return to Digital before closing; confirm Realistic artwork stays intact.
        app.workspace.setBrushEngine(QStringLiteral("digital"));
        QVERIFY(app.workspace.layers()->layerAt(0)->materialTiles().allocatedTiles() > 0);
        QVERIFY(QFile::exists(libraryPath));
        QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
        app.root.reset();
        app.window = nullptr;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    // Restart: load brush-library.json and verify all tabs, default tab, brushes,
    // and per-brush materialMixture survived without searchText leakage.
    {
        Running restarted;
        QVERIFY(start(restarted, QStringLiteral("BrushLibraryPersist")));
        QVERIFY(restarted.workspace.initializeSession(sessionPath));
        auto* lib = restarted.workspace.subTools();
        auto* brush = restarted.workspace.brush();

        QVERIFY(lib->searchText().isEmpty());
        QCOMPARE(lib->defaultTabId(), customDigitalTabId);
        QCOMPARE(lib->currentTabId(), customDigitalTabId);
        QCOMPARE(lib->currentTabName(), QStringLiteral("Comics Ink"));
        QCOMPARE(lib->engineTabs().first().toMap().value(QStringLiteral("id")).toString(),
                 customDigitalTabId);
        lib->selectById(customDigitalBrushId);
        QCOMPARE(brush->size(), 27);
        QCOMPARE(brush->tipStyle(), QStringLiteral("graphite"));
        QCOMPARE(brush->pressureSize(), 64);

        lib->selectTabById(QStringLiteral("digital-sketching"));
        lib->selectById(movedBrushId);
        QCOMPARE(lib->currentName(), QStringLiteral("Soft Graphite Copy"));
        QCOMPARE(brush->size(), 52);

        restarted.workspace.setRealisticEngineEnabled(true);
        restarted.workspace.setBrushEngine(QStringLiteral("realistic"));
        lib->selectTabById(realisticCustomTabId);
        lib->selectById(realisticBrushId);
        QCOMPARE(brush->materialMixture(), storedMixture);

        // Deleting last remaining tab in a collection is safely refused.
        while (lib->engineTabs().size() > 1) {
            const QString id = lib->engineTabs().last().toMap().value(QStringLiteral("id")).toString();
            QVERIFY(lib->deleteTab(id));
        }
        const QString lastId = lib->engineTabs().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!lib->deleteTab(lastId));
        QVERIFY(!lib->lastError().isEmpty());
        QVERIFY2(restarted.warnings.isEmpty(), qPrintable(restarted.warnings.join(QLatin1Char('\n'))));
    }
}

void TestBrushUi::brushLibraryMigrationPreservesUserCollectionsAndUpgradesBuiltIns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("brush-library.json"));

    BrushLibraryDocument bundled;
    BrushEntryData existingBuiltIn;
    existingBuiltIn.id = QStringLiteral("builtin-round");
    existingBuiltIn.name = QStringLiteral("Round");
    existingBuiltIn.category = QStringLiteral("Paint");
    existingBuiltIn.builtIn = true;
    existingBuiltIn.defaults.size = 16;
    existingBuiltIn.current.size = 16;

    BrushEntryData userDeletedBuiltIn;
    userDeletedBuiltIn.id = QStringLiteral("builtin-old-removed");
    userDeletedBuiltIn.name = QStringLiteral("Removed");
    userDeletedBuiltIn.category = QStringLiteral("Paint");
    userDeletedBuiltIn.builtIn = true;

    BrushTabData defTab;
    defTab.id = QStringLiteral("digital-default");
    defTab.name = QStringLiteral("Default");
    defTab.currentBrushId = existingBuiltIn.id;
    defTab.brushes = {existingBuiltIn, userDeletedBuiltIn};
    bundled.digital.defaultTabId = defTab.id;
    bundled.digital.currentTabId = defTab.id;
    bundled.digital.tabs = {defTab};
    bundled.realistic.defaultTabId = QStringLiteral("real-default");
    bundled.realistic.currentTabId = QStringLiteral("real-default");
    BrushTabData rTab;
    rTab.id = QStringLiteral("real-default");
    rTab.name = QStringLiteral("Default");
    rTab.brushes = {existingBuiltIn};
    rTab.brushes[0].id = QStringLiteral("real-gouache");
    rTab.brushes[0].defaults.traditionalMedia = true;
    rTab.brushes[0].current.traditionalMedia = true;
    rTab.currentBrushId = QStringLiteral("real-gouache");
    bundled.realistic.tabs = {rTab};
    bundled.knownBuiltInIds = {QStringLiteral("builtin-round"), QStringLiteral("builtin-old-removed"),
                               QStringLiteral("real-gouache")};

    // User renamed Default -> My Brushes, edited Round size to 42, deleted builtin-old-removed,
    // and added a custom tab.
    BrushLibraryDocument userSaved = bundled;
    userSaved.digital.tabs[0].name = QStringLiteral("My Brushes");
    userSaved.digital.tabs[0].brushes[0].current.size = 42;
    userSaved.digital.tabs[0].brushes.removeAt(1);
    BrushTabData customTab;
    customTab.id = QStringLiteral("user-custom-tab");
    customTab.name = QStringLiteral("Custom Paints");
    userSaved.digital.tabs.append(customTab);
    QString err;
    QVERIFY2(BrushLibraryStore::save(path, userSaved, &err), qPrintable(err));

    // App upgrade adds a brand-new built-in brush.
    BrushEntryData newlyShipped = existingBuiltIn;
    newlyShipped.id = QStringLiteral("builtin-new-v2");
    newlyShipped.name = QStringLiteral("New V2 Rake");
    newlyShipped.defaults.tipStyle = QStringLiteral("bristle");
    newlyShipped.current.tipStyle = QStringLiteral("bristle");
    bundled.digital.tabs[0].brushes.append(newlyShipped);
    bundled.knownBuiltInIds.append(newlyShipped.id);

    BrushLibraryDocument migrated;
    QVERIFY2(BrushLibraryStore::load(path, bundled, &migrated, &err), qPrintable(err));
    QCOMPARE(migrated.digital.tabs.size(), 2);
    QCOMPARE(migrated.digital.tabs[0].name, QStringLiteral("My Brushes"));
    QCOMPARE(migrated.digital.tabs[1].name, QStringLiteral("Custom Paints"));
    QCOMPARE(migrated.digital.tabs[0].brushes.size(), 2); // Round (user edited) + New V2 Rake; deleted stays deleted!
    QCOMPARE(migrated.digital.tabs[0].brushes[0].current.size, 42);
    QCOMPARE(migrated.digital.tabs[0].brushes[1].id, QStringLiteral("builtin-new-v2"));
}

// ---------------------------------------------------------------- panel dock
// Panels live in the shared docking workspace now: closing one removes it from
// the layout (the canvas takes the room), reopening puts it back. Nothing a
// panel can do makes it unreachable, because View > Panels and Reset Layout are
// driven by the same model.
void TestBrushUi::brushPanelsCanBeClosedAndComeBack()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiDock")));
    QVERIFY(item(app, QStringLiteral("dockWorkspace")));
    QVERIFY(item(app, QStringLiteral("subToolPanel")));
    const qreal canvasLeft = app.canvasItem->mapToScene(QPointF(0, 0)).x();
    for (const QString& id : {QStringLiteral("subtool"), QStringLiteral("toolproperty"), QStringLiteral("brushsize")}) {
        QVERIFY(app.panels.isVisible(id));
        app.panels.setVisible(id, false);
        QVERIFY(!app.panels.isVisible(id));
    }
    // With the whole left column gone the canvas starts further left.
    QTRY_VERIFY(app.canvasItem->mapToScene(QPointF(0, 0)).x() < canvasLeft - 200);
    QTRY_VERIFY(item(app, QStringLiteral("subToolPanel")) == nullptr);

    app.panels.setVisible(QStringLiteral("brushsize"), true);
    QTRY_VERIFY(item(app, QStringLiteral("brushSizePanel")) != nullptr);
    QVERIFY(item(app, QStringLiteral("brushSizePanel"))->isVisible());
    QVERIFY(item(app, QStringLiteral("subToolPanel")) == nullptr);

    app.panels.resetLayout();
    QTRY_VERIFY(item(app, QStringLiteral("subToolPanel")) != nullptr);
    QTRY_VERIFY(item(app, QStringLiteral("toolPropertyPanel")) != nullptr);
    QTRY_VERIFY(qAbs(app.canvasItem->mapToScene(QPointF(0, 0)).x() - canvasLeft) < 2.0);
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

void TestBrushUi::stage11ToolsAndOverlaysWorkEndToEnd()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiStage11")));
    auto& ws = app.workspace;
    auto* layer = ws.layers()->layerAt(0);
    QVERIFY(layer);

    // 1. Rectangle shape tool draws a closed boundary on the layer
    QVERIFY(clickNamed(app, QStringLiteral("tool_rectangle")));
    QCOMPARE(ws.activeTool(), QStringLiteral("rectangle"));
    ws.colors()->setForeground(QColor(20, 20, 20));
    ws.toolSettings()->setShapeMode(QStringLiteral("stroke"));
    ws.toolSettings()->setShapeStrokeWidth(6);
    drag(app, {onCanvas(app, 120, 120), onCanvas(app, 320, 320)});
    QTRY_VERIFY(alpha(app, 120, 120) > 200);
    QCOMPARE(alpha(app, 220, 220), 0); // interior still transparent

    // 2. Smart Fill with previewBeforeCommit shows HUD, previews, then commits on button click
    QVERIFY(clickNamed(app, QStringLiteral("tool_fill")));
    ws.subTools()->select(2); // Smart Fill
    QCOMPARE(ws.toolSettings()->executableTool(), QStringLiteral("smart_fill"));
    QVERIFY(ws.toolSettings()->previewBeforeCommit());
    ws.colors()->setForeground(QColor(40, 160, 220));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 220, 220));
    QTRY_VERIFY(app.canvas->property("hasPendingRegionPreview").toBool());
    QTRY_VERIFY(item(app, QStringLiteral("regionPreviewHud"))->isVisible());
    QCOMPARE(alpha(app, 220, 220), 0); // not committed yet
    QVERIFY(clickNamed(app, QStringLiteral("commitRegionFillButton")));
    QTRY_VERIFY(!app.canvas->property("hasPendingRegionPreview").toBool());
    QTRY_VERIFY(alpha(app, 220, 220) > 200);
    QCOMPARE(alpha(app, 420, 420), 0); // exterior stayed empty

    // 3. Boundary-Aware Eraser protects the sampled interior while erasing exterior spill
    QVERIFY(clickNamed(app, QStringLiteral("tool_pen")));
    ws.colors()->setForeground(QColor(200, 60, 60));
    ws.brush()->setSize(20);
    drag(app, {onCanvas(app, 360, 220), onCanvas(app, 420, 220)});
    QTRY_VERIFY(alpha(app, 380, 220) > 200);

    QVERIFY(clickNamed(app, QStringLiteral("tool_eraser")));
    ws.subTools()->select(2); // Boundary-Aware Eraser
    QCOMPARE(ws.toolSettings()->executableTool(), QStringLiteral("boundary_eraser"));
    QTRY_VERIFY(item(app, QStringLiteral("boundaryEraserHud"))->isVisible());
    // Click inside filled interior to sample protected region
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 220, 220));
    QTRY_VERIFY(app.canvas->property("hasProtectedRegion").toBool());
    // Drag across interior and exterior spill: interior stays protected, spill erases
    ws.brush()->setSize(40);
    drag(app, {onCanvas(app, 220, 220), onCanvas(app, 380, 220)});
    QTRY_VERIFY(alpha(app, 220, 220) > 200);
    QTRY_COMPARE(alpha(app, 380, 220), 0);
    QVERIFY(clickNamed(app, QStringLiteral("clearProtectedRegionButton")));
    QTRY_VERIFY(!app.canvas->property("hasProtectedRegion").toBool());

    // 4. Selection marquee clips brush strokes strictly inside selection
    QVERIFY(clickNamed(app, QStringLiteral("tool_selection")));
    ws.toolSettings()->setSelectionShape(QStringLiteral("rectangle"));
    ws.toolSettings()->setSelectionOp(QStringLiteral("replace"));
    drag(app, {onCanvas(app, 450, 100), onCanvas(app, 550, 200)});
    QTRY_VERIFY(ws.hasSelection());
    QTRY_VERIFY(item(app, QStringLiteral("selectionActionHud"))->isVisible());

    QVERIFY(clickNamed(app, QStringLiteral("tool_brush")));
    ws.colors()->setForeground(QColor(30, 180, 60));
    ws.brush()->setSize(30);
    drag(app, {onCanvas(app, 420, 150), onCanvas(app, 580, 150)});
    QTRY_VERIFY(alpha(app, 500, 150) > 200); // inside selection
    QCOMPARE(alpha(app, 425, 150), 0);     // clipped outside selection
    ws.deselect();
    QVERIFY(!ws.hasSelection());

    // 5. Gradient, Text, and Move tools execute and support Undo
    QVERIFY(clickNamed(app, QStringLiteral("tool_gradient")));
    drag(app, {onCanvas(app, 460, 260), onCanvas(app, 560, 320)});
    QTRY_VERIFY(alpha(app, 480, 290) > 0);
    QVERIFY(ws.history()->canUndo());
    ws.history()->undo();
    QTRY_COMPARE(alpha(app, 480, 290), 0);

    QVERIFY(clickNamed(app, QStringLiteral("tool_text")));
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 120, 420));
    QTRY_VERIFY(app.canvas->property("textInputRequested").toBool());
    QVERIFY(QMetaObject::invokeMethod(app.canvas, "applyTextAt",
                                      Q_ARG(qreal, 120.0), Q_ARG(qreal, 420.0),
                                      Q_ARG(QString, QStringLiteral("Hi"))));
    QTRY_VERIFY(!app.canvas->property("textInputRequested").toBool());

    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

QTEST_MAIN(TestBrushUi)
#include "TestBrushUi.moc"
