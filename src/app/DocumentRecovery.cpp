#include "DocumentRecovery.h"

#include "../canvas/CanvasDocument.h"
#include "../files/DocumentFile.h"
#include "../layers/Layer.h"
#include "../layers/LayerModel.h"

#include <QFile>

#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace komapaint {
namespace {

// QSaveFile already replaces the destination atomically. Flushing the
// committed file makes the snapshot's bytes durable before the workspace
// index starts referring to them, so an interrupted shutdown cannot leave an
// index entry pointing at a partially persisted snapshot.
bool flushCommittedFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) return false;
#ifdef Q_OS_WIN
    return _commit(file.handle()) == 0;
#else
    return ::fsync(file.handle()) == 0;
#endif
}

// The illustration adapter. It delegates to DocumentFile, which owns the
// .koma version 2 illustration payload and therefore keeps normal saving and
// recovery snapshots byte-compatible for this document type.
class IllustrationRecoveryAdapter final : public DocumentRecoveryAdapter
{
public:
    QString typeId() const override
    {
        return QString::fromLatin1(CanvasDocument::IllustrationTypeId);
    }
    QString displayName() const override { return QStringLiteral("Illustration"); }
    bool supports(const CanvasDocument& document) const override
    {
        return document.documentType() == typeId();
    }
    bool save(const CanvasDocument& document, const QString& path, QString* error) const override
    {
        if (!DocumentFile::save(document, path, error)) return false;
        if (!flushCommittedFile(path)) {
            if (error) *error = QStringLiteral("The recovery snapshot could not be synchronized to disk.");
            return false;
        }
        return true;
    }
    CanvasDocument* open(const QString& path, QString* error) const override
    {
        CanvasDocument* document = DocumentFile::open(path, error);
        if (document && !supports(*document)) {
            delete document;
            if (error) *error = QStringLiteral("The recovery snapshot contains a different document type.");
            return nullptr;
        }
        return document;
    }
    quint64 estimatedSnapshotBytes(const CanvasDocument& document) const override
    {
        // .koma stores the allocated tile pixels uncompressed, so the pixel
        // bytes plus a generous allowance for names and tables is an upper
        // estimate of the file size.
        quint64 bytes = 64U * 1024U;
        const auto* model = document.layers();
        for (int i = 0; i < model->rowCount(); ++i)
            bytes += static_cast<quint64>(model->layerAt(i)->tiles().allocatedPixelBytes()) + 8U * 1024U;
        return bytes;
    }
};

}  // namespace

QString DocumentRecoveryRegistry::illustrationTypeId()
{
    return QString::fromLatin1(CanvasDocument::IllustrationTypeId);
}

DocumentRecoveryRegistry::DocumentRecoveryRegistry()
{
    m_adapters.push_back(std::make_unique<IllustrationRecoveryAdapter>());
}

DocumentRecoveryRegistry& DocumentRecoveryRegistry::instance()
{
    static DocumentRecoveryRegistry registry;
    return registry;
}

bool DocumentRecoveryRegistry::registerAdapter(std::unique_ptr<DocumentRecoveryAdapter> adapter)
{
    if (!adapter) return false;
    const QString id = adapter->typeId();
    if (id.isEmpty() || adapterForType(id)) return false;
    m_adapters.push_back(std::move(adapter));
    return true;
}

const DocumentRecoveryAdapter* DocumentRecoveryRegistry::adapterForType(const QString& typeId) const
{
    if (typeId.isEmpty()) return nullptr;
    for (const auto& adapter : m_adapters)
        if (adapter->typeId() == typeId) return adapter.get();
    return nullptr;
}

const DocumentRecoveryAdapter* DocumentRecoveryRegistry::adapterForDocument(const CanvasDocument& document) const
{
    for (const auto& adapter : m_adapters)
        if (adapter->supports(document)) return adapter.get();
    return nullptr;
}

QStringList DocumentRecoveryRegistry::typeIds() const
{
    QStringList ids;
    for (const auto& adapter : m_adapters) ids.append(adapter->typeId());
    return ids;
}

}  // namespace komapaint
