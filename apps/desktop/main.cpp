#include "KomaPaintApp.h"

#include <QApplication>
#include <QQuickStyle>

#include <cstdio>
#include <windows.h>

// KomaPaint desktop entry point.

int main(int argc, char* argv[])
{
    // Attach to the parent process's console (if any) so fprintf / Qt warning
    // output is visible when launched from cmd.exe / PowerShell / CI runners
    // even though the executable is built with WIN32_EXECUTABLE=TRUE. Without
    // this, stdout/stderr are discarded and the process appears to "vanish"
    // from the parent console (which is why the CI smoke test could see no
    // output).
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
    }

    // When the KOMAPAINT_CONSOLE_DEBUG environment variable is set, also
    // allocate a standalone console (useful for development).
    if (GetEnvironmentVariableW(L"KOMAPAINT_CONSOLE_DEBUG", nullptr, 0) > 0) {
        AllocConsole();
        FILE* fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
    }

    fprintf(stdout, "[KomaPaint] main(): entry\n");
    fflush(stdout);

    // Select the Qt Quick Controls style BEFORE QApplication construction so
    // the style plugin resolves deterministically at startup.
    QQuickStyle::setStyle("Basic");

    QApplication app(argc, argv);

    komapaint::KomaPaintApp paintApp;
    int rc = paintApp.run();

    fprintf(stdout, "[KomaPaint] main(): exiting with code %d\n", rc);
    fflush(stdout);

    FreeConsole();
    return rc;
}
