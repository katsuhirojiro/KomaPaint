#include "HistoryJournal.h"
#include <QCryptographicHash>
#include <QtEndian>
#include <limits>
#include <algorithm>
#include <utility>
#include <new>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

namespace komapaint {
namespace {
constexpr char fileMagic[8] = {'K','P','H','I','S','T','0','1'};
constexpr char frameMagic[4] = {'R','E','C','1'};
constexpr char doneMagic[4] = {'E','N','D','1'};
constexpr qint64 fileHeader = 24;
constexpr qint64 frameHeader = 44; // magic, length, SHA-256
constexpr qint64 frameFooter = 4;
void setError(QString* out, const QString& text) { if (out) *out = text; }
bool same(const QByteArray& bytes, const char* expected, qsizetype size) {
    return bytes.size() == size && std::equal(bytes.begin(), bytes.end(), expected);
}
}
bool HistoryJournal::writeAll(const char* data, qint64 size, QString* error)
{
    while (size > 0) {
        if (inject(IoOperation::Write)) { setError(error, QStringLiteral("History disk write failed (injected).")); return false; }
        const auto written = m_file.write(data, size);
        if (written <= 0) { setError(error, QStringLiteral("History disk write failed: ") + m_file.errorString()); return false; }
        data += written; size -= written;
    }
    return true;
}
bool HistoryJournal::durableFlush(QString* error)
{
    if (inject(IoOperation::Flush)) { setError(error, QStringLiteral("History disk flush failed (injected).")); return false; }
    if (!m_file.flush()) { setError(error, QStringLiteral("History disk flush failed: ") + m_file.errorString()); return false; }
    if (inject(IoOperation::Sync)) { setError(error, QStringLiteral("History disk synchronization failed (injected).")); return false; }
#ifdef Q_OS_WIN
    if (_commit(m_file.handle()) != 0) {
#else
    if (::fsync(m_file.handle()) != 0) {
#endif
        setError(error, QStringLiteral("History disk synchronization failed.")); return false;
    }
    return true;
}
bool HistoryJournal::create(const QString& path, const QUuid& documentId, QString* error)
{
    if (m_file.isOpen() || documentId.isNull()) { setError(error, QStringLiteral("Invalid history journal or document identity.")); return false; }
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadWrite | QIODevice::NewOnly)) { setError(error, QStringLiteral("Cannot create history journal: ") + m_file.errorString()); return false; }
    const auto id = documentId.toRfc4122();
    if (!writeAll(fileMagic, 8, error) || !writeAll(id.constData(), 16, error) || !durableFlush(error)) {
        m_file.remove(); return false;
    }
    m_documentId = documentId;
    m_end = fileHeader;
    m_writable = true; return true;
}
bool HistoryJournal::open(const QString& path, const QUuid& documentId, QString* error)
{
    if (m_file.isOpen() || documentId.isNull()) { setError(error, QStringLiteral("Invalid history journal or document identity.")); return false; }
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadWrite)) { setError(error, QStringLiteral("Cannot open history journal: ") + m_file.errorString()); return false; }
    if (!same(m_file.read(8), fileMagic, 8) || m_file.read(16) != documentId.toRfc4122()) {
        setError(error, QStringLiteral("History journal format or document identity does not match.")); m_file.close(); return false;
    }
    m_documentId = documentId;
    // Do not append to a damaged or interrupted journal.
    if (!verify(nullptr, error)) { m_file.close(); return false; }
    m_end = m_file.size();
    m_writable = true; return true;
}
bool HistoryJournal::rollbackAppend(qint64 start) noexcept
{
    if (!m_file.resize(start) || !m_file.seek(start) || !m_file.flush()) { m_writable = false; return false; }
#ifdef Q_OS_WIN
    if (_commit(m_file.handle()) != 0) { m_writable = false; return false; }
#else
    if (::fsync(m_file.handle()) != 0) { m_writable = false; return false; }
#endif
    return true;
}
bool HistoryJournal::append(const QByteArray& payload, qint64* offset, QString* error)
{
    if (!m_file.isOpen() || !m_writable || !offset) { setError(error, QStringLiteral("History journal is not writable.")); return false; }
    const auto start = m_file.size();
    if (start != m_end) {
        m_writable = false;
        setError(error, QStringLiteral("History journal changed unexpectedly; further writes are disabled."));
        return false;
    }
    if (start < fileHeader || start > std::numeric_limits<qint64>::max() - frameHeader - frameFooter
        || payload.size() > std::numeric_limits<qint64>::max() - start - frameHeader - frameFooter || !m_file.seek(start)) {
        setError(error, QStringLiteral("History journal is too large or cannot seek.")); return false;
    }
    try {
        const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
        const quint64 length = qToLittleEndian(static_cast<quint64>(payload.size()));
        if (!writeAll(frameMagic, 4, error) || !writeAll(reinterpret_cast<const char*>(&length), 8, error)
            || !writeAll(digest.constData(), digest.size(), error) || !writeAll(payload.constData(), payload.size(), error)
            || !writeAll(doneMagic, 4, error) || !durableFlush(error)) {
            rollbackAppend(start); return false;
        }
        QByteArray check;
        if (!read(start, &check, error) || check != payload) { rollbackAppend(start); return false; }
        m_end = start + frameHeader + payload.size() + frameFooter;
        *offset = start;
        return true;
    } catch (const std::bad_alloc&) {
        setError(error, QStringLiteral("Not enough memory to validate the history record."));
        rollbackAppend(start); return false;
    }
}
bool HistoryJournal::read(qint64 offset, QByteArray* payload, QString* error)
{
    if (inject(IoOperation::Read)) { setError(error, QStringLiteral("History disk read failed (injected).")); return false; }
    const auto fileSize = m_file.size();
    if (!m_file.isOpen() || !payload || offset < fileHeader || offset > fileSize || fileSize - offset < frameHeader + frameFooter || !m_file.seek(offset)) {
        setError(error, QStringLiteral("History record is missing or incomplete.")); return false;
    }
    if (!same(m_file.read(4), frameMagic, 4)) { setError(error, QStringLiteral("History record header is corrupt.")); return false; }
    const auto rawLength = m_file.read(8);
    if (rawLength.size() != 8) { setError(error, QStringLiteral("History record length is incomplete.")); return false; }
    const auto length = qFromLittleEndian<quint64>(rawLength.constData());
    const auto digest = m_file.read(32);
    if (digest.size() != 32 || length > static_cast<quint64>(std::numeric_limits<qsizetype>::max()) || length > static_cast<quint64>(fileSize - offset - frameHeader - frameFooter)) {
        setError(error, QStringLiteral("History record is incomplete or too large.")); return false;
    }
    try {
        auto data = m_file.read(static_cast<qint64>(length));
        if (data.size() != static_cast<qsizetype>(length) || !same(m_file.read(4), doneMagic, 4)) { setError(error, QStringLiteral("History record is incomplete.")); return false; }
        if (QCryptographicHash::hash(data, QCryptographicHash::Sha256) != digest) { setError(error, QStringLiteral("History record checksum does not match.")); return false; }
        *payload = std::move(data); return true;
    } catch (const std::bad_alloc&) { setError(error, QStringLiteral("Not enough memory to read history.")); return false; }
}
bool HistoryJournal::verify(std::vector<qint64>* offsets, QString* error)
{
    if (!m_file.isOpen() || !m_file.seek(0) || !same(m_file.read(8), fileMagic, 8)
        || m_file.read(16) != m_documentId.toRfc4122()) {
        setError(error, QStringLiteral("History journal header or document identity is invalid.")); return false;
    }
    try {
        std::vector<qint64> found;
        for (qint64 pos = fileHeader; pos < m_file.size();) {
            QByteArray payload;
            if (!read(pos, &payload, error)) return false;
            if (offsets) found.push_back(pos);
            pos += frameHeader + payload.size() + frameFooter;
        }
        if (offsets) *offsets = std::move(found);
        return true;
    } catch (const std::bad_alloc&) { setError(error, QStringLiteral("Not enough memory to index history.")); return false; }
      catch (const std::length_error&) { setError(error, QStringLiteral("History index is too large.")); return false; }
}
}
