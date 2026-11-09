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

void KomaPaintApp::loadStyles()
{
    // Style is set in main() before QApplication construction.
}

static void logLine(const char* prefix, const char* msg)
{
    fprintf(stdout, "[KomaPaint] %s %s\n", prefix, msg);
    fflush(stdout);
}

void KomaPaintApp::loadUi()
{
    auto* ctx = m_engine.rootContext();
    ctx->setContextProperty(QStringLiteral("App"),       &m_config);
    ctx->setContextProperty(QStringLiteral("Settings"),  &m_settings);
    ctx->setContextProperty(QStringLiteral("Panels"),    &m_panels);
    ctx->setContextProperty(QStringLiteral("Workspace"), &m_workspace);

    logLine("INFO", "Starting QML load");

    QObject::connect(&m_engine, &QQmlApplicationEngine::warnings,
                     this, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& e : warnings) {
            fprintf(stderr, "[KomaPaint] QML WARNING: %s\n",
                    e.toString().toUtf8().constData());
            fflush(stderr);
        }
    });

    const QString qrcMain = QStringLiteral(":/workspaces/MainWindow.qml");
    const QString qrcUrl  = QStringLiteral("qrc:/workspaces/MainWindow.qml");
    const QString qmlDir  = QFileInfo(QString::fromUtf8(KOMAPAINT_QML_DIR)).absoluteFilePath();
    const QString srcMain = qmlDir + QStringLiteral("/workspaces/MainWindow.qml");

    QUrl loadUrl;
    if (QFile::exists(qrcMain)) {
        logLine("INFO", "Loading QML from Qt resource (qrc:/workspaces/MainWindow.qml)");
        m_engine.addImportPath(QStringLiteral("qrc:/"));
        loadUrl = QUrl(qrcUrl);
    } else if (QFile::exists(srcMain)) {
        logLine("INFO", ("Loading QML from source tree: " + srcMain).toUtf8().constData());
        m_engine.addImportPath(qmlDir);
        loadUrl = QUrl::fromLocalFile(srcMain);
    } else {
        fprintf(stderr,
                "[KomaPaint] CRITICAL: Cannot find MainWindow.qml in resource (%s) "
                "or source dir (%s)\n",
                qrcMain.toUtf8().constData(), srcMain.toUtf8().constData());
        fflush(stderr);
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-2); });
        return;
    }

    m_engine.load(loadUrl);

    // Synchronous check: if load didn't produce any root object, QML failed to
    // instantiate. Schedule exit so the event loop processes it cleanly.
    if (m_engine.rootObjects().isEmpty()) {
        fprintf(stderr, "[KomaPaint] CRITICAL: rootObjects() is empty after load().\n");
        fflush(stderr);
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-1); });
        return;
    }

    m_window = qobject_cast<QQuickWindow*>(m_engine.rootObjects().first());
    if (!m_window) {
        fprintf(stderr, "[KomaPaint] CRITICAL: root object is '%s', not a QQuickWindow.\n",
                m_engine.rootObjects().first()->metaObject()->className());
        fflush(stderr);
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-3); });
        return;
    }

    logLine("INFO", "Main window created; showing.");
    m_window->show();
}

int KomaPaintApp::run()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        fprintf(stderr, "[KomaPaint] CRITICAL: No QApplication instance.\n");
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
        if (QFontDatabase::hasFamily(name)) {
            app->setFont(QFont(name, 10));
            break;
        }
    }

    registerTypes();
    loadStyles();
    loadUi();
    logLine("INFO", "Entering event loop.");
    return app->exec();
}

}  // namespace komapaint
