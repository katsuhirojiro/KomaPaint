#pragma once

#include <QObject>
#include <QString>

namespace komapaint {

class CanvasDocument;

// Explicit result of project creation validation. ok=true means the settings
// are sufficient to construct a project; ok=false carries a user-readable
// reason why they are not.
struct ProjectCreationResult {
    bool ok = false;
    QString error;
};

// Project creation service — separates UI from document construction.
//
// Receives typed creation settings, validates them, constructs a detached
// candidate CanvasDocument, and returns it (or nullptr on failure). The last
// validation error is readable via creationError().
//
// No UI types (QColor, QML) appear here — background colour is a QString,
// converted to QColor at the service boundary.
class ProjectCreationService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString creationError READ creationError NOTIFY creationErrorChanged)

public:
    explicit ProjectCreationService(QObject* parent = nullptr);

    QString creationError() const { return m_creationError; }

    // Validate settings without creating anything. Returns ok=true with an
    // empty error when the settings are sufficient to create a project.
    ProjectCreationResult validate(const QString& name, int width, int height,
                                   bool transparent, const QString& backgroundColor,
                                   qreal dpi) const;

    // Construct a detached CanvasDocument candidate with the given settings.
    // Returns the document (parented to this service) on success, or nullptr
    // on failure with creationError() set. Ownership transfers to the caller.
    CanvasDocument* createDocument(const QString& name, int width, int height,
                                   bool transparent, const QString& backgroundColor,
                                   qreal dpi);

signals:
    void creationErrorChanged();

private:
    void setError(const QString& error);
    CanvasDocument* buildDocument(const QString& name, int width, int height,
                                  bool transparent, const QColor& backgroundColor,
                                  qreal dpi);

    QString m_creationError;
};

} // namespace komapaint
