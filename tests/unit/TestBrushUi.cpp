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
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <memory>

#include "app/BrushSettings.h"
#include "app/CanvasViewport.h"
#include "app/PanelManager.h"
#include "app/SubToolLibrary.h"
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

bool clickNamed(Running& app, const QString& name)
{
    auto* target = item(app, name);
    if (!target || target->width() <= 0 || !target->isVisible()) return false;
    click(app, target);
    return true;
}

// Click a PropertySlider's bar at `position` (0 = left end, 1 = right end).
bool setSlider(Running& app, const QString& name, double position)
{
    auto* slider = item(app, name);
    if (!slider || !slider->isVisible() || slider->width() <= 0) return false;
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
        QStringLiteral("PreferencesDialog.qml"), QStringLiteral("KomaTheme.qml"),
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
    void newProjectDialogShowsTheProjectTypeIcons();
    void iconsFollowTheTheme();
    void preferencesDialogStoresThemeAndIconColours();
    void brushPanelsCanBeClosedAndComeBack();
private:
    bool start(Running& app, const QString& name);
    void screenshot(Running& app, const QString& name);
};

void TestBrushUi::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
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
    g_warnings = &app.warnings;
    app.engine.addImportPath(QStringLiteral("qrc:/"));
    auto* context = app.engine.rootContext();
    context->setContextProperty(QStringLiteral("Settings"), app.settings.get());
    context->setContextProperty(QStringLiteral("Workspace"), &app.workspace);
    context->setContextProperty(QStringLiteral("NewProject"), &app.project);
    context->setContextProperty(QStringLiteral("App"), &app.config);
    context->setContextProperty(QStringLiteral("Panels"), &app.panels);
    QQmlComponent component(&app.engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    app.root.reset(component.create());
    if (!app.root) { qWarning() << component.errorString(); return false; }
    app.window = qobject_cast<QQuickWindow*>(app.root.get());
    if (!app.window) return false;
    app.window->show();
    if (!QTest::qWaitForWindowExposed(app.window)) return false;
    QTest::qWait(50);
    app.canvas = app.root->findChild<CanvasViewport*>(QStringLiteral("canvasViewport"));
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
    QVERIFY(!library.isPaintTool());

    // The first brush is the engine's default brush: nothing changes for a
    // workspace that starts on the brush tool.
    library.setTool(QStringLiteral("brush"));
    QVERIFY(library.isPaintTool());
    QCOMPARE(library.currentIndex(), 0);
    QCOMPARE(brush.size(), defaultSize);
    QCOMPARE(brush.spacing(), defaultSpacing);
    QCOMPARE(brush.opacity(), 100); QCOMPARE(brush.flow(), 100); QCOMPARE(brush.hardness(), 100);
    QCOMPARE(brush.stabilization(), 0); QVERIFY(!brush.textured()); QVERIFY(!brush.erase());
    QVERIFY(!library.isModified());

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

    // A tool that does not paint leaves the brush alone.
    library.setTool(QStringLiteral("fill"));
    QVERIFY(!library.isPaintTool());
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
    QList<QRectF> cells;
    const QStringList working = {"zoom", "hand", "eyedropper", "pen", "pencil", "brush", "airbrush", "eraser"};
    for (const QString& tool : kToolIcons) {
        auto* button = item(app, QStringLiteral("tool_") + tool);
        QVERIFY2(button, qPrintable(tool));
        QVERIFY2(button->isVisible(), qPrintable(tool));
        QCOMPARE(button->width(), 34.0); QCOMPARE(button->height(), 34.0);
        QCOMPARE(button->property("iconName").toString(), QStringLiteral("tools/") + tool);
        QCOMPARE(button->property("available").toBool(), working.contains(tool));
        const QRectF cell(button->mapToScene(QPointF(0, 0)), QSizeF(34, 34));
        for (const QRectF& other : cells) QVERIFY2(!cell.intersects(other), qPrintable(tool));
        cells << cell;
    }
    // Two columns.
    QSet<int> columns;
    for (const QRectF& cell : cells) columns << qRound(cell.x());
    QCOMPARE(columns.size(), 2);

    QVERIFY(clickNamed(app, QStringLiteral("tool_eraser")));
    QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    QVERIFY(ws.brush()->erase());
    QVERIFY(item(app, QStringLiteral("tool_eraser"))->property("selected").toBool());
    QVERIFY(!item(app, QStringLiteral("tool_brush"))->property("selected").toBool());

    // Tools that do nothing yet cannot be selected.
    for (const QString& tool : {QStringLiteral("fill"), QStringLiteral("text"), QStringLiteral("lasso"), QStringLiteral("gradient")}) {
        QVERIFY(clickNamed(app, QStringLiteral("tool_") + tool));
        QCOMPARE(ws.activeTool(), QStringLiteral("eraser"));
    }
    QVERIFY(clickNamed(app, QStringLiteral("tool_pen")));
    QCOMPARE(ws.activeTool(), QStringLiteral("pen"));
    QVERIFY(!ws.brush()->erase());

    // Tool keys.
    app.canvasItem->forceActiveFocus();
    QTest::keyClick(app.window, Qt::Key_E);
    QTRY_COMPARE(ws.activeTool(), QStringLiteral("eraser"));
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

    // The brush is no longer set up in the top toolbar.
    auto* toolbarButton = item(app, QStringLiteral("newProjectToolbarButton"));
    QVERIFY(toolbarButton);
    QQuickItem* toolbar = toolbarButton->parentItem();
    while (toolbar && toolbar->height() > 60) toolbar = toolbar->parentItem();
    QVERIFY(toolbar);
    QVERIFY(!hasClass(toolbar->parentItem(), "SpinBox"));
    QVERIFY(!hasClass(toolbar->parentItem(), "ComboBox"));

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
    QVERIFY(setSlider(app, QStringLiteral("brushHardnessSlider"), 0.0));
    QCOMPARE(brush->hardness(), 0);
    QVERIFY(setSlider(app, QStringLiteral("brushHardnessSlider"), 0.6));
    QVERIFY(qAbs(brush->hardness() - 60) <= 2);
    QVERIFY(setSlider(app, QStringLiteral("brushStabilizationSlider"), 1.0));
    QCOMPARE(brush->stabilization(), 100);
    QVERIFY(setSlider(app, QStringLiteral("brushSpacingSlider"), 1.0));
    QCOMPARE(brush->spacing(), int(BrushSettings::MaxSpacing));
    QVERIFY(setSlider(app, QStringLiteral("brushSpacingSlider"), 0.0));
    QCOMPARE(brush->spacing(), int(BrushSettings::MinSpacing));
    // Brush size: both ends of the bar are the engine's limits.
    QVERIFY(setSlider(app, QStringLiteral("brushSizeSlider"), 1.0));
    QCOMPARE(brush->size(), int(BrushSettings::MaxSize));
    QVERIFY(setSlider(app, QStringLiteral("brushSizeSlider"), 0.0));
    QCOMPARE(brush->size(), int(BrushSettings::MinSize));
    QVERIFY(setSlider(app, QStringLiteral("brushSizeSlider"), 0.5));
    QVERIFY(brush->size() > 40 && brush->size() < 90);                // the bar gives small sizes more room

    // Tip shape.
    auto* tip = item(app, QStringLiteral("brushTipChoice"));
    QVERIFY(tip && tip->isVisible());
    QVERIFY(!brush->textured());
    QPoint at = tip->mapToScene(QPointF(tip->width() * 0.75, tip->height() / 2)).toPoint();
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, at);
    QTRY_VERIFY(brush->textured());
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
    QVERIFY(setSlider(app, QStringLiteral("brushHardnessSlider"), 0.0));
    QCOMPARE(ws.brush()->hardness(), 0);
    QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier, onCanvas(app, 400, 300));
    QTest::qWait(20);
    QVERIFY(alpha(app, 400, 300) >= 245);
    const int softMiddle = alpha(app, 410, 300), softOuter = alpha(app, 416, 300);
    QVERIFY2(softMiddle > 60 && softMiddle < 200, qPrintable(QString::number(softMiddle)));
    QVERIFY2(softOuter > 0 && softOuter < softMiddle, qPrintable(QString::number(softOuter)));

    // Density 25 %: one dab deposits a quarter.
    QVERIFY(setSlider(app, QStringLiteral("brushHardnessSlider"), 1.0));
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
    auto* list = item(app, QStringLiteral("subToolList"));
    QVERIFY(list && list->isVisible());
    QCOMPARE(list->property("count").toInt(), 3);          // Round, Soft, Dry

    QVERIFY(clickNamed(app, QStringLiteral("subTool1")));  // Soft brush
    QCOMPARE(ws.subTools()->currentName(), QStringLiteral("Soft brush"));
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

