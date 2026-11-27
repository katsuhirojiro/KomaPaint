#include "brush/BrushTypes.h"

#include <algorithm>
#include <cmath>

namespace komapaint::brush {
namespace {

template <typename T, std::size_t N>
bool parseFromTable(std::string_view text, const std::pair<const char*, T> (&table)[N], T* out) noexcept {
    if (!out) return false;
    for (const auto& entry : table) {
        if (text == entry.first) {
            *out = entry.second;
            return true;
        }
    }
    return false;
}

constexpr std::pair<const char*, Engine> kEngines[] = {
    {"digital", Engine::Digital},
    {"realistic", Engine::Realistic},
};

constexpr std::pair<const char*, ComponentKind> kComponentKinds[] = {
    {"tip", ComponentKind::Tip},
    {"image", ComponentKind::Image},
    {"texture", ComponentKind::Texture},
    {"alpha-mask", ComponentKind::AlphaMask},
    {"grayscale-mask", ComponentKind::GrayscaleMask},
    {"colour-image", ComponentKind::ColourImage},
    {"procedural-shape", ComponentKind::ProceduralShape},
    {"particle", ComponentKind::Particle},
    {"bristle-group", ComponentKind::BristleGroup},
    {"pattern", ComponentKind::Pattern},
    {"sequence", ComponentKind::Sequence},
};

constexpr std::pair<const char*, ComponentBlend> kBlends[] = {
    {"normal", ComponentBlend::Normal},
    {"multiply", ComponentBlend::Multiply},
    {"screen", ComponentBlend::Screen},
    {"overlay", ComponentBlend::Overlay},
    {"height", ComponentBlend::Height},
    {"mask", ComponentBlend::Mask},
};

constexpr std::pair<const char*, SelectionMode> kSelectionModes[] = {
    {"all", SelectionMode::All},
    {"random", SelectionMode::Random},
    {"weighted", SelectionMode::Weighted},
    {"sequence", SelectionMode::Sequence},
    {"alternating", SelectionMode::Alternating},
    {"combination", SelectionMode::Combination},
};

constexpr std::pair<const char*, GeneratorKind> kGenerators[] = {
    {"continuous", GeneratorKind::Continuous},
    {"stamp", GeneratorKind::Stamp},
    {"particle", GeneratorKind::Particle},
    {"spray", GeneratorKind::Spray},
    {"bristle", GeneratorKind::Bristle},
    {"procedural", GeneratorKind::Procedural},
    {"pattern", GeneratorKind::Pattern},
    {"sequence", GeneratorKind::Sequence},
    {"multi", GeneratorKind::Multi},
};

constexpr std::pair<const char*, Signal> kSignals[] = {
    {"pressure", Signal::Pressure},
    {"tilt", Signal::Tilt},
    {"rotation", Signal::Rotation},
    {"velocity", Signal::Velocity},
    {"direction", Signal::Direction},
    {"distance", Signal::Distance},
    {"progress", Signal::Progress},
    {"time", Signal::Time},
    {"random", Signal::Random},
    {"speed", Signal::Speed},
};

constexpr std::pair<const char*, Distribution> kDistributions[] = {
    {"uniform", Distribution::Uniform},
    {"normal", Distribution::Normal},
    {"triangular", Distribution::Triangular},
    {"constant", Distribution::Constant},
};

constexpr std::pair<const char*, ControlType> kControls[] = {
    {"slider", ControlType::Slider},
    {"number", ControlType::Number},
    {"toggle", ControlType::Toggle},
    {"choice", ControlType::Choice},
    {"curve", ControlType::Curve},
};

constexpr std::pair<const char*, ResourceKind> kResourceKinds[] = {
    {"grayscale8", ResourceKind::Grayscale8},
    {"alpha8", ResourceKind::Alpha8},
    {"rgba8", ResourceKind::Rgba8},
};

constexpr std::pair<const char*, ParameterScope> kScopes[] = {
    {"brush", ParameterScope::Brush},
    {"component", ParameterScope::Component},
};

constexpr std::pair<const char*, ParameterUnit> kUnits[] = {
    {"unit", ParameterUnit::Unit},
    {"percent", ParameterUnit::Percent},
    {"pixels", ParameterUnit::Pixels},
    {"degrees", ParameterUnit::Degrees},
    {"ratio", ParameterUnit::Ratio},
};

} // namespace

const char* toString(Engine value) noexcept {
    return kEngines[value == Engine::Realistic ? 1 : 0].first;
}
const char* toString(ComponentKind value) noexcept {
    return kComponentKinds[static_cast<std::size_t>(value)].first;
}
const char* toString(ComponentBlend value) noexcept {
    return kBlends[static_cast<std::size_t>(value)].first;
}
const char* toString(SelectionMode value) noexcept {
    return kSelectionModes[static_cast<std::size_t>(value)].first;
}
const char* toString(GeneratorKind value) noexcept {
    return kGenerators[static_cast<std::size_t>(value)].first;
}
const char* toString(Signal value) noexcept {
    return kSignals[static_cast<std::size_t>(value)].first;
}
const char* toString(Distribution value) noexcept {
    return kDistributions[static_cast<std::size_t>(value)].first;
}
const char* toString(ControlType value) noexcept {
    return kControls[static_cast<std::size_t>(value)].first;
}
const char* toString(ResourceKind value) noexcept {
    return kResourceKinds[static_cast<std::size_t>(value)].first;
}
const char* toString(ParameterScope value) noexcept {
    return kScopes[static_cast<std::size_t>(value)].first;
}
const char* toString(ParameterUnit value) noexcept {
    return kUnits[static_cast<std::size_t>(value)].first;
}

bool parseEngine(std::string_view text, Engine* out) noexcept { return parseFromTable(text, kEngines, out); }
bool parseComponentKind(std::string_view text, ComponentKind* out) noexcept { return parseFromTable(text, kComponentKinds, out); }
bool parseComponentBlend(std::string_view text, ComponentBlend* out) noexcept { return parseFromTable(text, kBlends, out); }
bool parseSelectionMode(std::string_view text, SelectionMode* out) noexcept { return parseFromTable(text, kSelectionModes, out); }
bool parseGeneratorKind(std::string_view text, GeneratorKind* out) noexcept { return parseFromTable(text, kGenerators, out); }
bool parseSignal(std::string_view text, Signal* out) noexcept { return parseFromTable(text, kSignals, out); }
bool parseDistribution(std::string_view text, Distribution* out) noexcept { return parseFromTable(text, kDistributions, out); }
bool parseControlType(std::string_view text, ControlType* out) noexcept { return parseFromTable(text, kControls, out); }
bool parseResourceKind(std::string_view text, ResourceKind* out) noexcept { return parseFromTable(text, kResourceKinds, out); }
bool parseParameterScope(std::string_view text, ParameterScope* out) noexcept { return parseFromTable(text, kScopes, out); }
bool parseParameterUnit(std::string_view text, ParameterUnit* out) noexcept { return parseFromTable(text, kUnits, out); }

// ---- ParamBag ---------------------------------------------------------------

void ParamBag::set(std::string name, double value) {
    for (auto& entry : m_entries) {
        if (entry.first == name) {
            entry.second = value;
            return;
        }
    }
    m_entries.emplace_back(std::move(name), value);
}

void ParamBag::erase(std::string_view name) {
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [name](const auto& entry) { return entry.first == name; }),
                    m_entries.end());
}

