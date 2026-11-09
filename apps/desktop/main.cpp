#include "KomaPaintApp.h"

#include <QApplication>
#include <QQuickStyle>

#include <cstdio>
#include <cstdlib>

// KomaPaint desktop entry point.

int main(int argc, char* argv[])
{
    // stdout/stderr should be line-buffered by default but we want
    // everything flushed immediately so the CI smoke test sees lines the
    // moment they are printed.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    printf("[KomaPaint] main(): entry\n");

    // Select the Qt Quick Controls style BEFORE QApplication construction.
    QQuickStyle::setStyle("Basic");

    printf("[KomaPaint] Constructing QApplication...\n");
    QApplication app(argc, argv);

    printf("[KomaPaint] QApplication constructed successfully. Starting KomaPaintApp...\n");

    komapaint::KomaPaintApp paintApp;
    int rc = paintApp.run();

    printf("[KomaPaint] main(): exiting with code %d\n", rc);
    return rc;
}
