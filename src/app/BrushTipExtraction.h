#pragma once
// Turning Illustration artwork into a brush tip.
//
// This is deliberately an application service, not a second renderer: it reads
// the same layer tiles, the same visible-layer compositor and the same selection
// mask the canvas uses. The result is an ordinary RGBA image; Brush Studio then
// stores it as a normal .kb resource and the production engines paint it.
#include "../canvas/CanvasDocument.h"
#include "../layers/LayerModel.h"

#include <QImage>
#include <QString>

#include <vector>

namespace komapaint::brushtip {

enum class Source {
    CurrentLayer,   // the active layer, or the active layer clipped to a selection
    SelectedLayers, // every layer the artist has selected
    MergedVisible   // what the canvas shows, including layer opacity and blend
};

// What the transparent parts of the artwork mean.
enum class Coverage {
    Colour,    // keep RGB and transparency: a full-colour brush tip
    Alpha,     // keep only the transparency outline: white tip with that alpha
    Luminance  // use darkness as coverage, so black ink becomes an opaque tip
};

struct Request {
    Source source = Source::CurrentLayer;
    bool useSelection = true;
    Coverage coverage = Coverage::Colour;
    // Rows are the LayerModel's rows. Empty means the active layer for
    // CurrentLayer and every visible layer for MergedVisible.
    std::vector<int> layerRows;
};

struct Result {
    QImage image;
    QRect sourceRect;
    QString error;
    bool ok() const { return !image.isNull() && error.isEmpty(); }
};

QString sourceName(Source source);
QString coverageName(Coverage coverage);
bool parseSource(const QString& text, Source* out);
bool parseCoverage(const QString& text, Coverage* out);

// What can be offered for this document without lying about it. A source that
// has no drawable pixels is absent rather than disabled with a vague reason.
struct Availability {
    bool currentLayer = false;
    bool selectedLayers = false;
    bool mergedVisible = false;
    bool hasSelection = false;
    int selectedLayerCount = 0;
    QString currentLayerName;
    QString reason;
};
Availability availability(const CanvasDocument& document, const std::vector<int>& layerRows = {});

// Never mutates the document. Selection coverage multiplies the extracted
// alpha; the source layer's opacity and blend mode are honoured. If there is
// no selection the empty margins are trimmed, because a brush tip surrounded
// by canvas-sized transparency is unusable.
Result extract(const CanvasDocument& document, const Request& request);

} // namespace komapaint::brushtip
