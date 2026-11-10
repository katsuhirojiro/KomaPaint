#pragma once

#include <QString>

namespace komapaint {

// Manages the per-user (HKCU) Windows registration that makes double-clicking
// a .koma file open it in this KomaPaint build, in Explorer and in
// "Open with" dialogs. Per-user registration requires no administrator
// rights and never touches machine-wide settings. Registration is only ever
// under HKCU\Software\Classes and is limited to:
//
//   HKCU\Software\Classes\.koma                      (default = ProgID)
//   HKCU\Software\Classes\KomaPaint.Illustration     (ProgID: icon + shell\open\command)
//   HKCU\Software\Classes\Applications\komapaint.exe\SupportedTypes   (value ".koma")
//
// Unregistering removes only what this feature writes, and only the .koma
// ProgID binding we own — a user's own overrides elsewhere are left alone.
// Serialization never depends on these keys; this is a convenience for the
// operating system only. Unsupported platforms get a clear error message.
class FileAssociation {
public:
    // Absolute path of the running executable. Prefers the OS-reported path
    // (GetModuleFileNameW on Windows) so double-click registration always
    // points at the actual binary, even if PATH was involved.
    static QString currentExecutablePath();

    // Writes the per-user registration. On success returns true. On failure
    // returns false and, if error is given, a user-readable message.
    static bool registerAssociation(const QString& exePath, QString* error = nullptr);

    // Removes the per-user registration written by registerAssociation.
    // Returns true when no ours-anymore state remains (including "never
    // registered"). On failure returns false and, if error is given, a
    // user-readable message.
    static bool unregisterAssociation(QString* error = nullptr);

    // True when the HKCU registration exists and points at exePath.
    static bool isRegisteredFor(const QString& exePath);
};

} // namespace komapaint
