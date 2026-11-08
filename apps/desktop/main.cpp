#include "KomaPaintApp.h"

#include <QApplication>
#include <QCoreApplication>
#include <QQuickWindow>

// KomaPaint desktop entry point.
//
// We intentionally keep this file minimal: it creates a QApplication,
// instantiates the application controller, and lets the KomaPaintApp class
// own the rest. Adding bootstrapping logic here (e.g. argument parsing) is
// fine, but subsystem setup belongs in src/app.

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // Respect high-DPI scaling on modern Windows/macOS/Linux desktops.
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    komapaint::KomaPaintApp paintApp;
    return paintApp.run();
}
