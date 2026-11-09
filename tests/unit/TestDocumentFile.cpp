#include <QtTest/QtTest>
#include "files/DocumentFile.h"
#include "app/WorkspaceState.h"
#include "canvas/CanvasDocument.h"
#include "layers/LayerModel.h"
#include "layers/Layer.h"
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QUrl>
#include <cstring>
#include <memory>

using namespace komapaint;

// Builds candidate files with full control over every byte, then fixes the
// trailing CRC-32 so structural (not checksum) validation is what fails.
struct FileBuilder {
    QByteArray bytes;
    void raw(const char* data, qsizetype size) { bytes.append(data, size); }
    void u8(quint8 v) { bytes.append(static_cast<char>(v)); }
    void u16(quint16 v) { for (int i = 0; i < 2; ++i) bytes.append(static_cast<char>((v >> (8 * i)) & 0xff)); }
    void u32(quint32 v) { for (int i = 0; i < 4; ++i) bytes.append(static_cast<char>((v >> (8 * i)) & 0xff)); }
    void u64(quint64 v) { for (int i = 0; i < 8; ++i) bytes.append(static_cast<char>((v >> (8 * i)) & 0xff)); }
    void dbl(double v) { quint64 bits; std::memcpy(&bits, &v, sizeof(bits)); u64(bits); }
    void utf8(const QString& s) { const auto b = s.toUtf8(); u32(static_cast<quint32>(b.size())); raw(b.constData(), b.size()); }
    void utf8Raw(const QByteArray& b, quint32 declaredLength) { u32(declaredLength); raw(b.constData(), b.size()); }
    QByteArray finish() const {
        QByteArray out = bytes;
        const quint32 crc = DocumentFile::crc32(bytes);
        for (int i = 0; i < 4; ++i) out.append(static_cast<char>((crc >> (8 * i)) & 0xff));
        return out;
    }
};

// The illustration document body (shared by the v1 flat format and the v2
// project envelope).
static QByteArray illustrationPayload()
{
    FileBuilder b;
    b.u32(4); b.u32(4);          // 4 x 4 canvas
    b.dbl(300.0);                // dpi
    b.u8(1);                     // transparent background
    b.u32(0xffffffffU);          // background colour (opaque, required)
    b.utf8(QStringLiteral(""));  // title
    b.u32(0);                    // active layer
    b.u32(1);                    // one layer
    b.utf8(QStringLiteral("Layer 1"));
    b.u8(1); b.u8(0);            // visible, not locked
    b.dbl(1.0);                  // opacity
    b.u32(0);                    // no tile rectangles
    return b.bytes;
}

// Wraps a payload in the version 2 project envelope (CRC-32 trailer included).
static QByteArray toVersion2Envelope(const QByteArray& payload)
{
    FileBuilder b;
    b.raw("KOMAPAINT", 9);
    b.u16(2);                    // envelope version
    b.u32(1);                    // one document
    b.u8(1);                     // kind: illustration
    b.u64(static_cast<quint64>(payload.size()));
    b.raw(payload.constData(), payload.size());
    b.u32(0);                    // asset count (shared assets unused)
    return b.finish();
}

static QByteArray minimalValidV1()
{
    FileBuilder b;
    b.raw("KOMAPAINT", 9);
    b.u16(1);                    // legacy version 1 flat layout
    const QByteArray payload = illustrationPayload();
    b.raw(payload.constData(), payload.size());
    return b.finish();
}

static QByteArray minimalValidV2()
{
    return toVersion2Envelope(illustrationPayload());
}

static bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(bytes) == bytes.size();
}

class TestDocumentFile : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsPixelsAndMetadata();
    void emptyDocumentRoundTrip();
    void tileBoundariesAndPartialTiles();
    void corruptedFilesAreRejected();
    void structurallyInvalidFilesAreRejected();
    void failedSaveLeavesPreviousFileUntouched();
    void workspaceSaveOpenFlow();
    void saveWritesVersion2Envelope();
    void legacyVersion1FilesOpen();
    void legacyVersion1ResaveWritesVersion2();
    void legacyVersion1CorruptionRejected();
};

