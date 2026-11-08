#include "RecentFiles.h"
#include "../core/Settings.h"

#include <QFileInfo>

namespace komapaint {

static constexpr const char* kKey = "files/recentDocuments";

RecentFiles::RecentFiles(Settings* settings, QObject* parent)
    : QObject(parent), m_settings(settings) { load(); }

void RecentFiles::load()
{
    const auto v = m_settings->value(kKey).toStringList();
    m_files = v.mid(0, m_limit);
}

void RecentFiles::save()
{
    m_settings->setValue(kKey, m_files);
}

void RecentFiles::add(const QString& path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    m_files.removeAll(abs);
    m_files.prepend(abs);
    while (m_files.size() > m_limit) m_files.removeLast();
    save();
    emit filesChanged();
}

void RecentFiles::remove(const QString& path)
{
    if (m_files.removeAll(path) > 0) {
        save();
        emit filesChanged();
    }
}

void RecentFiles::clear()
{
    if (m_files.isEmpty()) return;
    m_files.clear();
    save();
    emit filesChanged();
}

void RecentFiles::setLimit(int limit)
{
    limit = qBound(1, limit, 50);
    if (limit == m_limit) return;
    m_limit = limit;
    while (m_files.size() > m_limit) m_files.removeLast();
    save();
    emit limitChanged();
    emit filesChanged();
}

}  // namespace komapaint
