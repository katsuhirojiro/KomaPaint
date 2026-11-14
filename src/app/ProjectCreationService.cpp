#include "ProjectCreationService.h"

#include <QColor>
#include <cmath>

#include "../canvas/CanvasDocument.h"
#include "../core/NewProjectSettings.h"

namespace komapaint {

ProjectCreationService::ProjectCreationService(QObject* parent)
    : QObject(parent)
{
}

void ProjectCreationService::setError(const QString& error)
{
    if (m_creationError == error) return;
    m_creationError = error;
    emit creationErrorChanged();
}

ProjectCreationResult ProjectCreationService::validate(
    const QString& name, int width, int height,
    bool transparent, const QString& backgroundColor,
    qreal dpi) const
{
    ProjectCreationResult result;

    if (name.trimmed().isEmpty()) {
        result.error = QStringLiteral("Enter a project name.");
        return result;
    }

    if (!CanvasDocument::validSize(QSize(width, height))) {
        result.error = QStringLiteral("Canvas dimensions must be between 1 and %1 pixels.")
            .arg(CanvasDocument::MaxDimension);
        return result;
    }

    QColor bgColor(backgroundColor);
    if (!bgColor.isValid()) {
        result.error = QStringLiteral("Background colour is invalid.");
        return result;
    }
    if (bgColor.alpha() != 255) {
        result.error = QStringLiteral("Background colour must be fully opaque.");
        return result;
    }

    if (!std::isfinite(dpi) || dpi <= 0) {
        result.error = QStringLiteral("Resolution (DPI) must be greater than zero.");
        return result;
    }

    result.ok = true;
    return result;
}

CanvasDocument* ProjectCreationService::createDocument(
    const QString& name, int width, int height,
    bool transparent, const QString& backgroundColor,
    qreal dpi)
{
    // Validate first; never construct a document from invalid settings.
    auto validation = validate(name, width, height, transparent, backgroundColor, dpi);
    if (!validation.ok) {
        setError(validation.error);
        return nullptr;
    }

    QColor bgColor(backgroundColor);
    auto* doc = buildDocument(name.trimmed(), width, height, transparent, bgColor, dpi);

    setError(QString()); // clear any previous error
    return doc;
}

CanvasDocument* ProjectCreationService::buildDocument(
    const QString& name, int width, int height,
    bool transparent, const QColor& backgroundColor,
    qreal dpi)
{
    auto* doc = new CanvasDocument(this);
    doc->setTitle(name);
    doc->setSize(QSize(width, height));
    doc->setDpi(dpi);
    doc->setBackgroundColor(backgroundColor);
    doc->setTransparentBackground(transparent);
    doc->setModified(false);
    return doc;
}

} // namespace komapaint