void TestDocumentFile::roundTripsPixelsAndMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.koma"));
    {
        CanvasDocument doc;
        doc.setSize(QSize(300, 200));
        doc.setTransparentBackground(false);
        doc.setBackgroundColor(Qt::darkGreen);
        doc.setDpi(150.5);
        doc.setTitle(QStringLiteral("My piece"));
        auto* base = doc.layers()->layerAt(0);
        base->setName(QStringLiteral("Base"));
        base->setLocked(true);
        base->tiles().setPixel({280, 190}, 0x80402010U); // partial alpha
        base->tiles().setPixel({0, 0}, 0xff010203U);
        doc.layers()->addLayer();
        auto* ink = doc.layers()->layerAt(doc.layers()->rowCount() - 1);
        ink->setName(QStringLiteral("Ink"));
        ink->setVisible(false);
        ink->setOpacity(0.5);
        ink->tiles().setPixel({10, 10}, 0x80808080U); // premultiplied: channels must not exceed alpha
        doc.layers()->setActiveLayerIndex(doc.layers()->rowCount() - 1);
        QString error;
        QVERIFY2(DocumentFile::save(doc, path, &error), qPrintable(error));
    }
    QString error;
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    QCOMPARE(loaded->width(), 300);
    QCOMPARE(loaded->height(), 200);
    QCOMPARE(loaded->dpi(), 150.5);
    QVERIFY(!loaded->hasTransparentBackground());
    QCOMPARE(loaded->backgroundColor(), QColor(Qt::darkGreen));
    QCOMPARE(loaded->title(), QStringLiteral("My piece"));
    QVERIFY(!loaded->isModified());
    QCOMPARE(loaded->layers()->rowCount(), 2);
    auto* base = loaded->layers()->layerAt(0);
    auto* ink = loaded->layers()->layerAt(1);
    QCOMPARE(base->name(), QStringLiteral("Base"));
    QVERIFY(base->isLocked());
    QVERIFY(base->isVisible());
    QCOMPARE(base->opacity(), 1.0);
    QCOMPARE(ink->name(), QStringLiteral("Ink"));
    QVERIFY(!ink->isVisible());
    QCOMPARE(ink->opacity(), 0.5);
    QCOMPARE(loaded->layers()->activeLayerIndex(), 1);
    QCOMPARE(base->tiles().pixel({280, 190}).value(), 0x80402010U);
    QCOMPARE(base->tiles().pixel({0, 0}).value(), 0xff010203U);
    QCOMPARE(ink->tiles().pixel({10, 10}).value(), 0x80808080U);
    QCOMPARE(ink->tiles().pixel({280, 190}).value(), 0U); // layers stay isolated
    QCOMPARE(base->tiles().pixel({100, 100}).value(), 0U);
}

void TestDocumentFile::emptyDocumentRoundTrip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("empty.koma"));
    {
        CanvasDocument doc; // default 1600x1200, transparent, one empty layer
        QString error;
        QVERIFY2(DocumentFile::save(doc, path, &error), qPrintable(error));
    }
    QString error;
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    QCOMPARE(loaded->layers()->rowCount(), 1);
    QCOMPARE(loaded->layers()->layerAt(0)->tiles().allocatedTiles(), size_t(0));
    QVERIFY(!loaded->isModified());
    QCOMPARE(loaded->title(), QStringLiteral("Untitled"));
    QVERIFY(loaded->hasTransparentBackground());
}

