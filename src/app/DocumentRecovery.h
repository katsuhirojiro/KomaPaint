#pragma once

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace komapaint {

class CanvasDocument;

// Shared recovery interface for every native document type.
//
// Workspace recovery never serializes a project itself. Each document type
// supplies one adapter that knows how to write, validate and reload that
// type's snapshot. The workspace stores the adapter's stable typeId in its
// recovery index and refuses to attach a snapshot to a document unless the
// recorded type, the loaded document and the adapter all agree.
//
// Adding a future document type (animation, comic page, multi-page comic)
// means implementing this interface and registering one adapter at startup;
// WorkspaceState, its index format and the recovery UI do not change.
class DocumentRecoveryAdapter
{
public:
    virtual ~DocumentRecoveryAdapter() = default;

    // Stable identifier persisted in the workspace index; never localised.
    virtual QString typeId() const = 0;
    // User-facing name for recovery diagnostics.
    virtual QString displayName() const = 0;
    // True when this adapter owns the given in-memory document. Used both to
    // tag new projects on creation and to double-check snapshots on restore.
    virtual bool supports(const CanvasDocument& document) const = 0;
    // Atomically writes a self-contained recovery snapshot for the document.
    // Must leave an existing snapshot untouched on failure.
    virtual bool save(const CanvasDocument& document, const QString& path, QString* error) const = 0;
    // Fully validates a snapshot and returns a detached, unmodified document.
    // Returns nullptr and sets *error on any mismatch or corruption.
    virtual CanvasDocument* open(const QString& path, QString* error) const = 0;
};

class DocumentRecoveryRegistry
{
public:
    static DocumentRecoveryRegistry& instance();

    // Registers an additional native document type. Returns false for an
    // empty or duplicate typeId. Built-in types are registered on first use.
    bool registerAdapter(std::unique_ptr<DocumentRecoveryAdapter> adapter);
    const DocumentRecoveryAdapter* adapterForType(const QString& typeId) const;
    const DocumentRecoveryAdapter* adapterForDocument(const CanvasDocument& document) const;
    QStringList typeIds() const;

    // Stable ids of the types this build knows. Illustration is the only type
    // defined by the current .koma version 2 format.
    static QString illustrationTypeId();

private:
    DocumentRecoveryRegistry();
    std::vector<std::unique_ptr<DocumentRecoveryAdapter>> m_adapters;
};

}  // namespace komapaint
