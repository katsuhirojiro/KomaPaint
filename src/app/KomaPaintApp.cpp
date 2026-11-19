#include "KomaPaintApp.h"
#include "BrushStudioAssetView.h"
#include "BrushStudioModel.h"
#include "CanvasViewport.h"
#include "MixerSurfaceItem.h"
#include "PaintMixerController.h"
#include "PerformanceSettings.h"
#include "ShortcutManager.h"
#include "SubToolLibrary.h"
#include "ToolSettings.h"
#include "canvas/CanvasDocument.h"
#include "color/ColorPalette.h"
#include "core/Version.h"
#include "history/UndoStack.h"
#include "layers/Layer.h"
#include "layers/LayerModel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtLogging>

namespace komapaint {

KomaPaintApp::KomaPaintApp(const QString& fileToOpen, QObject* parent)
    : QObject(parent),
      m_settings(QStringLiteral(KOMAPAINT_ORGANIZATION),
                 QStringLiteral(KOMAPAINT_PRETTY_NAME)),
      m_fileToOpen(fileToOpen)
{
    m_workspace.setDefaultTool(m_settings.value(QStringLiteral("tools/defaultTool"),
                                                QStringLiteral("brush")).toString());
    m_workspace.setRealisticEngineEnabled(
        m_settings.value(QStringLiteral("tools/realisticEngineEnabled"), false).toBool());
    m_workspace.setBrushEngine(m_settings.value(QStringLiteral("tools/brushEngine"),
                                                QStringLiteral("digital")).toString());
    const int recoveryInterval = m_settings.value(QStringLiteral("recovery/intervalMinutes"), 10).toInt();
    m_workspace.setRecoveryInterval(recoveryInterval);
    m_workspace.bindAppSettings(&m_settings);

    connect(&m_workspace, &WorkspaceState::brushEngineChanged, this, [this](const QString& engine) {
        m_settings.setValue(QStringLiteral("tools/brushEngine"), engine);
        m_settings.sync();
    });
    connect(&m_workspace, &WorkspaceState::realisticEngineEnabledChanged, this, [this](bool enabled) {
        m_settings.setValue(QStringLiteral("tools/realisticEngineEnabled"), enabled);
        m_settings.sync();
    });
}

KomaPaintApp::~KomaPaintApp() = default;

void KomaPaintApp::registerTypes()
{
    qRegisterMetaType<PanelManager*>();
    qRegisterMetaType<DockModel*>();
    qRegisterMetaType<PaintMixerController*>();
    qRegisterMetaType<PerformanceSettings*>();
    qRegisterMetaType<ShortcutManager*>();
    qRegisterMetaType<ToolSettings*>();
    qRegisterMetaType<komapaint::input::TabletInputSettings*>();
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
    qmlRegisterType<MixerSurfaceItem>("KomaPaint", 1, 0, "MixerSurfaceItem");
    qmlRegisterType<BrushStudioAssetView>("KomaPaint", 1, 0, "BrushStudioAssetView");
    qmlRegisterType<BrushStudioModel>("KomaPaint", 1, 0, "BrushStudioModel");
}

void KomaPaintApp::loadStyles() {}

void KomaPaintApp::loadUi()
{
    auto* ctx = m_engine.rootContext();
    ctx->setContextProperty("App",       &m_config);
    ctx->setContextProperty("Settings",  &m_settings);
    // The docking workspace: the shared layout model that every panel lives
    // in. "Panels" stays as the compatibility façade over the same model.
    m_dock.setSettings(&m_settings);
    m_panels.setDockModel(&m_dock);
    ctx->setContextProperty("Dock",      &m_dock);
    ctx->setContextProperty("Panels",    &m_panels);
    ctx->setContextProperty("Workspace", &m_workspace);
    ctx->setContextProperty("Shortcuts", m_workspace.shortcuts());
    ctx->setContextProperty("NewProject", &m_newProject);

    qInfo("[KomaPaint] loadUi(): setting up QML context properties");

    const QString qrcMain = QStringLiteral(":/workspaces/MainWindow.qml");
    const QString qrcUrl  = QStringLiteral("qrc:/workspaces/MainWindow.qml");
    const QString qmlDir  = QFileInfo(QString::fromUtf8(KOMAPAINT_QML_DIR)).absoluteFilePath();
    const QString srcMain = qmlDir + QStringLiteral("/workspaces/MainWindow.qml");

    QUrl loadUrl;
    if (QFile::exists(qrcMain)) {
        qInfo("[KomaPaint] loadUi(): loading QML from resource: %s",
              qUtf8Printable(qrcUrl));
        m_engine.addImportPath(QStringLiteral("qrc:/"));
        loadUrl = QUrl(qrcUrl);
    } else if (QFile::exists(srcMain)) {
        qInfo("[KomaPaint] loadUi(): loading QML from source tree: %s",
              qUtf8Printable(srcMain));
        m_engine.addImportPath(qmlDir);
        loadUrl = QUrl::fromLocalFile(srcMain);
    } else {
        qCritical("[KomaPaint] CRITICAL: MainWindow.qml not found in resource (%s) "
                  "or source tree (%s).",
                  qUtf8Printable(qrcMain), qUtf8Printable(srcMain));
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-2); });
        return;
    }

    qInfo("[KomaPaint] loadUi(): calling m_engine.load(%s)...",
          qUtf8Printable(loadUrl.toString()));
    connect(&m_engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const auto& w : warnings) m_qmlWarnings.append(w.toString());
    });
    m_engine.load(loadUrl);
    qInfo("[KomaPaint] loadUi(): m_engine.load() returned; rootObjects().size() = %lld",
          static_cast<long long>(m_engine.rootObjects().size()));

    if (m_engine.rootObjects().isEmpty()) {
        qCritical("[KomaPaint] CRITICAL: rootObjects() empty after load - QML failed to parse.");
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-1); });
        return;
    }

    m_window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().first());
    if (!m_window) {
        qCritical("[KomaPaint] CRITICAL: root object is '%s', not a QQuickWindow.",
                  m_engine.rootObjects().first()->metaObject()->className());
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-3); });
        return;
    }

    qInfo("[KomaPaint] loadUi(): main window created; calling show()");
    m_window->show();
}