void TestDocumentFile::tileBoundariesAndPartialTiles()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("edges.koma"));
    const std::vector<std::pair<int, int>> points = {
        {0, 0}, {255, 0}, {256, 0}, {511, 399}, {599, 399}, {300, 100}, {88, 255}, {87, 255}, {0, 399},
    };
    {
        CanvasDocument doc;
        doc.setSize(QSize(600, 400)); // exercises partial edge tiles in both axes
        auto* layer = doc.layers()->layerAt(0);
        for (std::size_t i = 0; i < points.size(); ++i)
            QCOMPARE(layer->tiles().setPixel({points[i].first, points[i].second},
                                             static_cast<quint32>(0xff000000U) | (quint32(i + 1) * 2917U)),
                     SparseTileStore::WriteResult::Changed);
        QString error;
        QVERIFY2(DocumentFile::save(doc, path, &error), qPrintable(error));
    }
    QString error;
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    auto* layer = loaded->layers()->layerAt(0);
    for (std::size_t i = 0; i < points.size(); ++i)
        QCOMPARE(layer->tiles().pixel({points[i].first, points[i].second}).value(),
                 static_cast<quint32>(0xff000000U) | (quint32(i + 1) * 2917U));
    QCOMPARE(layer->tiles().pixel({1, 1}).value(), 0U);
    QCOMPARE(layer->tiles().allocatedTiles(), size_t(5)); // only tiles with painted pixels exist
}

