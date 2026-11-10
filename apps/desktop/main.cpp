#include "KomaPaintApp.h"
#include "CommandLine.h"
#include "FileAssociation.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QMessageBox>
#include <QQuickStyle>
#include <QString>
#include <QStringList>
#include <QtLogging>

#include <cstdio>
#include <cstring>

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
    // Opt-in, CI-only startup diagnostics; normal GUI launches still use the
    // debugger sink and do not create a log file.
    const QString smokeLog = qEnvironmentVariable("KOMAPAINT_SMOKE_LOG");
    if (!smokeLog.isEmpty()) {
        QFile log(smokeLog);
        if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
            const QByteArray record = QByteArray::number(GetTickCount64()) + " " + text.toUtf8();
            log.write(record);
        }
    }
    OutputDebugStringW(text.toStdWString().c_str());
#else
    const QByteArray utf8 = text.toLocal8Bit();
    std::fprintf(stderr, "%s", utf8.constData());
    std::fflush(stderr);
#endif
}

// Headless commands are handled before any GUI object is created so they
// work in installers, CI and non-interactive sessions (no QApplication, no
// window, no event loop). Detection uses exact ASCII comparisons against
// the raw argv; everything else falls through to the normal GUI path where
// Qt properly decodes a Unicode command line into QString.
int runHeadlessIfRequested(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) continue;
        const bool associate = std::strcmp(arg, "--associate") == 0;
        const bool unassociate = std::strcmp(arg, "--unassociate") == 0;
        const bool help = std::strcmp(arg, "--help") == 0;
        if (!associate && !unassociate && !help) continue;

        // Reject combinations/extra positionals up front: this pre-scan owns
        // the flag entirely, so be strict rather than guess.
        for (int j = i + 1; j < argc; ++j) {
            if (argv[j] == nullptr) continue;
            const QString message = QStringLiteral("Option \"%1\" cannot be combined with other arguments. Try --help.")
                                        .arg(QString::fromLocal8Bit(arg));
            std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());
            return 2;
        }

        if (help) {
            const QString usage = komapaint::CommandLine::usageText();
            std::fprintf(stderr, "%s", usage.toLocal8Bit().constData());
            return 0;
        }
        const QString exePath = komapaint::FileAssociation::currentExecutablePath();
        QString error;
        const bool ok = associate ? komapaint::FileAssociation::registerAssociation(exePath, &error)
                                  : komapaint::FileAssociation::unregisterAssociation(&error);
        if (ok) {
            std::fprintf(stderr, "KomaPaint: .koma file association %s successfully.\n",
                         associate ? "registered" : "removed");
            return 0;
        }
        std::fprintf(stderr, "KomaPaint: could not %s the .koma file association: %s\n",
                     associate ? "register" : "remove",
                     error.toLocal8Bit().constData());
        return 3;
    }
    return -1; // not a headless request
}

}  // namespace

int main(int argc, char* argv[])
{
    // Install the log sink before constructing QApplication so messages
    // emitted during Qt / QML startup are captured as well.
    qInstallMessageHandler(komaPaintMessageHandler);

    qInfo("[KomaPaint] main(): entry");

    const int headless = runHeadlessIfRequested(argc, argv);
    if (headless != -1) return headless;

    // Select the Qt Quick Controls style BEFORE QApplication construction.
    QQuickStyle::setStyle("Basic");

    qInfo("[KomaPaint] Constructing QApplication...");
    QApplication app(argc, argv);

    // Qt has decoded the (possibly non-ASCII) command line properly now.
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    const komapaint::CommandLineRequest request = komapaint::CommandLine::parse(arguments);
    if (!request.error.isEmpty()) {
        qWarning("[KomaPaint] command line error: %s", qUtf8Printable(request.error));
        QMessageBox::warning(nullptr, QStringLiteral("KomaPaint"),
                             request.error + QStringLiteral("\n\n") + komapaint::CommandLine::usageText());
        return 2;
    }
    // Help is handled by the headless pre-scan; this branch only guards the
    // case where the flags were combined with arguments Qt had to decode.
    if (request.mode == komapaint::CommandLineRequest::Mode::Help) {
        std::fprintf(stderr, "%s", komapaint::CommandLine::usageText().toLocal8Bit().constData());
        return 0;
    }

    qInfo("[KomaPaint] QApplication constructed successfully. Starting KomaPaintApp...");

    komapaint::KomaPaintApp paintApp(request.fileToOpen);
    const int rc = paintApp.run();

    qInfo("[KomaPaint] main(): exiting with code %d", rc);
    return rc;
}
