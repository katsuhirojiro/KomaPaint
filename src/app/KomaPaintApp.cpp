#include "KomaPaintApp.h"
#include "core/Version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace komapaint {

static void qtMsgHandler(QtMsgType type, const QMessageLogContext& ctx,
                         const QString& msg)
{
    const char* tag = "INFO ";
    switch (type) {
        case QtDebugMsg:    tag = "DEBUG"; break;
        case QtInfoMsg:     tag = "INFO "; break;
        case QtWarningMsg:  tag = "WARN "; break;
        case QtCriticalMsg: tag = "CRIT "; break;
        case QtFatalMsg:    tag = "FATAL"; break;
    }
    fprintf(stderr, "[Qt-%s] %s\n", tag, msg.toUtf8().constData());
    if (ctx.file && ctx.line) {
        fprintf(stderr, "    at %s:%u\n", ctx.file, ctx.line);
    }
    fflush(stderr);
}

KomaPaintApp::KomaPaintApp(QObject* parent)
    : QObject(parent),
      m_settings(QStringLiteral(KOMAPAINT_ORGANIZATION),
                 QStringLiteral(KOMAPAINT_PRETTY_NAME))
{
}

KomaPaintApp::~KomaPaintApp() = default;

void KomaPaintApp::registerTypes()
{
    qRegisterMetaType<PanelManager*>();
}

void KomaPaintApp::loadStyles() {}

void KomaPaintApp::loadUi()
{
    qInstallMessageHandler(qtMsgHandler);

    auto* ctx = m_engine.rootContext();
    ctx->setContextProperty(QStringLiteral("App"),       &m_config);
    ctx->setContextProperty(QStringLiteral("Settings"),  &m_settings);
    ctx->setContextProperty(QStringLiteral("Panels"),    &m_panels);
    ctx->setContextProperty(QStringLiteral("Workspace"), &m_workspace);

    printf("[KomaPaint] loadUi(): setting up QML context properties\n");

    const QString qrcMain = QStringLiteral(":/workspaces/MainWindow.qml");
    const QString qrcUrl  = QStringLiteral("qrc:/workspaces/MainWindow.qml");
    const QString qmlDir  = QFileInfo(QString::fromUtf8(KOMAPAINT_QML_DIR)).absoluteFilePath();
    const QString srcMain = qmlDir + QStringLiteral("/workspaces/MainWindow.qml");

    QUrl loadUrl;
    if (QFile::exists(qrcMain)) {
        printf("[KomaPaint] loadUi(): loading QML from resource: %s\n",
               qrcUrl.toUtf8().constData());
        m_engine.addImportPath(QStringLiteral("qrc:/"));
        loadUrl = QUrl(qrcUrl);
    } else if (QFile::exists(srcMain)) {
        printf("[KomaPaint] loadUi(): loading QML from source tree: %s\n",
               srcMain.toUtf8().constData());
        m_engine.addImportPath(qmlDir);
        loadUrl = QUrl::fromLocalFile(srcMain);
    } else {
        fprintf(stderr,
                "[KomaPaint] CRITICAL: MainWindow.qml not found in resource (%s) "
                "or source tree (%s).\n",
                qrcMain.toUtf8().constData(), srcMain.toUtf8().constData());
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-2); });
        return;
    }

    printf("[KomaPaint] loadUi(): calling m_engine.load(%s)...\n",
           loadUrl.toString().toUtf8().constData());
    fflush(stdout);
    m_engine.load(loadUrl);
    printf("[KomaPaint] loadUi(): m_engine.load() returned; rootObjects().size() = %d\n",
           m_engine.rootObjects().size());
    fflush(stdout);

    if (m_engine.rootObjects().isEmpty()) {
        fprintf(stderr, "[KomaPaint] CRITICAL: rootObjects() empty after load - QML failed to parse.\n");
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-1); });
        return;
    }

    m_window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().first());
    if (!m_window) {
        fprintf(stderr, "[KomaPaint] CRITICAL: root object is '%s', not a QQuickWindow.\n",
                m_engine.rootObjects().first()->metaObject()->className());
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-3); });
        return;
    }

    printf("[KomaPaint] loadUi(): main window created; calling show()\n");
    fflush(stdout);
    m_window->show();
}

int KomaPaintApp::run()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        fprintf(stderr, "[KomaPaint] CRITICAL: no QApplication instance.\n");
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

    registerTypes();
    loadStyles();
    loadUi();
    printf("[KomaPaint] run(): entering app.exec() event loop\n");
    fflush(stdout);
    return app->exec();
}

}  // namespace komapaint