void TestDocumentFile::corruptedFilesAreRejected()
{
    QTemporaryDir dir;
    const QString goodPath = dir.filePath(QStringLiteral("good.koma"));
    {
        CanvasDocument doc;
        doc.setSize(QSize(64, 64));
        doc.layers()->layerAt(0)->tiles().setPixel({3, 4}, 0x80112233U);
        QString error;
        QVERIFY2(DocumentFile::save(doc, goodPath, &error), qPrintable(error));
    }
    QFile f(goodPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray good = f.readAll();
    QVERIFY(good.size() > 64);

    const auto tryBytes = [&](const QString& name, const QByteArray& bytes) {
        const QString path = dir.filePath(name);
        QVERIFY(writeFile(path, bytes));
        QString error;
        std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
        QVERIFY2(loaded == nullptr, qPrintable(name));
        QVERIFY2(!error.isEmpty(), qPrintable(name));
    };

    QByteArray midFlip = good;
    midFlip[good.size() / 2] = static_cast<char>(midFlip[good.size() / 2] ^ 0x40);
    tryBytes(QStringLiteral("flip-middle.koma"), midFlip);

    QByteArray crcFlip = good;
    crcFlip[good.size() - 1] = static_cast<char>(crcFlip[good.size() - 1] ^ 0x01);
    tryBytes(QStringLiteral("flip-crc.koma"), crcFlip);

    tryBytes(QStringLiteral("truncated.koma"), good.left(good.size() - 10));
    tryBytes(QStringLiteral("tiny.koma"), good.left(10));

    QByteArray garbage(200, '\0');
    for (int i = 0; i < garbage.size(); ++i) garbage[i] = static_cast<char>((i * 2654435761U) & 0xff);
    tryBytes(QStringLiteral("garbage.koma"), garbage);
}

void TestDocumentFile::structurallyInvalidFilesAreRejected()
{
    QTemporaryDir dir;
    const auto expectRejected = [&dir](const QString& name, const QByteArray& bytes) {
        const QString path = dir.filePath(name);
        QVERIFY(writeFile(path, bytes));
        QString error;
        std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
        QVERIFY2(loaded == nullptr, qPrintable(name));
    };

    expectRejected(QStringLiteral("unknown-version.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(3); // version this build does not know
        b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0); b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-zero-documents.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(0); // zero documents declared
        b.u32(0); // asset count
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-multi-document.koma"), [] {
        const QByteArray payload = illustrationPayload();
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(2); // multi-page projects do not exist yet
        b.u8(1);  // kind: illustration
        b.u64(static_cast<quint64>(payload.size()));
        b.raw(payload.constData(), payload.size());
        b.u8(1);  // kind: illustration
        b.u64(static_cast<quint64>(payload.size()));
        b.raw(payload.constData(), payload.size());
        b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-comic-page-kind.koma"), [] {
        const QByteArray payload = illustrationPayload();
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(1); // one document
        b.u8(2);  // reserved: comic page (not implemented)
        b.u64(static_cast<quint64>(payload.size()));
        b.raw(payload.constData(), payload.size());
        b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-length-too-large.koma"), [] {
        const QByteArray payload = illustrationPayload();
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(1);
        b.u8(1);
        b.u64(static_cast<quint64>(payload.size()) + 1); // one byte too many
        b.raw(payload.constData(), payload.size());
        b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-length-too-small.koma"), [] {
        const QByteArray payload = illustrationPayload();
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(1);
        b.u8(1);
        b.u64(static_cast<quint64>(payload.size()) - 8); // swallows part of the payload
        b.raw(payload.constData(), payload.size());
        b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-shared-assets.koma"), [] {
        const QByteArray payload = illustrationPayload();
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(1);
        b.u8(1);
        b.u64(static_cast<quint64>(payload.size()));
        b.raw(payload.constData(), payload.size());
        b.u32(1); // shared asset section: reserved, not supported yet
        return b.finish();
    }());
    expectRejected(QStringLiteral("zero-size.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(0); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0); b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("no-layers.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(0); // zero layers
        return b.finish();
    }());
    expectRejected(QStringLiteral("bad-active.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(1); b.u32(1); // active layer 1 of 1
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0); b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("bad-opacity.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.5); b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("rect-out-of-bounds.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0);
        b.u32(1);          // one rect
        b.u64(3); b.u64(0); b.u32(2); b.u32(2); // 3..5 exceeds width 4
        for (int i = 0; i < 8; ++i) b.u32(0xff102030U);
        return b.finish();
    }());
    expectRejected(QStringLiteral("not-premultiplied.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0);
        b.u32(1);
        b.u64(0); b.u64(0); b.u32(1); b.u32(1);
        b.u32(0x10102020U); // red channel larger than alpha
        return b.finish();
    }());
    expectRejected(QStringLiteral("invalid-utf8.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8Raw(QByteArray::fromHex("fffefd"), 3); // not valid UTF-8
        b.u8(1); b.u8(0); b.dbl(1.0); b.u32(0);
        return b.finish();
    }());
    expectRejected(QStringLiteral("trailing-garbage.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0); b.u32(0);
        b.u32(0xdeadbeefU); // one extra word before the checksum
        return b.finish();
    }());
    expectRejected(QStringLiteral("empty-rect.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(1); b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0);
        b.u32(1);
        b.u64(0); b.u64(0); b.u32(1); b.u32(1);
        b.u32(0x00000000U); // all-zero rect (never produced by the writer)
        return b.finish();
    }());
    expectRejected(QStringLiteral("v2-trailing-after-assets.koma"), [] {
        const QByteArray payload = illustrationPayload();
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2);
        b.u32(1);
        b.u8(1);
        b.u64(static_cast<quint64>(payload.size()));
        b.raw(payload.constData(), payload.size());
        b.u32(0);
        b.u32(0xdeadbeefU); // bytes after the asset count
        return b.finish();
    }());
    // Sanity: the untouched minimal files really are accepted, in both versions.
    const QString okPathV1 = dir.filePath(QStringLiteral("minimal-v1.koma"));
    QVERIFY(writeFile(okPathV1, minimalValidV1()));
    QString error;
    std::unique_ptr<CanvasDocument> loadedV1(DocumentFile::open(okPathV1, &error));
    QVERIFY2(loadedV1 != nullptr, qPrintable(error));
    const QString okPathV2 = dir.filePath(QStringLiteral("minimal-v2.koma"));
    QVERIFY(writeFile(okPathV2, minimalValidV2()));
    std::unique_ptr<CanvasDocument> loadedV2(DocumentFile::open(okPathV2, &error));
    QVERIFY2(loadedV2 != nullptr, qPrintable(error));
}

