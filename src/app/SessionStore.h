#pragma once

#include <QPointF>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace komapaint {

// Session persistence: which saved projects were open, which one was active,
// each project's own tool/zoom/viewport position, and the window layout.
//
// This is deliberately a separate mechanism from the three others:
//   * document serialization (.koma, src/files/DocumentFile)   - the artwork
//   * recovery snapshots (recovery/workspace.json + *.koma)    - crash safety
//   * undo history (src/history/UndoStack)                     - RAM only
// The session file never contains artwork, never refers to recovery
// snapshots, and losing it loses only convenience (the tab list and layout),
// never a drawing.
struct SessionDocument
{
    QString id;          // tab identity (UUID without braces)
    QString path;        // absolute path of the saved project
    QString title;       // only used for error messages when reopening fails
    QString tool;        // this project's active tool
    qreal zoom = 100.0;  // this project's zoom, in percent
    bool hasView = false;  // false: fit the canvas when it is first shown
    QPointF center;        // document point shown at the viewport centre
};

struct SessionData
{
    QString activeId;
    std::vector<SessionDocument> documents;
    // Window layout: panel visibility and dock sizes, keyed by plain names.
    QVariantMap ui;
};

class SessionStore
{
public:
    static constexpr int FormatVersion = 1;

    enum class LoadResult { Loaded, Missing, Invalid };

    // Reads and validates a session file (JSON, self-checksummed). Invalid or
    // truncated files are reported as Invalid and left on disk untouched.
    static LoadResult load(const QString& path, SessionData* data, QString* error = nullptr);

    // Atomically replaces the session file (temporary file, flush, rename).
    // On failure the previous session file is left as it was.
    static bool save(const QString& path, const SessionData& data, QString* error = nullptr);
};

}  // namespace komapaint
