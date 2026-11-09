#include "KomaPaintApp.h"

#include <QApplication>
#include <QQuickStyle>

#include <cstdio>

// KomaPaint desktop entry point.

int main(int argc, char* argv[])
{
    // Set the Qt Quick Controls style BEFORE QApplication construction so the
    // style plugin resolves deterministically at startup.
    QQuickStyle::setStyle("Basic");

    QApplication app(argc, argv);

    komapaint::KomaPaintApp paintApp;
    int rc = paintApp.run();

    fprintf(stdout, "[KomaPaint] main(): exiting with code %d\n", rc);
    fflush(stdout);
    return rc;
}
