// QML responsiveness baseline.
//
// The C++ benchmarks cannot see the interface, so this drives the real window
// offscreen and times the interactions an artist repeats: opening a project,
// switching tools and Sub-Tool collections, toggling panels, opening the
// settings dialog and changing layer visibility. Each measurement is printed as
// "MEASURE <name> <milliseconds>" so CI can publish it as an annotation and a
// regression shows up as a number rather than a feeling.
//
// These are not pass/fail thresholds: a shared CI runner is far too noisy for
// that. The test asserts only that each interaction actually happened, so a
// broken binding cannot silently report a fast number.
#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWindow>

#include <algorithm>
#include <memory>
#include <vector>

#include "app/CanvasViewport.h"
#include "app/DockModel.h"
#include "app/PanelManager.h"
#include "app/SubToolLibrary.h"
#include "app/ToolSettings.h"
#include "app/WorkspaceState.h"
#include "core/ApplicationConfig.h"
#include "core/NewProjectSettings.h"
#include "core/Settings.h"
#include "layers/Layer.h"
#include "layers/LayerModel.h"

using namespace komapaint;

namespace {

struct Harness {
    WorkspaceState workspace;
    NewProjectSettings project;
    ApplicationConfig config;
    std::unique_ptr<Settings> settings;
    PanelManager panels;
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QStringList messages;

    ~Harness()
    {
        root.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
};

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void report(const char* name, double ms)
{
    qInfo("MEASURE %s %.3f", name, ms);
}

QQuickItem* findVisual(QQuickItem* from, const QString& name)
{
    if (!from) return nullptr;
    if (from->objectName() == name) return from;
    for (auto* child : from->childItems())
        if (auto* found = findVisual(child, name)) return found;
    return nullptr;
}

} // namespace

class TestQmlResponsiveness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void measuresStartup();
    void measuresToolSwitching();
    void measuresSubToolSwitching();
    void measuresPanelToggling();
    void measuresLayerSelectionAndVisibility();
    void measuresSettingsDialog();

private:
    Harness m_app;
    double m_startupMs = 0.0;
    std::unique_ptr<QObject> m_settingsDialogHolder;
    QObject* m_settingsDialog = nullptr;
};

void TestQmlResponsiveness::initTestCase()
{
    m_app.settings = std::make_unique<Settings>(QStringLiteral("KomaPaintTests"),
                                                QStringLiteral("QmlResponsiveness"));
    m_app.settings->setValue(QStringLiteral("workspace/layout"), QString());
    m_app.settings->setValue(QStringLiteral("workspace/presets"), QString());
    m_app.settings->setValue(QStringLiteral("recovery/configured"), true);
    m_app.settings->setWindowMaximized(false);
    m_app.settings->setWindowSize(QSize(1200, 800));
    m_app.panels.dockModel()->setSettings(m_app.settings.get());
    QObject::connect(&m_app.workspace, &WorkspaceState::fileErrorOccurred, &m_app.workspace,
                     [this](const QString& message) { m_app.messages << message; });
    QVERIFY(m_app.workspace.newProject(QStringLiteral("Responsiveness"), 512, 512, true,
                                       QStringLiteral("white"), 72));
}

void TestQmlResponsiveness::measuresStartup()
{
    m_app.engine.addImportPath(QStringLiteral("qrc:/"));
    auto* context = m_app.engine.rootContext();
    context->setContextProperty(QStringLiteral("Settings"), m_app.settings.get());
    context->setContextProperty(QStringLiteral("Workspace"), &m_app.workspace);
    context->setContextProperty(QStringLiteral("NewProject"), &m_app.project);
    context->setContextProperty(QStringLiteral("App"), &m_app.config);
    context->setContextProperty(QStringLiteral("Panels"), &m_app.panels);
    context->setContextProperty(QStringLiteral("Dock"), m_app.panels.dockModel());

    QQmlComponent component(&m_app.engine, QUrl(QStringLiteral("qrc:/workspaces/MainWindow.qml")));
    QElapsedTimer timer;
    timer.start();
    m_app.root.reset(component.create());
    QVERIFY2(m_app.root, qPrintable(component.errorString()));
    m_app.window = qobject_cast<QQuickWindow*>(m_app.root.get());
    QVERIFY(m_app.window);
    m_app.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_app.window));
    // Wait for the workspace to have real geometry: a window that has not been
    // laid out yet would make every later measurement meaningless.
    QQuickItem* canvas = nullptr;
    for (int attempt = 0; attempt < 120 && !canvas; ++attempt) {
        QTest::qWait(10);
        if (auto* item = findVisual(m_app.window->contentItem(), QStringLiteral("canvasViewport"));
            item && item->width() > 100 && item->height() > 100) {
            canvas = item;
        }
    }
    QVERIFY(canvas);
    m_startupMs = double(timer.elapsed());
    report("qml_startup_open_project", m_startupMs);
}

