#include "FileAssociation.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shlobj.h> // SHChangeNotify
#include <string>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace komapaint {

namespace {

constexpr wchar_t kProgId[] = L"KomaPaint.Illustration";
constexpr wchar_t kDotKomaSubKey[] = L"Software\\Classes\\.koma";
constexpr wchar_t kProgIdSubKey[] = L"Software\\Classes\\KomaPaint.Illustration";
constexpr wchar_t kProgIdDefaultIconSubKey[] = L"Software\\Classes\\KomaPaint.Illustration\\DefaultIcon";
constexpr wchar_t kProgIdCommandSubKey[] = L"Software\\Classes\\KomaPaint.Illustration\\shell\\open\\command";
constexpr wchar_t kSupportedTypesSubKey[] = L"Software\\Classes\\Applications\\komapaint.exe\\SupportedTypes";

#if defined(Q_OS_WIN)

QString lastErrorMessage(const QString& what)
{
    const DWORD code = GetLastError();
    return QStringLiteral("%1 (Windows error %2).").arg(what).arg(code);
}

bool readDefaultString(const wchar_t* subKey, std::wstring* out)
{
    HKEY key = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_QUERY_VALUE, &key);
    if (openStatus != ERROR_SUCCESS) return false;
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    DWORD type = REG_SZ;
    const LSTATUS queryStatus = RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);
    if (queryStatus != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    // Strip the byte count back to characters (REG_SZ is NUL-terminated within size).
    const qsizetype maxChars = static_cast<qsizetype>(size / sizeof(wchar_t));
    // REG_SZ data is stored with a trailing NUL; copy up to (but excluding) it.
    size_t len = 0;
    while (len < static_cast<size_t>(maxChars) && buffer[len] != L'\0') ++len;
    out->assign(buffer, len);
    return true;
}

bool writeDefaultString(const wchar_t* subKey, const std::wstring& data, QString* error)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        if (error) *error = lastErrorMessage(QStringLiteral("Could not create the registry key HKCU\\%1").arg(QString::fromWCharArray(subKey)));
        return false;
    }
    const LSTATUS status = RegSetValueExW(key, nullptr, 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(data.c_str()),
                                          static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        if (error) *error = lastErrorMessage(QStringLiteral("Could not write the registry value HKCU\\%1").arg(QString::fromWCharArray(subKey)));
        return false;
    }
    return true;
}

bool writeNamedString(const wchar_t* subKey, const wchar_t* name, const std::wstring& data, QString* error)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        if (error) *error = lastErrorMessage(QStringLiteral("Could not create the registry key HKCU\\%1").arg(QString::fromWCharArray(subKey)));
        return false;
    }
    const LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ,
                                          reinterpret_cast<const BYTE*>(data.c_str()),
                                          static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        if (error) *error = lastErrorMessage(QStringLiteral("Could not write the registry value HKCU\\%1\\%2")
                                                 .arg(QString::fromWCharArray(subKey), QString::fromWCharArray(name)));
        return false;
    }
    return true;
}

void deleteTree(const wchar_t* subKey)
{
    // RegDeleteTreeW removes the key and all of its subkeys/values (Vista+).
    RegDeleteTreeW(HKEY_CURRENT_USER, subKey);
}

void deleteSingleValueAndEmptyKey(const wchar_t* subKey, const wchar_t* name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, name);
        RegCloseKey(key);
    }
    // Delete the key only if it has no subkeys left (RegDeleteKeyExW fails otherwise).
    RegDeleteKeyExW(HKEY_CURRENT_USER, subKey, KEY_WOW64_64KEY, 0);
}

void notifyShellOfAssociationChange()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

QString openCommandValue(const QString& exePath)
{
    // "C:\path\komapaint.exe" "%1"
    return QStringLiteral("\"%1\" \"%2\"").arg(exePath, QStringLiteral("%1"));
}

QString defaultIconValue(const QString& exePath)
{
    return exePath + QStringLiteral(",0");
}

#endif // Q_OS_WIN

} // namespace

QString FileAssociation::currentExecutablePath()
{
#if defined(Q_OS_WIN)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return QString::fromWCharArray(buffer.c_str(), static_cast<int>(buffer.size()));
    }
    return QString();
#else
    const QString fromEnv = QString::fromLocal8Bit(qgetenv("KOMAPAINT_APP_PATH"));
    if (!fromEnv.isEmpty()) return fromEnv;
    if (QCoreApplication::instance())
        return QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    return QString();
#endif
}

bool FileAssociation::registerAssociation(const QString& exePath, QString* error)
{
#if defined(Q_OS_WIN)
    if (exePath.isEmpty() || !QFileInfo::exists(exePath)) {
        if (error) *error = QStringLiteral("The KomaPaint executable path \"%1\" does not exist.").arg(exePath);
        return false;
    }
    // 1) Extension -> ProgID binding
    if (!writeDefaultString(kDotKomaSubKey, kProgId, error)) return false;
    // 2) ProgID: display name, icon, shell verb
    if (!writeDefaultString(kProgIdSubKey, L"KomaPaint illustration", error)) return false;
    if (!writeDefaultString(kProgIdDefaultIconSubKey, defaultIconValue(exePath).toStdWString(), error)) return false;
    if (!writeDefaultString(kProgIdCommandSubKey, openCommandValue(exePath).toStdWString(), error)) return false;
    // 3) Open-with hint
    if (!writeNamedString(kSupportedTypesSubKey, L".koma", L"", error)) return false;
    notifyShellOfAssociationChange();
    return true;
#else
    Q_UNUSED(exePath);
    if (error) *error = QStringLiteral("The .koma file association is only supported on Windows.");
    return false;
#endif
}

bool FileAssociation::unregisterAssociation(QString* error)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(error);
    // Only remove the .koma ProgID binding when it is ours. A user's own
    // binding (or another editor's) is left untouched.
    std::wstring dotKomaDefault;
    if (readDefaultString(kDotKomaSubKey, &dotKomaDefault) && dotKomaDefault == kProgId) {
        deleteTree(kDotKomaSubKey);
    }
    // The ProgID tree is exclusively ours by name; remove it wholesale.
    deleteTree(kProgIdSubKey);
    // The Open-with hint: remove the .koma value only.
    deleteSingleValueAndEmptyKey(kSupportedTypesSubKey, L".koma");
    notifyShellOfAssociationChange();
    return true;
#else
    if (error) *error = QStringLiteral("The .koma file association is only supported on Windows.");
    return false;
#endif
}

bool FileAssociation::isRegisteredFor(const QString& exePath)
{
#if defined(Q_OS_WIN)
    std::wstring dotKomaDefault;
    if (!readDefaultString(kDotKomaSubKey, &dotKomaDefault) || dotKomaDefault != kProgId) return false;
    std::wstring command;
    if (!readDefaultString(kProgIdCommandSubKey, &command)) return false;
    return command == openCommandValue(exePath).toStdWString();
#else
    Q_UNUSED(exePath);
    return false;
#endif
}

} // namespace komapaint
