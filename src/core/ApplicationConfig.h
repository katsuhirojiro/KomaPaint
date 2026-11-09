#pragma once

#include "Version.h"

#include <QObject>
#include <QString>
#include <QVersionNumber>

namespace komapaint {

// ApplicationConfig exposes immutable metadata about this KomaPaint build
// (name, version, paths, etc.) to QML and to C++ code. It is instantiated
// once and exposed as a context property.

class ApplicationConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name           READ name           CONSTANT)
    Q_PROPERTY(QString organization   READ organization   CONSTANT)
    Q_PROPERTY(QString version        READ version        CONSTANT)
    Q_PROPERTY(QString versionDisplay READ versionDisplay CONSTANT)
    Q_PROPERTY(QString copyrightYear  READ copyrightYear  CONSTANT)
    Q_PROPERTY(bool    alphaBuild     READ isAlphaBuild   CONSTANT)

public:
    explicit ApplicationConfig(QObject* parent = nullptr);

    QString name() const { return QStringLiteral(KOMAPAINT_PRETTY_NAME); }
    QString organization() const { return QStringLiteral(KOMAPAINT_ORGANIZATION); }
    QString version() const { return QStringLiteral(KOMAPAINT_VERSION_STRING); }
    QString versionDisplay() const;
    QString copyrightYear() const { return QStringLiteral("2026"); }
    bool    isAlphaBuild() const { return true; }

    static QVersionNumber versionNumber();
};

}  // namespace komapaint
