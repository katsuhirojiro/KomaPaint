#include "KomaPaintApp.h"

#include <QApplication>
#include <QQuickStyle>

// KomaPaint desktop entry point.
//
// This file is intentionally minimal: it creates a QApplication, instantiates
// the application controller, and hands control to KomaPaintApp. Any future
// bootstrapping (argument parsing, single-instance guard, splash screen)
// belongs here; subsystem setup belongs in src/app.

int main(int argc, char* argv[])
{
    // Select the Qt Quick Controls style BEFORE constructing QApplication so
    // the style plugin is resolved deterministically at startup. Setting this
    // after the QApplication exists is racy: if the QML engine cannot find
    // the Basic style plugin it fails to instantiate the root window and
    // exits with exit code -1 — matching the "process vanishes, no window"
    // symptom.
    QQuickStyle::setStyle("Basic");

    // High-DPI pixel-maps are enabled by default in Qt 6; no extra attributes
    // are required.
    QApplication app(argc, argv);

    komapaint::KomaPaintApp paintApp;
    return paintApp.run();
}