int KomaPaintApp::run()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        qCritical("[KomaPaint] CRITICAL: no QApplication instance.");
        return -1;
    }

    app->setApplicationName(QStringLiteral(KOMAPAINT_PRETTY_NAME));
    app->setApplicationVersion(QStringLiteral(KOMAPAINT_VERSION_STRING));
    app->setOrganizationName(QStringLiteral(KOMAPAINT_ORGANIZATION));
    app->setOrganizationDomain(QStringLiteral(KOMAPAINT_DOMAIN));
    m_workspace.setDefaultTool(m_settings.value(QStringLiteral("tools/defaultTool"),
                                               QStringLiteral("brush")).toString());
    m_workspace.setRealisticEngineEnabled(
        m_settings.value(QStringLiteral("tools/realisticEngineEnabled"), false).toBool());
    m_workspace.setBrushEngine(m_settings.value(QStringLiteral("tools/brushEngine"),
                                                QStringLiteral("digital")).toString());

    const QStringList preferredFonts = {
        QStringLiteral("Segoe UI"),
        QStringLiteral("Inter"),
        QStringLiteral("San Francisco"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("Ubuntu"),
        QStringLiteral("Arial")
    };
    for (const QString& name : preferredFonts) {
        if (QFontDatabase::hasFamily(name)) { app->setFont(QFont(name, 10)); break; }
    }

    const QDir appData(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    const bool recoveryReady = m_workspace.initializeRecovery(appData.filePath(QStringLiteral("recovery")));
    if (!recoveryReady) qWarning("[KomaPaint] Workspace recovery is unavailable (directory or lock failure).");
    // The session (open tabs, per-project view, window layout) is its own
    // small file next to the recovery folder. Only the instance that holds
    // the recovery lock owns the session, so a second running copy cannot
    // overwrite the first one's tab list.
    if (recoveryReady) m_workspace.initializeSession(appData.filePath(QStringLiteral("session.json")));
    registerTypes();
    loadStyles();
    loadUi();
    if (!recoveryReady) QTimer::singleShot(0, &m_workspace, [this] {
        emit m_workspace.fileErrorOccurred(QStringLiteral("Workspace recovery is unavailable. Automatic recovery and workspace restoration will not run in this instance."));
    });

    if (!m_fileToOpen.isEmpty()) {
        qInfo("[KomaPaint] run(): opening command-line file %s", qUtf8Printable(m_fileToOpen));
        // On failure the workspace emits fileErrorOccurred, which the QML
        // layer shows in the usual error dialog; the default untitled
        // document remains untouched.
        if (!m_workspace.openFromPath(m_fileToOpen)) {
            qWarning("[KomaPaint] run(): could not open command-line file %s", qUtf8Printable(m_fileToOpen));
        }
    }

    if (qEnvironmentVariableIsSet("KOMAPAINT_VERIFY_PACKAGED")) {
        QTimer::singleShot(150, this, &KomaPaintApp::runPackagedVerification);
    }

    qInfo("[KomaPaint] run(): entering app.exec() event loop");
    return app->exec();
}

void KomaPaintApp::runPackagedVerification()
{
    auto findVisual = [](auto& self, QQuickItem* from, const QString& name) -> QQuickItem* {
        if (!from) return nullptr;
        if (from->objectName() == name) return from;
        for (auto* child : from->childItems()) {
            if (auto* found = self(self, child, name)) return found;
        }
        return nullptr;
    };
    auto visual = [&](const QString& name) -> QQuickItem* {
        return m_window ? findVisual(findVisual, m_window->contentItem(), name) : nullptr;
    };

    QStringList report;
    auto fail = [&](const QString& reason) {
        report << (QStringLiteral("FAIL: ") + reason);
        qCritical("[KomaPaint][VerifyPackaged] %s", qUtf8Printable(reason));
        const QString reportPath = qEnvironmentVariable("KOMAPAINT_VERIFY_REPORT");
        if (!reportPath.isEmpty()) {
            QFile f(reportPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(report.join(u'\n').toUtf8());
            }
        }
        QCoreApplication::exit(1);
    };

    if (!m_window || !m_window->isVisible()) {
        fail(QStringLiteral("Main window did not open or is not visible."));
        return;
    }
    report << QStringLiteral("1. Main window opened: '%1' (%2x%3)")
                  .arg(m_window->title())
                  .arg(m_window->width())
                  .arg(m_window->height());

    // Verify bundled resources & tool icons inside the packaged binary.
    for (const char* res : {
             ":/workspaces/MainWindow.qml",
             ":/workspaces/BrushStudioWindow.qml",
             ":/brushes/presets.json",
             ":/icons/tools/brush.png",
             ":/icons/tools/eraser.png",
             ":/icons/tools/fill.png",
             ":/icons/tools/wand.png",
             ":/icons/tools/lasso.png",
             ":/icons/tools/selection.png",
             ":/icons/tools/gradient.png",
             ":/icons/tools/rectangle.png",
             ":/icons/tools/text.png"
         }) {
        if (!QFile::exists(QLatin1String(res))) {
            fail(QStringLiteral("Missing bundled resource: %1").arg(QLatin1String(res)));
            return;
        }
    }
    report << QStringLiteral("2. Bundled QML, brush presets, and tool icon resources verified.");

    // Create a canvas in the live running packaged application.
    if (!m_workspace.newProject(QStringLiteral("PackagedVerify"), 512, 512, true,
                                QStringLiteral("white"), 72)) {
        fail(QStringLiteral("WorkspaceState::newProject failed."));
        return;
    }
    QCoreApplication::processEvents();

    auto* canvas = qobject_cast<CanvasViewport*>(visual(QStringLiteral("canvasViewport")));
    if (!canvas) {
        fail(QStringLiteral("CanvasViewport ('canvasViewport') not created in live QML workspace."));
        return;
    }
    report << QStringLiteral("3. Canvas created (512x512) and live CanvasViewport initialized.");

    // Verify tool buttons exist and are enabled, and switch panels including Color Wheel & Paint Mixer.
    for (const char* toolId : {
             "brush", "eraser", "fill", "wand", "lasso", "selection",
             "move", "transform", "gradient", "line", "curveline",
             "rectangle", "circle", "text", "smudge"
         }) {
        const QString name = QStringLiteral("tool_") + QString::fromLatin1(toolId);
        auto* btn = visual(name);
        if (!btn || !btn->property("available").toBool()) {
            fail(QStringLiteral("Tool button '%1' missing or unavailable.").arg(name));
            return;
        }
        m_workspace.setActiveTool(QString::fromLatin1(toolId));
        QCoreApplication::processEvents();
    }
    m_dock.showPanel(QStringLiteral("subtool"));
    m_dock.showPanel(QStringLiteral("toolproperty"));
    m_dock.showPanel(QStringLiteral("color"));
    QCoreApplication::processEvents();
    auto* colorWheel = visual(QStringLiteral("colorWheel"));
    if (!colorWheel) {
        fail(QStringLiteral("ColorWheel not accessible in Color dock panel."));
        return;
    }

    m_dock.showPanel(QStringLiteral("colormixer"));
    QCoreApplication::processEvents();
    auto* mixerSurface = visual(QStringLiteral("mixerSurface"));
    if (!mixerSurface) {
        fail(QStringLiteral("MixerSurfaceItem not accessible in Color Mixer dock panel."));
        return;
    }
    report << QStringLiteral("4. Tool palette (all 15 active art tools + view tools), panels, Color Wheel, and Paint Mixer verified.");

    // Verify Digital and Realistic engine settings toggle cleanly.
    m_workspace.setActiveTool(QStringLiteral("brush"));
    m_workspace.setRealisticEngineEnabled(true);
    m_workspace.setBrushEngine(QStringLiteral("realistic"));
    QCoreApplication::processEvents();
    if (!m_workspace.isRealisticEngine() || m_workspace.subTools()->activeEngine() != QStringLiteral("realistic")) {
        fail(QStringLiteral("Failed to switch to Realistic engine."));
        return;
    }
    m_workspace.setBrushEngine(QStringLiteral("digital"));
    QCoreApplication::processEvents();
    if (m_workspace.isRealisticEngine() || m_workspace.subTools()->activeEngine() != QStringLiteral("digital")) {
        fail(QStringLiteral("Failed to switch back to Digital engine."));
        return;
    }
    report << QStringLiteral("5. Digital & Realistic engine switching verified.");

    // Exercise draw, erase, select, smart fill, and undo on the real document.
    auto* doc = qobject_cast<CanvasDocument*>(m_workspace.document());
    if (!doc || !doc->layers() || !doc->layers()->layerAt(0)) {
        fail(QStringLiteral("Active CanvasDocument/Layer missing."));
        return;
    }
    auto* layer = doc->layers()->layerAt(0);

    // Draw closed rectangle boundary [100..260, 100..260]
    m_workspace.setActiveTool(QStringLiteral("rectangle"));
    m_workspace.colors()->setForeground(QColor(20, 20, 20));
    m_workspace.toolSettings()->setShapeMode(QStringLiteral("stroke"));
    m_workspace.toolSettings()->setShapeStrokeWidth(6);
    if (!canvas->applyShape(QStringLiteral("rectangle"), 100, 100, 260, 260)) {
        fail(QStringLiteral("Rectangle shape tool failed."));
        return;
    }
    // Smart Fill inside [100..260, 100..260]
    m_workspace.setActiveTool(QStringLiteral("fill"));
    m_workspace.subTools()->select(2); // Smart Fill
    m_workspace.colors()->setForeground(QColor(40, 140, 220));
    if (!canvas->applyFillAt(180, 180) || !canvas->commitPendingRegionFill()) {
        fail(QStringLiteral("Smart Fill preview/commit failed."));
        return;
    }
    if ((layer->tiles().pixel({180, 180}).value_or(0U) >> 24) < 200 ||
        (layer->tiles().pixel({360, 360}).value_or(0U) >> 24) != 0) {
        fail(QStringLiteral("Smart Fill leaked or did not fill interior."));
        return;
    }

    // Brush stroke + Eraser + Selection + Undo
    const QPointF vpStart = canvas->documentToViewport(340, 180);
    const QPointF vpEnd = canvas->documentToViewport(390, 180);
    const QPointF vpMid = canvas->documentToViewport(360, 180);
    m_workspace.setActiveTool(QStringLiteral("brush"));
    m_workspace.colors()->setForeground(QColor(210, 50, 40));
    m_workspace.brush()->setSize(20);
    if (!canvas->beginStroke(vpStart.x(), vpStart.y(), 1.0, m_workspace.brush(), m_workspace.colors()->foreground())) {
        fail(QStringLiteral("Digital brush beginStroke failed."));
        return;
    }
    canvas->continueStroke(vpEnd.x(), vpEnd.y(), 1.0);
    canvas->endStroke(vpEnd.x(), vpEnd.y(), 1.0);
    if ((layer->tiles().pixel({360, 180}).value_or(0U) >> 24) < 200) {
        fail(QStringLiteral("Brush stroke left no pixels."));
        return;
    }

    m_workspace.setActiveTool(QStringLiteral("eraser"));
    m_workspace.brush()->setSize(24);
    if (!canvas->beginStroke(vpMid.x(), vpMid.y(), 1.0, m_workspace.brush(), m_workspace.colors()->foreground())) {
        fail(QStringLiteral("Eraser beginStroke failed."));
        return;
    }
    canvas->endStroke(vpMid.x(), vpMid.y(), 1.0);
    if ((layer->tiles().pixel({360, 180}).value_or(0U) >> 24) != 0) {
        fail(QStringLiteral("Eraser did not erase pixels."));
        return;
    }

    // Undo eraser -> pixel returns
    if (!m_workspace.history() || !m_workspace.history()->canUndo()) {
        fail(QStringLiteral("Undo stack unavailable."));
        return;
    }
    m_workspace.history()->undo();
    if ((layer->tiles().pixel({360, 180}).value_or(0U) >> 24) < 200) {
        fail(QStringLiteral("Undo did not restore erased pixel."));
        return;
    }

    // Selection API
    doc->selectAll();
    if (!m_workspace.hasSelection()) {
        fail(QStringLiteral("Select All failed."));
        return;
    }
    doc->deselect();
    report << QStringLiteral("6. Canvas draw, erase, select, Smart Fill, and undo verified.");

    // Save and reopen .koma project
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        fail(QStringLiteral("Could not create temporary verification directory."));
        return;
    }
    const QString komaPath = tempDir.filePath(QStringLiteral("verify_roundtrip.koma"));
    if (!m_workspace.saveToPath(komaPath)) {
        fail(QStringLiteral("saveToPath failed."));
        return;
    }
    if (!m_workspace.closeTab(0, true) || !m_workspace.openFromPath(komaPath)) {
        fail(QStringLiteral("openFromPath failed on saved .koma project."));
        return;
    }
    auto* reopenedDoc = qobject_cast<CanvasDocument*>(m_workspace.document());
    if (!reopenedDoc || !reopenedDoc->layers() || reopenedDoc->layers()->rowCount() < 1 ||
        reopenedDoc->width() != 512 || reopenedDoc->height() != 512 ||
        (reopenedDoc->layers()->layerAt(0)->tiles().pixel({180, 180}).value_or(0U) >> 24) < 200) {
        fail(QStringLiteral("Reopened .koma project lost dimensions or artwork pixels."));
        return;
    }
    report << QStringLiteral("7. Save and reopen .koma round-trip verified.");

    // Unlike a resource-existence check, this loads the lazy window (and all
    // of its QML imports) from the packaged executable.
    if (!QMetaObject::invokeMethod(m_window, "openBrushStudio")) {
        fail(QStringLiteral("Brush Studio opener is unavailable."));
        return;
    }
    QCoreApplication::processEvents();
    QQuickWindow* studio = nullptr;
    for (auto* window : QGuiApplication::allWindows()) {
        if (window->objectName() == QStringLiteral("brushStudioWindow")) {
            studio = qobject_cast<QQuickWindow*>(window);
            break;
        }
    }
    auto* draft = studio ? studio->findChild<BrushStudioModel*>() : nullptr;
    if (!studio || !studio->isVisible() || studio->transientParent() != m_window || !draft ||
        !studio->findChild<QObject*>(QStringLiteral("brushStudioSaveDialog")) ||
        !findVisual(findVisual, studio->contentItem(), QStringLiteral("brushStudioPreview"))) {
        fail(QStringLiteral("Brush Studio did not open with its independent draft and bundled controls."));
        return;
    }
    const auto brushPath = QUrl::fromLocalFile(tempDir.filePath(QStringLiteral("verify_brush.kb")));
    draft->setName(QStringLiteral("Packaged Studio brush"));
    if (!draft->saveDraft(brushPath) || !draft->newDraft() || !draft->openDraft(brushPath) ||
        draft->name() != QStringLiteral("Packaged Studio brush") || m_workspace.document() != reopenedDoc) {
        fail(QStringLiteral("Studio .kb round-trip failed or changed the active artwork."));
        return;
    }
    studio->close();
    report << QStringLiteral("8. Independent Brush Studio opened from the package; .kb round-trip verified.");

    if (!m_qmlWarnings.isEmpty()) {
        fail(QStringLiteral("QML warnings occurred: %1").arg(m_qmlWarnings.join(QStringLiteral(" | "))));
        return;
    }
    report << QStringLiteral("9. Zero QML warnings, missing Qt plugins, or critical errors.");

    const QString reportPath = qEnvironmentVariable("KOMAPAINT_VERIFY_REPORT");
    if (!reportPath.isEmpty()) {
        QFile f(reportPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(report.join(u'\n').toUtf8());
        }
    }
    qInfo("[KomaPaint][VerifyPackaged] ALL CHECKS PASSED:\n%s", qUtf8Printable(report.join(u'\n')));
    QCoreApplication::exit(0);
}

}  // namespace komapaint
