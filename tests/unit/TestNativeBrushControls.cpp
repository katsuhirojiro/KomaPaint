// Native Sub-Tool adjustments and install outcomes. The .kb asset is immutable;
// per-install controls live in the library manifest.
#include "app/BrushSettings.h"
#include "app/SubToolLibrary.h"
#include "brush/BrushFormat.h"
#include "drawing/DigitalBrushCapabilities.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

using namespace komapaint;

static int g_checks = 0;

#define CHECK(test) do { \
    ++g_checks; \
    if (!(test)) { \
        std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); \
        return 1; \
    } \
} while (false)

namespace {

QByteArray bytesOf(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool hasControl(const QVariantList& rows, const QString& key)
{
    for (const auto& row : rows)
        if (row.toMap().value(QStringLiteral("key")).toString() == key) return true;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString manifest = dir.filePath(QStringLiteral("brush-library.json"));
    const QString source = dir.filePath(QStringLiteral("Marker.kb"));
    auto doc = brush::BrushDocument::fromLegacy({}, "native-controls-test", "Marker");
    CHECK(doc.setParameter("aspect", 220));
    CHECK(doc.setParameter("tip-angle", 40));
    CHECK(brush::saveBrushFile(QFileInfo(source).filesystemFilePath(), doc));
    const QByteArray assetBefore = bytesOf(source);
    CHECK(!assetBefore.isEmpty());

    BrushSettings settings;
    SubToolLibrary library(&settings);
    CHECK(library.initializePersistence(manifest));
    library.setTool(QStringLiteral("brush"));
    const QString markers = library.createTab(QStringLiteral("Markers"));
    CHECK(!markers.isEmpty());
    CHECK(library.currentTabId() == markers);

    const auto result = library.installNativeBrushInTab(QUrl::fromLocalFile(source), markers);
    CHECK(result.value(QStringLiteral("installed")).toBool());
    CHECK(result.value(QStringLiteral("supported")).toBool());
    CHECK(result.value(QStringLiteral("tabId")).toString() == markers);
    CHECK(result.value(QStringLiteral("tabName")).toString() == QStringLiteral("Markers"));
    const QString id = result.value(QStringLiteral("id")).toString();
    CHECK(!id.isEmpty());
    CHECK(library.currentTabId() == markers && library.currentBrushId() == id);
    CHECK(settings.nativeBrushDocument() && *settings.nativeBrushDocument() == doc);
    CHECK(library.nativeControls().size() >= 8);
    CHECK(hasControl(library.nativeControls(), QStringLiteral("aspect")));
    CHECK(hasControl(library.nativeControls(), QStringLiteral("tip-angle")));
    CHECK(hasControl(library.nativeControls(), QStringLiteral("follow-direction")));

    CHECK(library.setNativeControl(QStringLiteral("tip-angle"), 55));
    CHECK(settings.nativeBrushDocument());
    CHECK(settings.nativeBrushDocument()->parameters.value("tip-angle") == 55);
    CHECK(library.currentNativeModified());
    CHECK(bytesOf(source) == assetBefore); // the user's .kb is untouched
    // The private library snapshot is untouched as well.
    CHECK(settings.nativeBrushDocument()->parameters.value("aspect") == 220);

    {
        SubToolLibrary restarted(&settings);
        CHECK(restarted.initializePersistence(manifest));
        restarted.setTool(QStringLiteral("brush"));
        restarted.selectTabById(markers);
        restarted.selectById(id);
        CHECK(settings.nativeBrushDocument());
        CHECK(settings.nativeBrushDocument()->parameters.value("tip-angle") == 55);
        CHECK(restarted.currentNativeModified());
        CHECK(restarted.resetCurrentNativeOverrides());
        CHECK(!restarted.currentNativeModified());
        CHECK(settings.nativeBrushDocument());
        CHECK(settings.nativeBrushDocument()->parameters.value("tip-angle") == 40);
        CHECK(bytesOf(source) == assetBefore);
    }

    // A valid document in a library that cannot be written is a library
    // failure, not a renderer-support failure.
    SubToolLibrary noLibrary(&settings);
    const auto unavailable = noLibrary.installNativeBrush(QUrl::fromLocalFile(source));
    CHECK(unavailable.value(QStringLiteral("supported")).toBool());
    CHECK(!unavailable.value(QStringLiteral("installed")).toBool());
    CHECK(!unavailable.value(QStringLiteral("error")).toString().isEmpty());

    // Unsupported stored data is honestly refused.
    auto unsupported = doc;
    CHECK(unsupported.setParameter("stroke-delay", 42));
    const QString unsupportedFile = dir.filePath(QStringLiteral("Unsupported.kb"));
    CHECK(brush::saveBrushFile(QFileInfo(unsupportedFile).filesystemFilePath(), unsupported));
    const auto rejected = library.installNativeBrush(QUrl::fromLocalFile(unsupportedFile));
    CHECK(!rejected.value(QStringLiteral("supported")).toBool());
    CHECK(!rejected.value(QStringLiteral("installed")).toBool());
    CHECK(rejected.value(QStringLiteral("error")).toString().contains(QStringLiteral("Stroke delay")));

    std::printf("%d checks, 0 failures\n", g_checks);
    std::puts("Native install outcomes, tab selection, overrides, reset and asset immutability passed.");
    return 0;
}