bool ParamBag::contains(std::string_view name) const {
    for (const auto& entry : m_entries) {
        if (entry.first == name) return true;
    }
    return false;
}

double ParamBag::value(std::string_view name, double fallback) const {
    for (const auto& entry : m_entries) {
        if (entry.first == name) return entry.second;
    }
    return fallback;
}

// ---- ResponseCurve ----------------------------------------------------------

ResponseCurve::ResponseCurve()
    : m_points{{0.0, 0.0}, {1.0, 1.0}} {}

ResponseCurve::ResponseCurve(std::vector<CurvePoint> points) {
    if (!setPoints(std::move(points))) {
        m_points = {{0.0, 0.0}, {1.0, 1.0}};
    }
}

bool ResponseCurve::setPoints(std::vector<CurvePoint> points) {
    if (points.size() < 2) return false;
    for (const auto& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    }
    m_points = std::move(points);
    sortAndClamp();
    return true;
}

void ResponseCurve::sortAndClamp() {
    for (auto& point : m_points) {
        point.x = std::clamp(point.x, 0.0, 1.0);
        point.y = std::clamp(point.y, 0.0, 1.0);
    }
    std::stable_sort(m_points.begin(), m_points.end(),
                     [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
}

double ResponseCurve::evaluate(double x) const noexcept {
    if (m_points.empty()) return 0.0;
    if (m_points.size() == 1) return m_points.front().y;
    if (x <= m_points.front().x) return m_points.front().y;
    if (x >= m_points.back().x) return m_points.back().y;
    for (std::size_t i = 1; i < m_points.size(); ++i) {
        const CurvePoint& hi = m_points[i];
        if (x > hi.x) continue;
        const CurvePoint& lo = m_points[i - 1];
        const double span = hi.x - lo.x;
        if (span <= 0.0) return hi.y;
        const double t = (x - lo.x) / span;
        return lo.y + (hi.y - lo.y) * t;
    }
    return m_points.back().y;
}

} // namespace komapaint::brush
