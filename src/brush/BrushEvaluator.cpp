#include "brush/BrushEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace komapaint::brush {
namespace {

constexpr double kPi = 3.14159265358979323846;

// splitmix64: small, fast, and identical on every platform, which matters
// because a fixed seed has to mean one picture everywhere.
std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t hashBytes(std::uint64_t seed, std::string_view text) noexcept {
    std::uint64_t h = seed;
    for (char c : text) {
        h = mix(h ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
    return h;
}

std::uint64_t keyFor(std::uint64_t seed, std::string_view text, std::uint64_t index) noexcept {
    return mix(hashBytes(seed, text) ^ mix(index));
}

double clamp01(double v) noexcept { return std::clamp(v, 0.0, 1.0); }

double uniformFromKey(std::uint64_t key) noexcept {
    return static_cast<double>(mix(key) >> 11) * (1.0 / 9007199254740992.0);
}

const ParameterInfo* infoFor(std::string_view name) { return findParameter(name); }

// These kinds are source images for another component's mask, not marks of
// their own. A group may contain them so the artist can reuse the same grain
// across several tips; the renderer consumes them through maskComponentId.
bool paintsMark(ComponentKind kind) noexcept {
    return kind != ComponentKind::Texture && kind != ComponentKind::AlphaMask &&
           kind != ComponentKind::GrayscaleMask;
}

double registryDefault(std::string_view name) {
    const ParameterInfo* info = infoFor(name);
    return info ? info->defaultValue : 0.0;
}

// Degrees parameters respond additively (a rotation of 30 degrees means 30
// degrees); everything else scales the base value multiplicatively, which is
// what "pressure to size" and friends mean.
bool isAngular(std::string_view name) {
    const ParameterInfo* info = infoFor(name);
    return info && info->unit == ParameterUnit::Degrees;
}

double signalOf(Signal signal, const InputSignalSample& in) noexcept {
    switch (signal) {
    case Signal::Pressure: return clamp01(in.pressure);
    case Signal::Tilt: return clamp01(in.tilt);
    case Signal::Rotation:
        return clamp01(in.rotationRadians / (2.0 * kPi) + 0.5);
    case Signal::Velocity: return clamp01(in.velocity);
    case Signal::Direction: return clamp01(in.directionRadians / (2.0 * kPi) + 0.5);
    case Signal::Distance: return clamp01(in.distance);
    case Signal::Progress: return clamp01(in.progress);
    case Signal::Time: return clamp01(in.time);
    case Signal::Random: return 0.5;   // resolved per stamp by the caller's seed
    }
    return 0.5;
}

} // namespace

double BrushEvaluator::signalValue(Signal signal, const InputSignalSample& input) noexcept {
    return signalOf(signal, input);
}

double BrushEvaluator::baseParameter(const BrushDocument& document, std::string_view name) {
    return document.parameters.value(name, registryDefault(name));
}

double BrushEvaluator::dynamicParameter(const BrushDocument& document, std::string_view name,
                                        const InputSignalSample& input) {
    double value = baseParameter(document, name);
    for (const auto& binding : document.dynamics) {
        if (binding.parameter != name) continue;
        if (!(binding.amount > 0.0)) continue;
        const double curve = binding.curve.evaluate(signalOf(binding.signal, input));
        if (isAngular(name)) {
            // -1..1 across the curve, scaled to a half turn.
            value += binding.amount * (curve * 2.0 - 1.0) * 180.0;
        } else {
            // Lerp from "no effect" to "full curve", so amount is a strength
            // rather than a switch.
            value *= (1.0 - binding.amount) + binding.amount * curve;
        }
    }
    return value;
}

double BrushEvaluator::uniform01(std::uint64_t key) noexcept {
    // 53 significant bits, the most a double can hold without bias.
    return static_cast<double>(mix(key) >> 11) * (1.0 / 9007199254740992.0);
}

double BrushEvaluator::resolvedParameter(const BrushDocument& document, std::string_view name,
                                         const StampContext& context) {
    double value = dynamicParameter(document, name, context.input);

    for (const auto& rule : document.randomization) {
        if (!rule.enabled || rule.parameter != name) continue;
        // A per-stroke rule uses one draw for the whole stroke; a per-stamp
        // rule draws again for every stamp. sharedSeed lets a brush keep the
        // same variation across strokes when the artist wants that.
        const std::uint64_t index = rule.perStamp ? context.stampIndex : 0u;
        // A shared rule (or a rule the artist pinned to a seed) is the same on
        // every stroke; an unpinned one varies per stroke, which is what a
        // randomly textured brush is supposed to do. Either way the same
        // inputs give the same picture.
        const std::uint64_t base = rule.sharedSeed
            ? static_cast<std::uint64_t>(context.brushSeed)
            : (rule.seed != 0
                   ? static_cast<std::uint64_t>(rule.seed)
                   : static_cast<std::uint64_t>(context.brushSeed) ^
                         (static_cast<std::uint64_t>(context.strokeSeed) * 0x9E3779B97F4A7C15ull));
        const std::uint64_t key = keyFor(base, name, index);
        double t = uniform01(key);
        switch (rule.distribution) {
        case Distribution::Uniform:
            break;
        case Distribution::Constant:
            t = 0.5;
            break;
        case Distribution::Normal: {
            // Two draws averaged is a cheap, bounded bell; bounded matters
            // because a normal draw must never push a size below zero.
            const double second = uniform01(keyFor(key, name, 1));
            t = clamp01(0.5 + ((t + second) * 0.5 - 0.5) * 2.2);
            break;
        }
        case Distribution::Triangular: {
            const double second = uniform01(keyFor(key, name, 2));
            t = 0.5 * (t + second);
            break;
        }
        }
        value += rule.minimum + (rule.maximum - rule.minimum) * t;
    }

    const ParameterInfo* info = infoFor(name);
    if (info) value = std::clamp(value, info->minimum, info->maximum);
    return value;
}

namespace {

// Chooses which components a group contributes, honouring its own rule, then
// lets its child groups contribute theirs. That is what makes a foliage brush
// expressible: a leaf set, a branch set and a grass set, each choosing by its
// own rule, all inside one parent.
void selectFromGroup(const BrushDocument& document, const ComponentGroup& group,
                     const StampContext& context,
                     std::vector<const BrushComponent*>* out, EvaluationReport* report) {
    std::vector<const BrushComponent*> candidates;
    candidates.reserve(group.members.size());
    for (const auto& memberId : group.members) {
        const BrushComponent* c = document.component(memberId);
        if (!c || !c->visible || !paintsMark(c->kind)) continue;
        candidates.push_back(c);
    }
    if (report) report->componentsConsidered += static_cast<int>(candidates.size());

    const std::size_t count = candidates.size();
    const int wanted = std::max(1, group.membersPerStamp);
    const std::uint64_t seed = static_cast<std::uint64_t>(context.brushSeed) ^
                               (static_cast<std::uint64_t>(context.strokeSeed) << 17) ^
                               static_cast<std::uint64_t>(group.seed);

    if (count > 0) {
        switch (group.mode) {
        case SelectionMode::All:
        case SelectionMode::Combination:
            for (const auto* c : candidates) out->push_back(c);
            break;

        case SelectionMode::Random: {
            // Without replacement: one stamp never draws the same leaf twice.
            std::vector<std::size_t> order(count);
            for (std::size_t i = 0; i < count; ++i) order[i] = i;
            for (std::size_t i = count; i > 1; --i) {
                const std::size_t j = static_cast<std::size_t>(
                    uniformFromKey(keyFor(seed, "random", context.stampIndex * 131 + i)) * double(i));
                std::swap(order[i - 1], order[std::min(j, i - 1)]);
            }
            const auto take = std::min<std::size_t>(static_cast<std::size_t>(wanted), count);
            for (std::size_t i = 0; i < take; ++i) out->push_back(candidates[order[i]]);
            break;
        }

        case SelectionMode::Weighted: {
            std::vector<std::size_t> pool(count);
            for (std::size_t i = 0; i < count; ++i) pool[i] = i;
            const auto take = std::min<std::size_t>(static_cast<std::size_t>(wanted), count);
            for (std::size_t pick = 0; pick < take; ++pick) {
                double total = 0.0;
                for (std::size_t index : pool) total += std::max(0.0, candidates[index]->weight);
                if (total <= 0.0) break;
                const double target =
                    uniformFromKey(keyFor(seed, "weighted", context.stampIndex * 977 + pick)) * total;
                double running = 0.0;
                std::size_t chosen = pool.back();
                for (std::size_t index : pool) {
                    running += std::max(0.0, candidates[index]->weight);
                    if (target <= running) {
                        chosen = index;
                        break;
                    }
                }
                out->push_back(candidates[chosen]);
                pool.erase(std::remove(pool.begin(), pool.end(), chosen), pool.end());
                if (pool.empty()) break;
            }
            break;
        }

        case SelectionMode::Sequence: {
            const double step = std::max(1.0, document.parameters.value("sequence-step", 1.0));
            const auto advance =
                static_cast<std::uint64_t>(std::floor(double(context.stampIndex) / step));
            for (int i = 0; i < wanted; ++i) {
                const auto index = static_cast<std::size_t>((advance + std::uint64_t(i)) % count);
                out->push_back(candidates[index]);
            }
            break;
        }

        case SelectionMode::Alternating:
            for (int i = 0; i < wanted; ++i) {
                const auto index =
                    static_cast<std::size_t>((context.stampIndex + std::uint64_t(i)) % count);
                out->push_back(candidates[index]);
            }
            break;
        }
    }

    for (const auto& child : document.groups) {
        if (child.parentGroupId != group.id) continue;
        selectFromGroup(document, child, context, out, report);
    }
}

} // namespace

std::vector<DrawRequest> BrushEvaluator::evaluate(const BrushDocument& document,
                                                  const StampContext& context,
                                                  EvaluationReport* report) {
    EvaluationReport local;
    EvaluationReport* out = report ? report : &local;
    *out = EvaluationReport{};

    std::vector<DrawRequest> requests;

    const double size = resolvedParameter(document, "size", context);
    const double rotationBase = resolvedParameter(document, "rotation-offset", context);
    const double scatterX = resolvedParameter(document, "scatter-x", context) / 100.0;
    const double scatterY = resolvedParameter(document, "scatter-y", context) / 100.0;
    const double cluster = resolvedParameter(document, "cluster", context) / 100.0;
    const int stampCount = std::max(1, static_cast<int>(std::lround(
                                           resolvedParameter(document, "scatter-count", context))));
    const bool followDirection = resolvedParameter(document, "follow-direction", context) >= 0.5;
    const bool followRotation = resolvedParameter(document, "follow-rotation", context) >= 0.5;

    const double directionDegrees = context.input.directionRadians * 180.0 / kPi;
    const double penDegrees = context.input.rotationRadians * 180.0 / kPi;

    const std::uint64_t seed = static_cast<std::uint64_t>(context.brushSeed) ^
                               (static_cast<std::uint64_t>(context.strokeSeed) << 17);

    for (const auto& generator : document.generators) {
        if (!generator.enabled) continue;
        ++out->generatorsEvaluated;

        std::vector<const BrushComponent*> chosen;
        if (generator.groupId.empty()) {
            for (const auto& c : document.components) {
                if (c.groupId.empty() && c.visible && paintsMark(c.kind)) chosen.push_back(&c);
            }
        } else if (const ComponentGroup* group = document.group(generator.groupId)) {
            selectFromGroup(document, *group, context, &chosen, out);
        } else {
            out->notes.push_back("generator '" + generator.id + "' targets a missing group");
            continue;
        }
        if (chosen.empty()) continue;

        // Combination is "all at once, each carrying its weight", so the
        // weights become relative contribution instead of probability.
        double combinationTotal = 0.0;
        if (const ComponentGroup* group = document.group(generator.groupId)) {
            if (group->mode == SelectionMode::Combination) {
                for (const auto* c : chosen) combinationTotal += std::max(0.0, c->weight);
            }
        }

        for (int k = 0; k < stampCount; ++k) {
            // One cluster centre per stamp, so a clustered brush makes clumps
            // rather than even noise.
            const std::uint64_t stampKey = keyFor(seed, generator.id, context.stampIndex * 4099 + std::uint64_t(k));
            const double clusterX = (uniform01(stampKey) * 2.0 - 1.0) * scatterX * cluster;
            const double clusterY = (uniform01(stampKey ^ 0x51ull) * 2.0 - 1.0) * scatterY * cluster;

            for (const BrushComponent* component : chosen) {
                DrawRequest request;
                request.componentId = component->id;
                request.generatorId = generator.id;
                request.blend = component->blend;
                request.erases = component->erases;
                request.flipX = component->transform.flipX;
                request.flipY = component->transform.flipY;

                const double jitterX = (uniform01(stampKey ^ mix(hashBytes(seed, component->id))) * 2.0 - 1.0);
                const double jitterY = (uniform01(stampKey ^ mix(hashBytes(seed, component->id)) ^ 0x9E37ull) * 2.0 - 1.0);
                request.offsetX = component->transform.offsetX / 100.0 + scatterX * jitterX + clusterX;
                request.offsetY = component->transform.offsetY / 100.0 + scatterY * jitterY + clusterY;

                const double componentScale =
                    resolvedParameter(document, "component-scale", context) / 100.0;
                request.scale = component->transform.scale * componentScale;
                request.aspect = component->transform.aspect;

                double rotation = rotationBase + component->transform.rotationDegrees;
                if (followDirection) rotation += directionDegrees;
                if (followRotation) rotation += penDegrees;
                request.rotationDegrees = rotation;

                // Relative, deliberately: the brush's own opacity is a property
                // of the stroke definition the renderer already holds, so a
                // request only says what this component contributes to it.
                // Folding it in here as well would apply it twice.
                const double componentOpacity =
                    resolvedParameter(document, "component-opacity", context) / 100.0;
                double opacity = component->opacity * componentOpacity;
                if (combinationTotal > 0.0) {
                    opacity *= std::max(0.0, component->weight) / combinationTotal;
                }
                request.opacity = std::clamp(opacity, 0.0, 1.0);

                if (component->kind == ComponentKind::Sequence) {
                    request.sequenceKey = component->id;
                    const double step = std::max(1.0, document.parameters.value("sequence-step", 1.0));
                    request.frameIndex = static_cast<int>(
                        std::floor(double(context.stampIndex) / step));
                }

                requests.push_back(std::move(request));
            }
        }
    }

    (void)size;
    out->requestsProduced = static_cast<int>(requests.size());
    return requests;
}

} // namespace komapaint::brush
