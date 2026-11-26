// Library ownership and resolution, without a window or an artwork document.
#include "app/BrushSettings.h"
#include "app/SubToolLibrary.h"
#include "brush/BrushFormat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

using namespace komapaint;

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (false)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString manifest = dir.filePath(QStringLiteral("brush-library.json"));
    const QString source = dir.filePath(QStringLiteral("artist's brush.kb"));
    const auto url = QUrl::fromLocalFile(source);
    const auto path = QFileInfo(source).filesystemFilePath();
    auto doc = brush::BrushDocument::fromLegacy({}, "studio-original", "Native test");
    const QString tool = QStringLiteral("brush");
    CHECK(brush::saveBrushFile(path, doc));
    QString id;
    QString alias;
    QString stock;
    {
        BrushSettings settings;
        SubToolLibrary library(&settings);
        CHECK(library.initializePersistence(manifest));
        library.setTool(tool);
        stock = library.currentBrushId();
        CHECK(!stock.isEmpty());
        id = library.registerNativeBrush(url);
        CHECK(!id.isEmpty());
        CHECK(library.currentBrushId() == id && settings.hasNativeBrush());
        CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
        auto first = settings.nativeBrushDocument();
        settings.setSize(48);
        CHECK(settings.size() == 16); // legacy quick size cannot overwrite a native definition
        alias = library.duplicateBrushById(id);
        CHECK(!alias.isEmpty() && alias != id);
        CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
        CHECK(doc.setParameter("size", 27.25));
        CHECK(brush::saveBrushFile(path, doc));
        CHECK(library.registerNativeBrush(url) == id); // update original and select it
        CHECK(library.currentBrushId() == id && *settings.nativeBrushDocument() == doc);
        library.selectById(alias);
        CHECK(library.currentBrushId() == alias && *settings.nativeBrushDocument() == *first);
        library.selectById(id);
        CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
        CHECK(first->parameters.value("size") == 16); // in-flight snapshots remain immutable
        library.setTool(QStringLiteral("hand"));
        CHECK(!settings.hasNativeBrush());
        library.setTool(tool);
        CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
        settings.setBrushEngine(QStringLiteral("realistic"));
        CHECK(!settings.hasNativeBrush());
        settings.setBrushEngine(QStringLiteral("digital"));
        CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
    }
    CHECK(QFile::remove(source)); // the application owns its exact snapshot
    BrushSettings settings;
    SubToolLibrary restarted(&settings);
    CHECK(restarted.initializePersistence(manifest));
    restarted.setTool(tool);
    restarted.selectById(id);
    CHECK(restarted.currentBrushId() == id);
    CHECK(settings.nativeBrushIssue().isEmpty());
    CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
    bool seen = false;
    for (const auto& value : restarted.subTools()) {
        auto row = value.toMap();
        if (row.value(QStringLiteral("id")) == id) seen = row.value(QStringLiteral("nativeBrush")).toBool();
    }
    CHECK(seen);

    BrushLibraryDocument index;
    CHECK(BrushLibraryStore::load(manifest, {}, &index));
    QString installed;
    for (const auto& tab : index.digital.tabs)
        for (const auto& entry : tab.brushes)
            if (entry.id == id) installed = BrushLibraryStore::nativeFilePath(manifest, entry.nativeAssetKey);
    CHECK(!installed.isEmpty() && QFile::exists(installed));
    CHECK(BrushLibraryStore::nativeFilePath(manifest, QStringLiteral("../other.kb")).isEmpty());
    CHECK(QFile::rename(installed, source));
    restarted.selectById(stock);
    CHECK(!settings.hasNativeBrush());
    restarted.selectById(id);
    CHECK(settings.hasNativeBrush() && !settings.nativeBrushDocument() && !settings.nativeBrushIssue().isEmpty());
    CHECK(QFile::copy(source, installed));
    restarted.selectById(id); // retry the failed selection after restoration
    CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
    const auto installedPath = QFileInfo(installed).filesystemFilePath();
    CHECK(brush::saveBrushFile(installedPath, *settings.nativeBrushDocument()));
    auto changed = doc;
    CHECK(changed.setParameter("size", 33));
    CHECK(brush::saveBrushFile(installedPath, changed)); // a valid .kb, but *not* this snapshot
    restarted.selectById(stock);
    restarted.selectById(id);
    CHECK(settings.nativeBrushIssue().contains(QStringLiteral("checksum")));
    CHECK(!settings.nativeBrushDocument());
    restarted.selectById(alias);
    CHECK(settings.nativeBrushDocument() && settings.size() == 16);

    CHECK(changed.setParameter("stroke-delay", 5));
    CHECK(brush::saveBrushFile(path, changed));
    CHECK(restarted.registerNativeBrush(url).isEmpty());
    CHECK(restarted.lastError().contains(QStringLiteral("Stroke delay")));
    CHECK(restarted.currentBrushId() == alias);
    QFile damaged(source);
    CHECK(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(damaged.write("broken") == 6);
    damaged.close();
    CHECK(restarted.registerNativeBrush(url).isEmpty() && !restarted.lastError().isEmpty());
    CHECK(restarted.currentBrushId() == alias);
    std::puts("Native library registration, restart, immutable aliases, legacy selection and failure paths passed.");
    return 0;
}