void TestDocumentFile::failedSaveLeavesPreviousFileUntouched()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("keep.koma"));
    CanvasDocument doc;
    doc.setSize(QSize(32, 32));
    doc.layers()->layerAt(0)->tiles().setPixel({1, 1}, 0xff112233U);
    QString error;
    QVERIFY2(DocumentFile::save(doc, path, &error), qPrintable(error));
    QFile::remove(dir.filePath(QStringLiteral("keep.tmp"))); // hygiene, not used by the format
    // A save to a location that cannot be written must fail and report an error.
    QVERIFY(!DocumentFile::save(doc, QString(), nullptr));
    QVERIFY(!DocumentFile::save(doc, dir.filePath(QStringLiteral("no-such-dir/keep.koma")), &error));
    QVERIFY2(!error.isEmpty(), "save failure must carry a message");
    // The previously saved file is still fully readable and unchanged.
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    QCOMPARE(loaded->layers()->layerAt(0)->tiles().pixel({1, 1}).value(), 0xff112233U);
    // And saving again over the good file works (atomic replace path).
    doc.layers()->layerAt(0)->tiles().setPixel({2, 2}, 0xff445566U);
    QVERIFY2(DocumentFile::save(doc, path, &error), qPrintable(error));
    loaded.reset(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    QCOMPARE(loaded->layers()->layerAt(0)->tiles().pixel({2, 2}).value(), 0xff445566U);
}

void TestDocumentFile::workspaceSaveOpenFlow()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ws.koma"));
    QStringList errors;
    {
        WorkspaceState workspace;
        connect(&workspace, &WorkspaceState::fileErrorOccurred, &workspace, [&errors](const QString& m) { errors << m; });
        QVERIFY(workspace.newDocument(120, 80, true, Qt::white));
        auto* layer = workspace.layers()->layerAt(0);
        // Paint through the history path, exactly like a real brush stroke.
        QVERIFY(workspace.history()->beginStroke(layer));
        QCOMPARE(layer->tiles().setPixel({5, 5}, 0xffff0000U), SparseTileStore::WriteResult::Changed);
        QVERIFY(workspace.history()->commitStroke());
        QVERIFY(workspace.document()->isModified());
        errors.clear();
        // Saving with no current file and no argument must fail with a message.
        QVERIFY(!workspace.saveToPath(QString()));
        QCOMPARE(errors.size(), 1);
        errors.clear();
        QVERIFY(workspace.saveToPath(path));
        QVERIFY(errors.isEmpty());
        QVERIFY(!workspace.document()->isModified());
        QCOMPARE(workspace.currentFile(), path);
        QVERIFY(workspace.history()->canUndo()); // painting is still undoable after a save
    }
    {
        WorkspaceState reopened;
        connect(&reopened, &WorkspaceState::fileErrorOccurred, &reopened, [&errors](const QString& m) { errors << m; });
        errors.clear();
        QSignalSpy docChanged(&reopened, &WorkspaceState::documentChanged);
        QVERIFY(reopened.openFromPath(path));
        QCOMPARE(docChanged.count(), 1);
        QVERIFY(!reopened.document()->isModified());
        QVERIFY(!reopened.history()->canUndo()); // history never travels with the file
        QCOMPARE(reopened.document()->layers()->layerAt(0)->tiles().pixel({5, 5}).value(), 0xffff0000U);
        QCOMPARE(reopened.currentFile(), path);
        // Saving with an empty argument reuses the current file.
        QVERIFY(reopened.saveToPath(QString()));
        // A failed open must leave the live document completely untouched.
        const QString before = reopened.currentFile();
        auto* beforeDoc = reopened.document();
        errors.clear();
        QVERIFY(!reopened.openFromPath(dir.filePath(QStringLiteral("does-not-exist.koma"))));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(reopened.document(), beforeDoc);
        QCOMPARE(reopened.currentFile(), before);
        QCOMPARE(reopened.document()->layers()->layerAt(0)->tiles().pixel({5, 5}).value(), 0xffff0000U);
        // File-URL form works for both open and save.
        const QString url = QUrl::fromLocalFile(path).toString();
        QVERIFY(reopened.openFromPath(url));
        const QString path2 = dir.filePath(QStringLiteral("ws-url.koma"));
        QVERIFY(reopened.saveToPath(QUrl::fromLocalFile(path2).toString()));
        QVERIFY(QFile::exists(path2));
    }
}

