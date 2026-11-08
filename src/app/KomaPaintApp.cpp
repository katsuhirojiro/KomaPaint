#include "KomaPaintApp.h"
#include "core/Version.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QQuickWindow>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
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

    const QString qmlDir = QString::fromUtf8(KOMAPAINT_QML_DIR);
    m_engine.addImportPath(qmlDir);

    const QString mainQml = qmlDir + QStringLiteral("/workspaces/MainWindow.qml");
    QObject::connect(&m_engine, &QQmlApplicationEngine::objectCreated,
                     this, [this](QObject* obj, const QUrl& url) {
        if (!obj && url.toString().endsWith(QStringLiteral("MainWindow.qml"))) {
            qCritical() << "Failed to load main QML:" << url;
            QCoreApplication::exit(-1);
        }
        m_window = qobject_cast<QQuickWindow*>(obj);
    }, Qt::QueuedConnection);

    m_engine.load(QUrl::fromLocalFile(mainQml));
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
