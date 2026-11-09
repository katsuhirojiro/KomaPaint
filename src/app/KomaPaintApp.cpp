#include "KomaPaintApp.h"
#include "core/Version.h"

#include <QApplication>
#include <QCoreApplication>
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
    // Use the "Basic" Qt Quick Controls style and apply a custom KomaPaint
    // theme from QML. This avoids native widget styling that would clash
    // with the dark charcoal aesthetic.
    QQuickStyle::setStyle("Basic");
}

void KomaPaintApp::loadUi()
{
    auto* ctx = m_engine.rootContext();
    ctx->setContextProperty(QStringLiteral("App"),         &m_config);
    ctx->setContextProperty(QStringLiteral("Settings"),    &m_settings);
    ctx->setContextProperty(QStringLiteral("Panels"),      &m_panels);
    ctx->setContextProperty(QStringLiteral("Workspace"),   &m_workspace);

    qInfo() << "[KomaPaint] Starting up; Qt version =" << qVersion();
    qInfo() << "[KomaPaint] Application dir =" << QCoreApplication::applicationDirPath();

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

    m_engine.load(loadUrl);

    QObject::connect(&m_engine, &QQmlApplicationEngine::objectCreated,
                     this, [this, loadUrl](QObject* obj, const QUrl& url) {
        if (url != loadUrl) return;  // only react to the main window creation
        if (!obj) {
            qCritical() << "[KomaPaint] Failed to instantiate main QML:" << url;
            QCoreApplication::exit(-1);
            return;
        }
        m_window = qobject_cast<QQuickWindow*>(obj);
        if (!m_window) {
            qCritical() << "[KomaPaint] Root QML object is not a QQuickWindow; check that"
                        << "MainWindow.qml is an ApplicationWindow.";
            QCoreApplication::exit(-3);
            return;
        }
        qInfo() << "[KomaPaint] Main window created; showing.";
        m_window->show();
    }, Qt::QueuedConnection);
}

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
