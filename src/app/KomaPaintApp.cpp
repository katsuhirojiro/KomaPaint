#include "KomaPaintApp.h"
#include "CanvasViewport.h"
#include "core/Version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
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
}

KomaPaintApp::~KomaPaintApp() = default;

void KomaPaintApp::registerTypes()
{
    qRegisterMetaType<PanelManager*>();
    qmlRegisterType<CanvasViewport>("KomaPaint", 1, 0, "CanvasViewport");
}

void KomaPaintApp::loadStyles() {}

void KomaPaintApp::loadUi()
{
    auto* ctx = m_engine.rootContext();
    ctx->setContextProperty("App",       &m_config);
    ctx->setContextProperty("Settings",  &m_settings);
    ctx->setContextProperty("Panels",    &m_panels);
    ctx->setContextProperty("Workspace", &m_workspace);
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

    qInfo("[KomaPaint] run(): entering app.exec() event loop");
    return app->exec();
}

}  // namespace komapaint