// --------------------------------------------------------------- new project
void TestBrushUi::newProjectDialogShowsTheProjectTypeIcons()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiNewProject")));
    QVERIFY(clickNamed(app, QStringLiteral("newProjectToolbarButton")));
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
    auto* unavailableIcon = buttonIcon(app, QStringLiteral("tool_text"), QStringLiteral("tools/text"));
    QVERIFY(penIcon && brushIcon && unavailableIcon);

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
    QCOMPARE(shownColour(unavailableIcon), darkDisabled); // "Text" is not implemented yet

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

// ---------------------------------------------------------------- panel dock
void TestBrushUi::brushPanelsCanBeClosedAndComeBack()
{
    Running app;
    QVERIFY(start(app, QStringLiteral("BrushUiDock")));
    auto* dock = item(app, QStringLiteral("leftDock"));
    QVERIFY(dock && dock->isVisible());
    const qreal canvasWidth = app.canvasItem->width();
    for (const QString& id : {QStringLiteral("subtool"), QStringLiteral("toolproperty"), QStringLiteral("brushsize")}) {
        QVERIFY(app.panels.isVisible(id));
        app.panels.setVisible(id, false);
    }
    QTRY_VERIFY(!dock->isVisible());
    QTRY_VERIFY(app.canvasItem->width() > canvasWidth + 200);   // the canvas gets the room
    app.panels.setVisible(QStringLiteral("brushsize"), true);
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(item(app, QStringLiteral("brushSizePanel"))->isVisible());
    QVERIFY(!item(app, QStringLiteral("subToolPanel"))->isVisible());
    app.panels.resetLayout();
    QTRY_VERIFY(item(app, QStringLiteral("subToolPanel"))->isVisible());
    QTRY_VERIFY(item(app, QStringLiteral("toolPropertyPanel"))->isVisible());
    QVERIFY2(app.warnings.isEmpty(), qPrintable(app.warnings.join(QLatin1Char('\n'))));
}

QTEST_MAIN(TestBrushUi)
#include "TestBrushUi.moc"
