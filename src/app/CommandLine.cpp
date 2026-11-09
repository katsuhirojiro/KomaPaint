#include "CommandLine.h"

namespace komapaint {

QString CommandLine::usageText()
{
    return QStringLiteral(
        "KomaPaint\n"
        "\n"
        "Usage:\n"
        "  komapaint                    Start with a new untitled document\n"
        "  komapaint <file.koma>        Open a .koma file\n"
        "  komapaint --associate        Register .koma files to open with this KomaPaint (per-user)\n"
        "  komapaint --unassociate      Remove the .koma registration created by --associate (per-user)\n"
        "  komapaint --help             Show this help\n"
        "\n"
        "Files must start with a path name; use ./-name.koma for a file whose\n"
        "name starts with a dash.\n");
}

CommandLineRequest CommandLine::parse(const QStringList& arguments)
{
    CommandLineRequest request;

    bool sawAssociate = false;
    bool sawUnassociate = false;
    bool sawHelp = false;
    QStringList positional;

    for (const QString& argument : arguments) {
        if (argument == QStringLiteral("--associate")) { sawAssociate = true; continue; }
        if (argument == QStringLiteral("--unassociate")) { sawUnassociate = true; continue; }
        if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h")
            || argument == QStringLiteral("-help")) { sawHelp = true; continue; }
        if (!argument.isEmpty() && argument.front() == u'-') {
            request.error = QStringLiteral("Unknown option \"%1\". Try --help.").arg(argument);
            return request;
        }
        positional.append(argument);
    }

    const int headless = (sawAssociate ? 1 : 0) + (sawUnassociate ? 1 : 0) + (sawHelp ? 1 : 0);
    if (headless > 1) {
        request.error = QStringLiteral("--associate, --unassociate and --help cannot be combined.");
        return request;
    }
    if (sawAssociate || sawUnassociate || sawHelp) {
        if (!positional.isEmpty()) {
            request.error = QStringLiteral("Unexpected argument \"%1\".").arg(positional.front());
            return request;
        }
        request.mode = sawAssociate ? CommandLineRequest::Mode::Associate
                     : sawUnassociate ? CommandLineRequest::Mode::Unassociate
                                      : CommandLineRequest::Mode::Help;
        return request;
    }
    if (positional.size() > 1) {
        request.error = QStringLiteral("Too many arguments: expected at most one file to open.");
        return request;
    }
    request.mode = CommandLineRequest::Mode::Launch;
    if (!positional.isEmpty()) request.fileToOpen = positional.front();
    return request;
}

} // namespace komapaint
