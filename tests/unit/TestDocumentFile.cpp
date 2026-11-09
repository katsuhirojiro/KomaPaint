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

static QByteArray minimalValidFile()
{
    FileBuilder b;
    b.raw("KOMAPAINT", 9);
    b.u16(DocumentFile::FormatVersion);
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
    return b.finish();
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
        ink->tiles().setPixel({10, 10}, 0x800000ffU);
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
    QCOMPARE(ink->tiles().pixel({10, 10}).value(), 0x800000ffU);
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

    expectRejected(QStringLiteral("version2.koma"), [] {
        FileBuilder b;
        b.raw("KOMAPAINT", 9);
        b.u16(2); // unknown version
        b.u32(4); b.u32(4); b.dbl(300.0); b.u8(1); b.u32(0xffffffffU);
        b.utf8(QStringLiteral("")); b.u32(0); b.u32(1);
        b.utf8(QStringLiteral("Layer 1")); b.u8(1); b.u8(0); b.dbl(1.0); b.u32(0);
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
    // Sanity: the untouched minimal file really is accepted.
    const QString okPath = dir.filePath(QStringLiteral("minimal.koma"));
    QVERIFY(writeFile(okPath, minimalValidFile()));
    QString error;
    std::unique_ptr<CanvasDocument> loaded(DocumentFile::open(okPath, &error));
    QVERIFY2(loaded != nullptr, qPrintable(error));
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

QTEST_APPLESS_MAIN(TestDocumentFile)
#include "TestDocumentFile.moc"