static QByteArray legacyFileWithPixels()
{
    FileBuilder p;
    p.u32(8); p.u32(8);
    p.dbl(144.0);
    p.u8(0);              // opaque background
    p.u32(0xffff8800U);   // background colour, alpha 0xFF
    p.utf8(QStringLiteral("Legacy piece"));
    p.u32(0);             // active layer
    p.u32(1);             // one layer
    p.utf8(QStringLiteral("Ink"));
    p.u8(1); p.u8(1);     // visible, locked
    p.dbl(0.75);
    p.u32(1);             // one tile rectangle
    p.u64(2); p.u64(3);   // left, top
    p.u32(2); p.u32(1);   // 2 x 1 pixels
    p.u32(0x80112233U);   // premultiplied: channels <= alpha
    p.u32(0x80445566U);
    FileBuilder b;
    b.raw("KOMAPAINT", 9);
    b.u16(1);             // version 1 flat layout
    b.raw(p.bytes.constData(), p.bytes.size());
    return b.finish();
}

void TestDocumentFile::saveWritesVersion2Envelope()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("written.koma"));
    {
        CanvasDocument doc;
        doc.setSize(QSize(16, 16));
        QString error;
        QVERIFY2(DocumentFile::save(doc, path, &error), qPrintable(error));
    }
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QVERIFY(bytes.size() >= 9 + 2 + 1 + 8 + 4 + 4);
    QCOMPARE(memcmp(bytes.constData(), "KOMAPAINT", 9), 0);
    const quint16 version = static_cast<quint16>(static_cast<quint8>(bytes[9]))
        | static_cast<quint16>(static_cast<quint8>(bytes[10]) << 8);
    QCOMPARE(version, quint16(2));      // this build always writes the v2 envelope
    const quint32 documentCount = static_cast<quint32>(static_cast<quint8>(bytes[11]))
        | (static_cast<quint32>(static_cast<quint8>(bytes[12])) << 8)
        | (static_cast<quint32>(static_cast<quint8>(bytes[13])) << 16)
        | (static_cast<quint32>(static_cast<quint8>(bytes[14])) << 24);
    QCOMPARE(documentCount, quint32(1)); // one illustration document
    QCOMPARE(static_cast<quint8>(bytes[15]), quint8(1)); // kind: illustration
    // Document length must cover exactly the payload bytes before assetCount+CRC.
    quint64 payloadLength = 0;
    for (int i = 0; i < 8; ++i)
        payloadLength |= static_cast<quint64>(static_cast<quint8>(bytes[16 + i])) << (8 * i);
    QCOMPARE(payloadLength, static_cast<quint64>(bytes.size()) - 24 - 4 - 4); // header (incl. length field) + assetCount + CRC
    // And it must still open.
    QString error;
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
}

