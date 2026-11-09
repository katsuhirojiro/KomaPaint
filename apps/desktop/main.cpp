#include "KomaPaintApp.h"

#include <QApplication>
#include <QQuickStyle>

#include <cstdio>
#include <cstring>
#include <windows.h>

// KomaPaint desktop entry point.

// When the KOMAPAINT_LOG_FILE environment variable is set, reopen stdout and
// stderr to that path. This is used by the CI smoke test to capture diagnostic
// output from the WIN32_EXECUTABLE GUI build (which otherwise has no attached
// console). We use freopen() on the well-known stdio FILE objects so every
// printf / fprintf / Qt message that goes to stdout/stderr is flushed to disk
// unbuffered.
static void redirectStdioToLogFile()
{
    const DWORD bufSize = GetEnvironmentVariableW(L"KOMAPAINT_LOG_FILE", nullptr, 0);
    if (bufSize == 0) return;
    wchar_t* wpath = new wchar_t[bufSize];
    GetEnvironmentVariableW(L"KOMAPAINT_LOG_FILE", wpath, bufSize);
    char pathA[MAX_PATH] = {0};
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, pathA, MAX_PATH, nullptr, nullptr);
    delete[] wpath;
    if (freopen(pathA, "w", stdout) == nullptr) { return; }
    freopen(pathA, "a", stderr);  // append to the same file
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

int main(int argc, char* argv[])
{
    redirectStdioToLogFile();

    printf("[KomaPaint] main(): entry\n");
    fflush(stdout);

    // Select the Qt Quick Controls style BEFORE QApplication construction so
    // the style plugin resolves deterministically at startup.
    QQuickStyle::setStyle("Basic");

    QApplication app(argc, argv);

    komapaint::KomaPaintApp paintApp;
    int rc = paintApp.run();

    printf("[KomaPaint] main(): exiting with code %d\n", rc);
    fflush(stdout);
    return rc;
}
