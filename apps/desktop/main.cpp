#include "KomaPaintApp.h"

#include <QApplication>

// KomaPaint desktop entry point.
//
// This file is intentionally minimal: it creates a QApplication, instantiates
// the application controller, and hands control to KomaPaintApp. Any future
// bootstrapping (argument parsing, single-instance guard, splash screen)
// belongs here; subsystem setup belongs in src/app.

int main(int argc, char* argv[])
{
    // High-DPI pixel-maps are enabled by default in Qt 6; no extra attributes
    // are required.
    QApplication app(argc, argv);

    komapaint::KomaPaintApp paintApp;
    return paintApp.run();
}
