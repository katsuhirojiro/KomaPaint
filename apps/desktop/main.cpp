#include "KomaPaintApp.h"

#include <QApplication>
#include <QDebug>
#include <QQuickStyle>

// KomaPaint desktop entry point.
//
// Creates a QApplication, instantiates the application controller, and hands
// control to KomaPaintApp. Any future bootstrapping (argument parsing,
// single-instance guard, splash screen) belongs here.

int main(int argc, char* argv[])
{
    // Route qInfo/qWarning/qCritical to both stdout and stderr so they are
    // visible regardless of how the executable is launched (Explorer, cmd,
    // CI redirection). We also write a sentinel that CI smoke tests look for.
    qInfo() << "[KomaPaint] main(): entry";
    fflush(stdout);

    // Select the Qt Quick Controls style BEFORE constructing QApplication so
    // the style plugin resolves deterministically at startup.
    QQuickStyle::setStyle("Basic");

    QApplication app(argc, argv);

    komapaint::KomaPaintApp paintApp;
    int rc = paintApp.run();
    qInfo() << "[KomaPaint] main(): exiting with code" << rc;
    fflush(stdout);
    return rc;
}
