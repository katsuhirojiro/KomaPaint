#include "ApplicationConfig.h"

#include <QVersionNumber>

namespace komapaint {

ApplicationConfig::ApplicationConfig(QObject* parent) : QObject(parent) {}

QString ApplicationConfig::versionDisplay() const
{
    // e.g. "KomaPaint 0.1.0 (Alpha)"
    return QStringLiteral("%1 %2%3")
        .arg(KOMAPAINT_PRETTY_NAME, KOMAPAINT_VERSION_STRING,
             isAlphaBuild() ? QStringLiteral(" (Alpha)") : QString());
}

QVersionNumber ApplicationConfig::versionNumber()
{
    return QVersionNumber(KOMAPAINT_VERSION_MAJOR,
                          KOMAPAINT_VERSION_MINOR,
                          KOMAPAINT_VERSION_PATCH);
}

}  // namespace komapaint
