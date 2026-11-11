#pragma once
#include <QByteArray>
#include <QFile>
#include <QString>
#include <QUuid>
#include <vector>

namespace komapaint {
// Building block only: this journal does not persist artwork, a history cursor,
// layer identities, or a complete project. It is not yet connected to UndoStack.
class HistoryJournal {
public:
    bool create(const QString& path, const QUuid& documentId, QString* error);
    bool open(const QString& path, const QUuid& documentId, QString* error);
    bool append(const QByteArray& payload, qint64* offset, QString* error);
    bool read(qint64 offset, QByteArray* payload, QString* error);
    // Verifies every record, including the final commit marker and digest.
    // Does not truncate or silently skip a damaged/incomplete tail.
    bool verify(std::vector<qint64>* offsets, QString* error);
private:
    bool writeAll(const char* data, qint64 size, QString* error);
    bool durableFlush(QString* error);
    bool rollbackAppend(qint64 start) noexcept;
    QFile m_file;
    QUuid m_documentId;
    bool m_writable = false;
};
}
