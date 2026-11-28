#pragma once
// Vocabulary shared by the brush model, its file format and its editors.
//
// Everything here is deliberately Qt-free: the model is data, and data should
// not need a GUI toolkit to be tested. The QML/QtObject binding is a later
// adapter (see docs/architecture/brush-studio-audit.md, stage 11).
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace komapaint::brush {

// Base diameters share the exact range of the live brush's signed integer
// property, QML numeric entry, and legacy preset records. This is a numeric
// boundary, not a canvas- or memory-size limit; rendering clips to the canvas.
inline constexpr int MinBrushDiameter = 1;
inline constexpr int MaxBrushDiameter = std::numeric_limits<int>::max();

// Which production engine paints the brush. The model is shared; the settings
// a brush can express are not (Part 8: engine-aware, never engine-inferior).
enum class Engine { Digital, Realistic };

// What a component contributes to the mark.
enum class ComponentKind {
    Tip,              // the primary footprint
    Image,            // an imported picture, colour preserved
    Texture,          // grain/tiling over the mark
    AlphaMask,        // multiplies coverage
    GrayscaleMask,    // multiplies coverage, tonal
    ColourImage,      // colour-carried component
    ProceduralShape,  // generated at paint time, no stored pixels
    Particle,         // source for particle generators
    BristleGroup,     // a bundle of bristles
    Pattern,          // repeating motif
    Sequence          // frame-ordered images
};

// How a component composites onto the accumulated mark.
enum class ComponentBlend { Normal, Multiply, Screen, Overlay, Height, Mask };

// How a group picks which of its members is used for a given stamp.
enum class SelectionMode { All, Random, Weighted, Sequence, Alternating, Combination };

// Where a texture is sampled. These are deliberately artist words, not shader
// words: Tip follows each stamp, Stroke follows the movement of the line, and
// Canvas stays still under the moving brush.
enum class TexturePlacement { Tip, Stroke, Canvas };

// How marks are produced. Generators are combinable, never mutually exclusive
// brush categories (Part 3).
enum class GeneratorKind {
    Continuous, Stamp, Particle, Spray, Bristle, Procedural, Pattern, Sequence, Multi
};

// Input signals a property can respond to (Part 5.11).
enum class Signal {
    Pressure, Tilt, Rotation, Velocity, Direction, Distance, Progress, Time, Random,
    Speed
};

// Shape of a randomization range (Part 6).
enum class Distribution { Uniform, Normal, Triangular, Constant };

// Control a setting is exposed with in Sub-Tool Properties (Part 10).
enum class ControlType { Slider, Number, Toggle, Choice, Curve };

// Stored pixel data formats. Anything else is converted on import, explicitly.
enum class ResourceKind { Grayscale8, Alpha8, Rgba8 };

// Where a parameter applies.
enum class ParameterScope { Brush, Component };

enum class ParameterUnit { Unit, Percent, Pixels, Degrees, Ratio };

const char* toString(Engine value) noexcept;
const char* toString(ComponentKind value) noexcept;
const char* toString(ComponentBlend value) noexcept;
const char* toString(SelectionMode value) noexcept;
const char* toString(TexturePlacement value) noexcept;
const char* toString(GeneratorKind value) noexcept;
const char* toString(Signal value) noexcept;
const char* toString(Distribution value) noexcept;
const char* toString(ControlType value) noexcept;
const char* toString(ResourceKind value) noexcept;
const char* toString(ParameterScope value) noexcept;
const char* toString(ParameterUnit value) noexcept;

bool parseEngine(std::string_view text, Engine* out) noexcept;
bool parseComponentKind(std::string_view text, ComponentKind* out) noexcept;
bool parseComponentBlend(std::string_view text, ComponentBlend* out) noexcept;
bool parseSelectionMode(std::string_view text, SelectionMode* out) noexcept;
bool parseTexturePlacement(std::string_view text, TexturePlacement* out) noexcept;
bool parseGeneratorKind(std::string_view text, GeneratorKind* out) noexcept;
bool parseSignal(std::string_view text, Signal* out) noexcept;
bool parseDistribution(std::string_view text, Distribution* out) noexcept;
bool parseControlType(std::string_view text, ControlType* out) noexcept;
bool parseResourceKind(std::string_view text, ResourceKind* out) noexcept;
bool parseParameterScope(std::string_view text, ParameterScope* out) noexcept;
bool parseParameterUnit(std::string_view text, ParameterUnit* out) noexcept;

// An ordered string->double bag. Order is preserved so a saved document is
// byte-reproducible; unknown keys are legal inside a bag and validated against
// the parameter registry by BrushDocument::validate().
class ParamBag {
public:
    void set(std::string name, double value);
    void erase(std::string_view name);
    void clear() { m_entries.clear(); }
    bool contains(std::string_view name) const;
    double value(std::string_view name, double fallback = 0.0) const;
    const std::vector<std::pair<std::string, double>>& entries() const { return m_entries; }
    std::vector<std::pair<std::string, double>>& entries() { return m_entries; }
    bool empty() const { return m_entries.empty(); }
    bool operator==(const ParamBag&) const = default;
private:
    std::vector<std::pair<std::string, double>> m_entries;
};

// One point of a response curve, in normalised space (0..1 on both axes).
struct CurvePoint {
    double x = 0.0;
    double y = 0.0;
    bool operator==(const CurvePoint&) const = default;
};

// A monotonic-in-x piecewise linear curve. Arbitrary point counts, because a
// curve an artist cannot shape is not a custom curve (Part 5.13).
class ResponseCurve {
public:
    ResponseCurve();
    explicit ResponseCurve(std::vector<CurvePoint> points);
    static ResponseCurve linear() { return ResponseCurve(); }
    const std::vector<CurvePoint>& points() const { return m_points; }
    bool setPoints(std::vector<CurvePoint> points);
    double evaluate(double x) const noexcept;
    bool operator==(const ResponseCurve&) const = default;
private:
    void sortAndClamp();
    std::vector<CurvePoint> m_points;
};

} // namespace komapaint::brush