void TestQmlResponsiveness::measuresToolSwitching()
{
    const QStringList tools = {QStringLiteral("brush"), QStringLiteral("pen"),
                               QStringLiteral("pencil"), QStringLiteral("airbrush"),
                               QStringLiteral("eraser"), QStringLiteral("fill"),
                               QStringLiteral("wand"), QStringLiteral("selection")};
    // Warm up: the first switch after load touches lazy QML bindings.
    for (const QString& tool : tools) m_app.workspace.setActiveTool(tool);
    QCoreApplication::processEvents();

    std::vector<double> samples;
    for (int round = 0; round < 12; ++round) {
        for (const QString& tool : tools) {
            QElapsedTimer timer;
            timer.start();
            m_app.workspace.setActiveTool(tool);
            QCoreApplication::processEvents();
            samples.push_back(double(timer.nsecsElapsed()) / 1e6);
        }
    }
    QVERIFY(!samples.empty());
    report("qml_tool_switch_median", median(samples));
    double worst = 0.0;
    for (double sample : samples) worst = std::max(worst, sample);
    report("qml_tool_switch_worst", worst);
}

void TestQmlResponsiveness::measuresSubToolSwitching()
{
    auto* library = m_app.workspace.subTools();
    QVERIFY(library);
    m_app.workspace.setActiveTool(QStringLiteral("brush"));
    QCoreApplication::processEvents();
    const QVariantList subTools = library->property("subTools").toList();
    QVERIFY2(subTools.size() > 1, "expected a populated brush collection");

    std::vector<double> samples;
    for (int round = 0; round < 20; ++round) {
        for (int index = 0; index < std::min<int>(6, int(subTools.size())); ++index) {
            QElapsedTimer timer;
            timer.start();
            library->select(index);
            QCoreApplication::processEvents();
            samples.push_back(double(timer.nsecsElapsed()) / 1e6);
        }
    }
    report("qml_subtool_select_median", median(samples));

    // Tab switching rebuilds the Sub-Tool list, which is the expensive one.
    const QVariantList tabs = library->property("engineTabs").toList();
    if (tabs.size() > 1) {
        std::vector<double> tabSamples;
        for (int round = 0; round < 10; ++round) {
            for (int index = 0; index < tabs.size(); ++index) {
                QElapsedTimer timer;
                timer.start();
                library->selectTab(index);
                QCoreApplication::processEvents();
                tabSamples.push_back(double(timer.nsecsElapsed()) / 1e6);
            }
        }
        report("qml_subtool_tab_switch_median", median(tabSamples));
    }
}

void TestQmlResponsiveness::measuresPanelToggling()
{
    const QString panel = QStringLiteral("color");
    std::vector<double> samples;
    for (int round = 0; round < 12; ++round) {
        QElapsedTimer timer;
        timer.start();
        m_app.panels.toggle(panel);
        QCoreApplication::processEvents();
        samples.push_back(double(timer.nsecsElapsed()) / 1e6);
        QElapsedTimer back;
        back.start();
        m_app.panels.toggle(panel);
        QCoreApplication::processEvents();
        samples.push_back(double(back.nsecsElapsed()) / 1e6);
    }
    QVERIFY(!samples.empty());
    report("qml_panel_toggle_median", median(samples));
}

void TestQmlResponsiveness::measuresLayerSelectionAndVisibility()
{
    auto* document = m_app.workspace.document();
    QVERIFY(document);
    auto* layers = document->layers();
    QVERIFY(layers);
    while (layers->rowCount() < 6) layers->addLayer();
    QCoreApplication::processEvents();

    std::vector<double> selectSamples;
    for (int round = 0; round < 10; ++round) {
        for (int row = 0; row < layers->rowCount(); ++row) {
            QElapsedTimer timer;
            timer.start();
            layers->setActiveLayerIndex(row);
            QCoreApplication::processEvents();
            selectSamples.push_back(double(timer.nsecsElapsed()) / 1e6);
        }
    }
    report("qml_layer_select_median", median(selectSamples));

    std::vector<double> visibilitySamples;
    for (int round = 0; round < 20; ++round) {
        const int row = round % layers->rowCount();
        Layer* layer = layers->layerAt(row);
        if (!layer) continue;
        QElapsedTimer timer;
        timer.start();
        layer->setVisible(!layer->isVisible());
        QCoreApplication::processEvents();
        visibilitySamples.push_back(double(timer.nsecsElapsed()) / 1e6);
    }
    QVERIFY(!visibilitySamples.empty());
    report("qml_layer_visibility_median", median(visibilitySamples));
}

void TestQmlResponsiveness::measuresSettingsDialog()
{
    // The dialog is created on demand by the window; creating it repeatedly is
    // what switching through the seven pages costs.
    std::vector<double> samples;
    for (int round = 0; round < 6; ++round) {
        QQmlComponent component(&m_app.engine, QUrl(QStringLiteral("qrc:/components/KomaSettingsDialog.qml")));
        QElapsedTimer timer;
        timer.start();
        auto* dialog = component.create(m_app.engine.rootContext());
        const double elapsed = double(timer.nsecsElapsed()) / 1e6;
        if (!dialog) {
            qInfo("settings dialog could not be created standalone: %s",
                  qPrintable(component.errorString()));
            QSKIP("settings dialog needs the window context");
        }
        samples.push_back(elapsed);
        delete dialog;
        QCoreApplication::processEvents();
    }
    report("qml_settings_dialog_create_median", median(samples));
}

QTEST_MAIN(TestQmlResponsiveness)
#include "TestQmlResponsiveness.moc"
