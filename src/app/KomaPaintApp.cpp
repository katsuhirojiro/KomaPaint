#include "KomaPaintApp.h"
#include "core/Version.h"

#include <QApplication>
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

#include <QtGlobal>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QList>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlError>
#include <QQmlContext>
#include <QTimer>
#include <QDebug>

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

    // Custom QML modules are registered as singletons / types here as the
    // project grows. In this phase we keep the C++ surface minimal and push
    // the interface into QML.
}

void KomaPaintApp::loadStyles()
{
    // Style selection now happens in main() before QApplication is created
    // so the QML engine can resolve the Basic style plugin deterministically.
    // This function is kept as a hook for future runtime style tweaks (font
    // overrides, palette injection, etc.).
}

void KomaPaintApp::loadUi()
{
    auto* ctx = m_engine.rootContext();
    ctx->setContextProperty(QStringLiteral("App"),         &m_config);
    ctx->setContextProperty(QStringLiteral("Settings"),    &m_settings);
    ctx->setContextProperty(QStringLiteral("Panels"),      &m_panels);
    ctx->setContextProperty(QStringLiteral("Workspace"),   &m_workspace);

    fprintf(stdout, "[KomaPaint] Starting up; Qt version %s\n", qVersion());
    fprintf(stdout, "[KomaPaint] Application dir = %s\n",
            QCoreApplication::applicationDirPath().toUtf8().constData());
    fprintf(stdout, "[KomaPaint] Arguments (argc = %d):\n",
            QCoreApplication::arguments().size());
    for (const QString& a : QCoreApplication::arguments()) {
        fprintf(stdout, "   %s\n", a.toUtf8().constData());
    }
    fflush(stdout);

    // Log any QML warnings/errors so startup failures are visible even when
    // the user launches from Explorer with no console attached.
    QObject::connect(&m_engine, &QQmlApplicationEngine::warnings,
                     this, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& e : warnings) {
            qWarning() << "[QML]" << e.toString();
        }
    });

    // When built against the resource-bundled QML (the default for release
    // and Windows deployment), load from "qrc:/". The source-tree path is
    // kept as a fallback for developer builds that skip the qrc bundle.
    //
    // NOTE: a QUrl("qrc:/...") URL is a qrc scheme URL. QFile uses the
    // ":/" prefix to open resources, so QFile::exists must use ":/" while
    // QQmlApplicationEngine::load must use "qrc:/" — do NOT concatenate
    // ":" onto a "qrc:/" string, that produces the invalid ":/qrc:/..."
    // path and the existence check will silently return false.
    const QString qrcMain  = QStringLiteral(":/workspaces/MainWindow.qml");
    const QString qrcUrl   = QStringLiteral("qrc:/workspaces/MainWindow.qml");
    const QString qmlDir   = QFileInfo(QString::fromUtf8(KOMAPAINT_QML_DIR)).absoluteFilePath();
    const QString srcMain  = qmlDir + QStringLiteral("/workspaces/MainWindow.qml");

    QUrl loadUrl;
    if (QFile::exists(qrcMain)) {
        qInfo() << "[KomaPaint] Loading QML from Qt resource:" << qrcUrl;
        m_engine.addImportPath(QStringLiteral("qrc:/"));
        loadUrl = QUrl(qrcUrl);
    } else if (QFile::exists(srcMain)) {
        qInfo() << "[KomaPaint] Loading QML from source tree:" << srcMain;
        m_engine.addImportPath(qmlDir);
        loadUrl = QUrl::fromLocalFile(srcMain);
    } else {
        qCritical() << "[KomaPaint] Cannot find MainWindow.qml in resources ("
                    << qrcMain << ") or in source dir (" << srcMain << ").";
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-2); });
        return;
    }

    fprintf(stdout, "[KomaPaint] About to load QML: %s\n", loadUrl.toString().toUtf8().constData());
    fflush(stdout);
    m_engine.load(loadUrl);
    fprintf(stdout, "[KomaPaint] m_engine.load() returned. Root objects: %d\n",
            m_engine.rootObjects().size());
    for (QObject* o : m_engine.rootObjects()) {
        fprintf(stdout, "   root object: %s\n", o->metaObject()->className());
    }
    fflush(stdout);
    // If QML load failed to produce ANY root object, exit immediately.
    // (The objectCreated signal may not have been dispatched yet depending on
    // connection type, so check synchronously after load returns.)
    if (m_engine.rootObjects().isEmpty()) {
        fprintf(stderr, "[KomaPaint] CRITICAL: m_engine.rootObjects() is empty after load.\n");
        fflush(stderr);
        QTimer::singleShot(0, QCoreApplication::instance(), []{ QCoreApplication::exit(-4); });
    }

    QObject::connect(&m_engine, &QQmlApplicationEngine::objectCreated,
                     this, [this, loadUrl](QObject* obj, const QUrl& url) {
        if (url != loadUrl) return;  // only react to the main window creation
        fprintf(stdout, "[KomaPaint] objectCreated fired: obj=%p url=%s\n",
                (void*)obj, url.toString().toUtf8().constData());
        fflush(stdout);
        if (!obj) {
            fprintf(stderr, "[KomaPaint] CRITICAL: Failed to instantiate main QML: %s\n",
                    url.toString().toUtf8().constData());
            fflush(stderr);
            QCoreApplication::exit(-1);
            return;
        }
        m_window = qobject_cast<QQuickWindow*>(obj);
        if (!m_window) {
            fprintf(stderr, "[KomaPaint] CRITICAL: Root QML object (%s) is not a QQuickWindow.\n",
                    obj->metaObject()->className());
            fflush(stderr);
            QCoreApplication::exit(-3);
            return;
        }
        fprintf(stdout, "[KomaPaint] Main window created; calling show().\n");
        fflush(stdout);
        m_window->show();
    }, Qt::DirectConnection);

int KomaPaintApp::run()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) {
        qCritical() << "KomaPaintApp requires a QApplication instance.";
        return -1;
    }

    app->setApplicationName(QStringLiteral(KOMAPAINT_PRETTY_NAME));
    app->setApplicationVersion(QStringLiteral(KOMAPAINT_VERSION_STRING));
    app->setOrganizationName(QStringLiteral(KOMAPAINT_ORGANIZATION));
    app->setOrganizationDomain(QStringLiteral(KOMAPAINT_DOMAIN));

    // Application-wide font. We ship no bundled font in phase 1, so pick the
    // most legible sans-serif available on the host system.
    const QStringList preferredFonts = {
        QStringLiteral("Segoe UI"),        // Windows
        QStringLiteral("Inter"),           // Common open source
        QStringLiteral("San Francisco"),   // macOS
        QStringLiteral("Noto Sans"),       // Linux
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
    return app->exec();
}

}  // namespace komapaint
