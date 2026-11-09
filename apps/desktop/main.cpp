#include "KomaPaintApp.h"

#include <QApplication>
#include <QQuickStyle>
#include <QString>
#include <QtLogging>

#include <cstdio>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

// KomaPaint desktop entry point.

namespace {

// Route Qt log messages to a development log sink that does not require a
// console window. This matters on Windows, where komapaint is built as a
// GUI-subsystem executable: stdout/stderr are not connected to a terminal
// for a normal Explorer/Start-menu launch.
//
// Windows developers can read these messages in the Visual Studio Output
// window, Qt Creator's Application Output pane while debugging, or a
// system-wide debug-output viewer such as DebugView. Other platforms keep
// the conventional stderr sink. The handler is intentionally small and
// reentrant-safe: it only formats the message and writes it to an OS sink.
void komaPaintMessageHandler(QtMsgType type, const QMessageLogContext& context,
                             const QString& message)
{
    const char* level = "INFO";
    switch (type) {
        case QtDebugMsg:    level = "DEBUG";    break;
        case QtInfoMsg:     level = "INFO";     break;
        case QtWarningMsg:  level = "WARNING";  break;
        case QtCriticalMsg: level = "CRITICAL"; break;
        case QtFatalMsg:    level = "FATAL";    break;
    }

    QString text = QStringLiteral("[KomaPaint][%1] %2")
                       .arg(QString::fromLatin1(level), message);
    if (context.file && context.line) {
        text += QStringLiteral(" (%1:%2)")
                    .arg(QString::fromUtf8(context.file))
                    .arg(context.line);
    }
    text += u'\n';

#if defined(Q_OS_WIN)
    OutputDebugStringW(text.toStdWString().c_str());
#else
    const QByteArray utf8 = text.toLocal8Bit();
    std::fprintf(stderr, "%s", utf8.constData());
    std::fflush(stderr);
#endif
}

}  // namespace

int main(int argc, char* argv[])
{
    // Install the log sink before constructing QApplication so messages
    // emitted during Qt / QML startup are captured as well.
    qInstallMessageHandler(komaPaintMessageHandler);

    qInfo("[KomaPaint] main(): entry");

    // Select the Qt Quick Controls style BEFORE QApplication construction.
    QQuickStyle::setStyle("Basic");

    qInfo("[KomaPaint] Constructing QApplication...");
    QApplication app(argc, argv);

    qInfo("[KomaPaint] QApplication constructed successfully. Starting KomaPaintApp...");

    komapaint::KomaPaintApp paintApp;
    const int rc = paintApp.run();

    qInfo("[KomaPaint] main(): exiting with code %d", rc);
    return rc;
}
