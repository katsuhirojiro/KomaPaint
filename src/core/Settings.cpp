#include "Settings.h"

#include <QSettings>

namespace komapaint {

namespace keys {
constexpr const char* kWindowSize       = "ui/windowSize";
constexpr const char* kWindowMaximized  = "ui/windowMaximized";
constexpr const char* kLanguage         = "app/language";
constexpr const char* kLastOpenDir      = "files/lastOpenDirectory";
}  // namespace keys

Settings::Settings(const QString& organization, const QString& application,
                   QObject* parent)
    : QObject(parent), d(new QSettings(organization, application))
{
}

Settings::~Settings()
{
    d->sync();
    delete d;
}

QVariant Settings::value(const QString& key, const QVariant& defaultValue) const
{
    return d->value(key, defaultValue);
}

void Settings::setValue(const QString& key, const QVariant& value)
{
    if (d->value(key) == value) return;
    d->setValue(key, value);
    emit valueChanged(key);
}

QSize Settings::windowSize() const
{
    return d->value(keys::kWindowSize, QSize(1440, 900)).toSize();
}

void Settings::setWindowSize(const QSize& size)
{
    setValue(keys::kWindowSize, size);
}

bool Settings::windowMaximized() const
{
    return d->value(keys::kWindowMaximized, false).toBool();
}

void Settings::setWindowMaximized(bool maximized)
{
    setValue(keys::kWindowMaximized, maximized);
}

QString Settings::language() const
{
    return d->value(keys::kLanguage, "en").toString();
}

void Settings::setLanguage(const QString& locale)
{
    setValue(keys::kLanguage, locale);
}

QString Settings::lastOpenDirectory() const
{
    return d->value(keys::kLastOpenDir, QString()).toString();
}

void Settings::setLastOpenDirectory(const QString& path)
{
    setValue(keys::kLastOpenDir, path);
}

}  // namespace komapaint