void TestDocumentFile::legacyVersion1FilesOpen()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("legacy.koma"));
    QVERIFY(writeFile(path, legacyFileWithPixels()));
    QString error;
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
    QCOMPARE(loaded->width(), 8);
    QCOMPARE(loaded->height(), 8);
    QCOMPARE(loaded->dpi(), 144.0);
    QVERIFY(!loaded->hasTransparentBackground());
    QCOMPARE(loaded->backgroundColor(), QColor(0xff, 0x88, 0x00));
    QCOMPARE(loaded->title(), QStringLiteral("Legacy piece"));
    QCOMPARE(loaded->layers()->rowCount(), 1);
    auto* layer = loaded->layers()->layerAt(0);
    QCOMPARE(layer->name(), QStringLiteral("Ink"));
    QVERIFY(layer->isLocked());
    QVERIFY(layer->isVisible());
    QCOMPARE(layer->opacity(), 0.75);
    QCOMPARE(layer->tiles().pixel({2, 3}).value(), 0x80112233U);
    QCOMPARE(layer->tiles().pixel({3, 3}).value(), 0x80445566U);
    QCOMPARE(layer->tiles().pixel({0, 0}).value(), 0U);
}

void TestDocumentFile::legacyVersion1ResaveWritesVersion2()
{
    QTemporaryDir dir;
    const QString path1 = dir.filePath(QStringLiteral("legacy.koma"));
    QVERIFY(writeFile(path1, legacyFileWithPixels()));
    QString error;
    QString output;
    {
        std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(path1, &error));
        QVERIFY2(loaded != nullptr, qPrintable(error));
        output = dir.filePath(QStringLiteral("resaved.koma"));
        QVERIFY2(DocumentFile::save(*loaded, output, &error), qPrintable(error));
    }
    // Re-saving a legacy file migrates it to the v2 envelope; it is never
    // renamed, invalidated or left in a half-written state.
    QFile f(output);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    const quint16 version = static_cast<quint16>(static_cast<quint8>(bytes[9]))
        | static_cast<quint16>(static_cast<quint8>(bytes[10]) << 8);
    QCOMPARE(version, quint16(2));
    std::unique_ptr<CanvasDocument> reloaded(DocumentFile::open(output, &error));
    QVERIFY2(reloaded != nullptr, qPrintable(error));
    QCOMPARE(reloaded->dpi(), 144.0);
    QCOMPARE(reloaded->title(), QStringLiteral("Legacy piece"));
    QCOMPARE(reloaded->backgroundColor(), QColor(0xff, 0x88, 0x00));
    auto* layer = reloaded->layers()->layerAt(0);
    QVERIFY(layer->isLocked());
    QCOMPARE(layer->opacity(), 0.75);
    QCOMPARE(layer->tiles().pixel({2, 3}).value(), 0x80112233U);
    QCOMPARE(layer->tiles().pixel({3, 3}).value(), 0x80445566U);
}

void TestDocumentFile::legacyVersion1CorruptionRejected()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("legacy.koma"));
    QVERIFY(writeFile(path, legacyFileWithPixels()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray good = f.readAll();
    QVERIFY(good.size() > 32);

    const auto tryBytes = [&](const QString& name, const QByteArray& bytes) {
        const QString p = dir.filePath(name);
        QVERIFY(writeFile(p, bytes));
        QString error;
        std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(p, &error));
        QVERIFY2(loaded == nullptr, qPrintable(name));
        QVERIFY2(!error.isEmpty(), qPrintable(name));
    };

    QByteArray crcFlip = good;
    crcFlip[good.size() - 1] = static_cast<char>(crcFlip[good.size() - 1] ^ 0x01);
    tryBytes(QStringLiteral("legacy-flip-crc.koma"), crcFlip);

    QByteArray midFlip = good;
    midFlip[20] = static_cast<char>(midFlip[20] ^ 0x40);
    tryBytes(QStringLiteral("legacy-flip-middle.koma"), midFlip);

    tryBytes(QStringLiteral("legacy-truncated.koma"), good.left(good.size() / 2));
    // A legacy v1 file with an unsupported version id is rejected, not guessed at.
    QByteArray badVersion = good;
    badVersion[9] = static_cast<char>(7);
    // (CRC will no longer match either, but version rejection must also be safe.)
    tryBytes(QStringLiteral("legacy-unknown-version.koma"), badVersion);
}

QTEST_APPLESS_MAIN(TestDocumentFile)
#include "TestDocumentFile.moc"
