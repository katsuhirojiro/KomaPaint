#pragma once

#include <QString>
#include <QStringList>

namespace komapaint {

// Result of parsing the process command line (program name excluded).
struct CommandLineRequest {
    enum class Mode {
        Launch,       // start the editor; optionally open fileToOpen
        Associate,    // register the .koma file association for this build (headless)
        Unassociate,  // remove the .koma registration created by --associate (headless)
        Help          // print usage and exit (headless)
    };

    Mode mode = Mode::Launch;
    // Launch only: absolute or relative path (or QUrl) to open. Empty means
    // start with the default untitled document.
    QString fileToOpen;
    // Nonempty when parsing failed; the caller is expected to surface this
    // to the user and exit without starting the editor.
    QString error;
};

// Parses the KomaPaint command line. This is a pure function — it never
// touches the file system, the registry or the GUI — so every rule is
// unit-testable (see tests/unit/TestCommandLine.cpp).
class CommandLine {
public:
    static CommandLineRequest parse(const QStringList& arguments);
    static QString usageText();
};

} // namespace komapaint
