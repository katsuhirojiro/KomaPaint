#include "brush/BrushModel.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>

namespace komapaint::brush {
namespace {

bool needsPixels(ComponentKind kind) {
    switch (kind) {
    case ComponentKind::ProceduralShape:
    case ComponentKind::Particle:
    case ComponentKind::BristleGroup:
        return false;
    default:
        return true;
    }
}

bool isBrushScope(ParameterScope scope) { return scope == ParameterScope::Brush; }

// The five tip masks the production engines actually implement. Values are
// stored in the component's parameter bag as `tip-shape`.
int tipShapeForStyle(const std::string& style) {
    if (style == "textured") return 1;
    if (style == "bristle") return 2;
    if (style == "graphite") return 3;
    if (style == "stipple") return 4;
    return 0;  // round
}

std::string tipStyleForShape(double shape) {
    switch (static_cast<int>(shape)) {
    case 1: return "textured";
    case 2: return "bristle";
    case 3: return "graphite";
    case 4: return "stipple";
    default: return "round";
    }
}

// True when following `next` from some node returns to that node. `count` is
// the "no successor" sentinel. Used for nested groups and component masks.
template <typename NextFn>
bool hasCycle(std::size_t count, NextFn next) {
    for (std::size_t start = 0; start < count; ++start) {
        std::size_t node = next(start);
        std::size_t steps = 0;
        while (node < count) {
            if (node == start) return true;
            node = next(node);
            if (++steps > count) return true;
        }
    }
    return false;
}

std::string formatNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return buffer;
}

} // namespace

// ---- Basic value types ------------------------------------------------------

std::size_t BrushResource::expectedBytes() const {
    if (width <= 0 || height <= 0) return 0;
    const std::size_t px = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    switch (kind) {
    case ResourceKind::Rgba8:
        return px * 4;
    default:
        return px;
    }
}

// ---- ValidationResult -------------------------------------------------------

void ValidationResult::addError(std::string code, std::string detail) {
    m_issues.push_back({ValidationIssue::Severity::Error, std::move(code), std::move(detail)});
}

void ValidationResult::addWarning(std::string code, std::string detail) {
    m_issues.push_back({ValidationIssue::Severity::Warning, std::move(code), std::move(detail)});
}

bool ValidationResult::ok() const {
    for (const auto& issue : m_issues) {
        if (issue.severity == ValidationIssue::Severity::Error) return false;
    }
    return true;
}

bool ValidationResult::hasWarnings() const {
    for (const auto& issue : m_issues) {
        if (issue.severity == ValidationIssue::Severity::Warning) return true;
    }
    return false;
}

std::string ValidationResult::summary() const {
    for (const auto& issue : m_issues) {
        if (issue.severity == ValidationIssue::Severity::Error) {
            return issue.code + ": " + issue.detail;
        }
    }
    return "ok";
}

// ---- BrushDocument: construction and identity -------------------------------

BrushDocument::BrushDocument() = default;

BrushDocument::BrushDocument(std::string brushName) : name(std::move(brushName)) {}

bool BrushDocument::idInUse(std::string_view candidate) const {
    if (candidate.empty()) return false;
    for (const auto& c : components) {
        if (c.id == candidate) return true;
    }
    for (const auto& g : groups) {
        if (g.id == candidate) return true;
    }
    for (const auto& g : generators) {
        if (g.id == candidate) return true;
    }
    for (const auto& r : resources) {
        if (r.id == candidate) return true;
    }
    return false;
}

std::string BrushDocument::allocateId(std::string_view prefix) {
    // Derived from the highest existing suffix rather than a counter member, so
    // two documents with the same content compare equal.
    int highest = 0;
    auto consider = [&](const std::string& existing) {
        if (existing.size() <= prefix.size()) return;
        if (existing.compare(0, prefix.size(), prefix) != 0) return;
        int value = 0;
        for (std::size_t i = prefix.size(); i < existing.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(existing[i]))) return;
            value = value * 10 + (existing[i] - '0');
            if (value > 1000000) return;
        }
        highest = std::max(highest, value);
    };
    for (const auto& c : components) consider(c.id);
    for (const auto& g : groups) consider(g.id);
    for (const auto& g : generators) consider(g.id);
    for (const auto& r : resources) consider(r.id);

    std::string candidate;
    do {
        candidate = std::string(prefix) + std::to_string(++highest);
    } while (idInUse(candidate));
    return candidate;
}

// ---- Components -------------------------------------------------------------

BrushComponent* BrushDocument::component(std::string_view componentId) {
    for (auto& c : components) {
        if (c.id == componentId) return &c;
    }
    return nullptr;
}

const BrushComponent* BrushDocument::component(std::string_view componentId) const {
    for (const auto& c : components) {
        if (c.id == componentId) return &c;
    }
    return nullptr;
}

int BrushDocument::componentIndex(std::string_view componentId) const {
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (components[i].id == componentId) return static_cast<int>(i);
    }
    return -1;
}

bool BrushDocument::addComponent(BrushComponent c, int index) {
    if (c.id.empty()) c.id = allocateId("c");
    if (idInUse(c.id)) return false;
    ComponentGroup* target = c.groupId.empty() ? nullptr : group(c.groupId);
    if (!c.groupId.empty() && !target) return false;
    if (target) target->members.push_back(c.id);
    try {
        if (index < 0 || static_cast<std::size_t>(index) > components.size()) {
            components.push_back(std::move(c));
        } else {
            components.insert(components.begin() + index, std::move(c));
        }
    } catch (...) {
        if (target) target->members.pop_back();
        throw;
    }
    return true;
}

bool BrushDocument::removeComponent(std::string_view componentId, bool cascade) {
    const int index = componentIndex(componentId);
    if (index < 0) return false;

    // A component referenced as a mask is either removed too (cascade) or the
    // removal is refused: never leave a dangling reference behind.
    std::vector<std::string> dependents;
    for (const auto& c : components) {
        if (c.maskComponentId == componentId) dependents.push_back(c.id);
    }
    if (!dependents.empty() && !cascade) return false;

    // Detach from every group before erasing.
    for (auto& g : groups) std::erase(g.members, components[index].id);
    components.erase(components.begin() + index);

    if (cascade) {
        for (const auto& dependent : dependents) {
            removeComponent(dependent, true);
        }
    }
    return true;
}

bool BrushDocument::moveComponent(std::string_view componentId, int newIndex) {
    const int index = componentIndex(componentId);
    if (index < 0) return false;
    const int clamped = std::clamp(newIndex, 0, static_cast<int>(components.size()) - 1);
    if (clamped == index) return true;
    BrushComponent moved = components[static_cast<std::size_t>(index)];
    components.erase(components.begin() + index);
    components.insert(components.begin() + clamped, std::move(moved));
    return true;
}

bool BrushDocument::setComponentGroup(std::string_view componentId, std::string_view groupId) {
    BrushComponent* c = component(componentId);
    if (!c) return false;
    if (groupId.empty()) {
        for (auto& g : groups) std::erase(g.members, c->id);
        c->groupId.clear();
        return true;
    }
    const ComponentGroup* target = group(groupId);
    if (!target) return false;
    // Reassigning to the same group must not change its paint sequence.
    const auto at = std::find(target->members.begin(), target->members.end(), c->id);
    const int index = c->groupId == groupId && at != target->members.end()
        ? static_cast<int>(at - target->members.begin()) : -1;
    return addToGroup(groupId, componentId, index);
}

bool BrushDocument::duplicateComponent(std::string_view componentId, std::string* newId) {
    const BrushComponent* source = component(componentId);
    if (!source) return false;
    BrushComponent copy = *source;
    copy.id = allocateId("c");
    copy.name = source->name + " copy";
    const int index = componentIndex(componentId);
    std::string created = newId ? copy.id : std::string{};
    if (!addComponent(std::move(copy), index + 1)) return false;
    if (newId) *newId = std::move(created);
    return true;
}

// ---- Groups -----------------------------------------------------------------

ComponentGroup* BrushDocument::group(std::string_view groupId) {
    for (auto& g : groups) {
        if (g.id == groupId) return &g;
    }
    return nullptr;
}

const ComponentGroup* BrushDocument::group(std::string_view groupId) const {
    for (const auto& g : groups) {
        if (g.id == groupId) return &g;
    }
    return nullptr;
}

bool BrushDocument::addGroup(ComponentGroup g) {
    if (g.id.empty()) g.id = allocateId("g");
    if (idInUse(g.id) || !g.members.empty()) return false;
    if (!g.parentGroupId.empty() && !group(g.parentGroupId)) return false;
    groups.push_back(std::move(g));
    return true;
}

bool BrushDocument::groupHasGenerator(std::string_view groupId) const {
    return !groupId.empty() && std::ranges::any_of(generators, [groupId](const BrushGenerator& g) {
        return g.groupId == groupId;
    });
}

bool BrushDocument::removeGroup(std::string_view groupId, bool cascade) {
    if (!group(groupId) || groupHasGenerator(groupId)) return false;
    for (const auto& other : groups) {
        if (other.id != groupId && other.parentGroupId == groupId && !cascade) return false;
    }
    const std::string key(groupId);
    for (auto& c : components) {
        if (c.groupId == key) c.groupId.clear();
    }
    for (auto& other : groups) {
        if (other.parentGroupId == key) other.parentGroupId.clear();
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                                [&key](const ComponentGroup& g) { return g.id == key; }),
                 groups.end());
    return true;
}

bool BrushDocument::addToGroup(std::string_view groupId, std::string_view componentId, int index) {
    ComponentGroup* g = group(groupId);
    BrushComponent* c = component(componentId);
    if (!g || !c) return false;
    // Prepare the new membership before touching the old one. In particular,
    // allocation failure must not leave the component without a group.
    std::string targetId(groupId);
    g->members.push_back(c->id);
    for (auto& other : groups) {
        if (&other != g) std::erase(other.members, c->id);
    }
    auto appended = g->members.end() - 1;
    g->members.erase(std::remove(g->members.begin(), appended, c->id), appended);
    if (index >= 0 && static_cast<std::size_t>(index) < g->members.size() - 1) {
        std::rotate(g->members.begin() + index, g->members.end() - 1, g->members.end());
    }
    c->groupId = std::move(targetId);
    return true;
}

bool BrushDocument::removeFromGroup(std::string_view groupId, std::string_view componentId) {
    ComponentGroup* g = group(groupId);
    BrushComponent* c = component(componentId);
    if (!g || !c) return false;
    if (c->groupId == groupId) return setComponentGroup(componentId, {});
    std::erase(g->members, c->id);
    return true;
}

std::vector<const BrushComponent*> BrushDocument::groupMembers(std::string_view groupId) const {
    std::vector<const BrushComponent*> result;
    const ComponentGroup* g = group(groupId);
    if (!g) return result;
    for (const auto& memberId : g->members) {
        if (const BrushComponent* c = component(memberId)) result.push_back(c);
    }
    return result;
}

// ---- Generators, dynamics, randomization, exposure --------------------------

bool BrushDocument::addGenerator(BrushGenerator g) {
    if (g.id.empty()) g.id = allocateId("n");
    if (idInUse(g.id)) return false;
    if (!g.groupId.empty() && !group(g.groupId)) return false;
    generators.push_back(std::move(g));
    return true;
}

bool BrushDocument::removeGenerator(std::string_view generatorId) {
    const auto before = generators.size();
    generators.erase(std::remove_if(generators.begin(), generators.end(),
                                    [generatorId](const BrushGenerator& g) {
                                        return g.id == generatorId;
                                    }),
                     generators.end());
    return generators.size() != before;
}

BrushGenerator* BrushDocument::generator(std::string_view generatorId) {
    for (auto& g : generators) {
        if (g.id == generatorId) return &g;
    }
    return nullptr;
}

bool BrushDocument::setParameter(std::string_view parameterName, double value) {
    const ParameterInfo* info = findParameter(parameterName);
    if (!info || !isBrushScope(info->scope)) return false;
    if (!std::isfinite(value)) return false;
    parameters.set(std::string(parameterName), value);
    return true;
}

double BrushDocument::parameter(std::string_view parameterName, double fallback) const {
    return parameters.value(parameterName, fallback);
}

bool BrushDocument::bindDynamics(DynamicsBinding binding) {
    if (!findParameter(binding.parameter)) return false;
    if (!std::isfinite(binding.amount) || binding.amount < 0.0) return false;
    for (auto& existing : dynamics) {
        if (existing.parameter == binding.parameter && existing.signal == binding.signal) {
            existing = std::move(binding);
            return true;
        }
    }
    dynamics.push_back(std::move(binding));
    return true;
}

bool BrushDocument::removeDynamics(std::string_view parameter, Signal signal) {
    const auto before = dynamics.size();
    dynamics.erase(std::remove_if(dynamics.begin(), dynamics.end(),
                                  [&](const DynamicsBinding& b) {
                                      return b.parameter == parameter && b.signal == signal;
                                  }),
                   dynamics.end());
    return dynamics.size() != before;
}

bool BrushDocument::addRandomization(RandomizationRule rule) {
    if (!findParameter(rule.parameter)) return false;
    if (!std::isfinite(rule.minimum) || !std::isfinite(rule.maximum)) return false;
    if (rule.minimum > rule.maximum) return false;
    for (auto& existing : randomization) {
        if (existing.parameter == rule.parameter) {
            existing = std::move(rule);
            return true;
        }
    }
    randomization.push_back(std::move(rule));
    return true;
}

bool BrushDocument::removeRandomization(std::string_view parameter) {
    const auto before = randomization.size();
    randomization.erase(std::remove_if(randomization.begin(), randomization.end(),
                                       [&](const RandomizationRule& r) {
                                           return r.parameter == parameter;
                                       }),
                        randomization.end());
    return randomization.size() != before;
}

bool BrushDocument::expose(ExposedProperty property) {
    if (!findParameter(property.parameter)) return false;
    for (auto& existing : exposed) {
        if (existing.parameter == property.parameter) {
            existing = std::move(property);
            return true;
        }
    }
    exposed.push_back(std::move(property));
    return true;
}

bool BrushDocument::unexpose(std::string_view parameter) {
    const auto before = exposed.size();
    exposed.erase(std::remove_if(exposed.begin(), exposed.end(),
                                 [&](const ExposedProperty& p) { return p.parameter == parameter; }),
                  exposed.end());
    return exposed.size() != before;
}

std::string effectiveSection(const ExposedProperty& property) {
    if (!property.section.empty()) return property.section;
    const char* section = parameterSection(property.parameter);
    return section ? std::string(section) : std::string();
}

namespace {

// A group an artist invented sorts after the built-in thirteen, and then
// alphabetically among other invented groups, so two custom groups still have a
// stable order instead of depending on insertion.
std::pair<int, std::string> sectionKey(const ExposedProperty& property) {
    const std::string section = effectiveSection(property);
    const int rank = sectionRank(section);
    return {rank < 0 ? int(parameterSections().size()) : rank, section};
}

} // namespace

bool exposedLess(const ExposedProperty& a, const ExposedProperty& b) {
    const auto keyA = sectionKey(a);
    const auto keyB = sectionKey(b);
    if (keyA != keyB) return keyA < keyB;
    if (a.order != b.order) return a.order < b.order;
    return a.parameter < b.parameter;
}

std::vector<const ExposedProperty*> BrushDocument::visibleExposed() const {
    std::vector<const ExposedProperty*> result;
    for (const auto& p : exposed) {
        if (p.visible) result.push_back(&p);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const ExposedProperty* a, const ExposedProperty* b) {
                         return exposedLess(*a, *b);
                     });
    return result;
}

// ---- Resources --------------------------------------------------------------

BrushResource* BrushDocument::resource(std::string_view resourceId) {
    for (auto& r : resources) {
        if (r.id == resourceId) return &r;
    }
    return nullptr;
}

const BrushResource* BrushDocument::resource(std::string_view resourceId) const {
    for (const auto& r : resources) {
        if (r.id == resourceId) return &r;
    }
    return nullptr;
}

bool BrushDocument::addResource(BrushResource r) {
    if (r.id.empty()) r.id = allocateId("p");
    if (r.width <= 0 || r.height <= 0) return false;
    if (r.pixels.size() != r.expectedBytes()) return false;
    if (BrushResource* existing = resource(r.id)) {
        *existing = std::move(r);
        return true;
    }
    resources.push_back(std::move(r));
    return true;
}

int BrushDocument::resourceUsage(std::string_view resourceId) const {
    int count = 0;
    for (const auto& c : components) {
        if (c.resourceId == resourceId) ++count;
    }
    return count;
}

std::size_t BrushDocument::resourceBytes() const {
    std::size_t total = 0;
    for (const auto& r : resources) total += r.pixels.size();
    return total;
}

// ---- Validation -------------------------------------------------------------

ValidationResult BrushDocument::validate() const {
    ValidationResult result;

    if (name.empty()) result.addError("missing-name", "The brush needs a name.");

    // Ids are the backbone of every reference in the document.
    std::set<std::string> seen;
    for (const auto& c : components) {
        if (c.id.empty()) result.addError("missing-id", "A component has no id.");
        else if (!seen.insert(c.id).second)
            result.addError("duplicate-id", "Two components share the id '" + c.id + "'.");
    }
    for (const auto& g : groups) {
        if (g.id.empty()) result.addError("missing-id", "A group has no id.");
        else if (!seen.insert(g.id).second)
            result.addError("duplicate-id", "The id '" + g.id + "' is used twice.");
    }
    for (const auto& g : generators) {
        if (g.id.empty()) result.addError("missing-id", "A generator has no id.");
        else if (!seen.insert(g.id).second)
            result.addError("duplicate-id", "The id '" + g.id + "' is used twice.");
    }
    for (const auto& r : resources) {
        if (r.id.empty()) result.addError("missing-id", "A resource has no id.");
        else if (!seen.insert(r.id).second)
            result.addError("duplicate-id", "The id '" + r.id + "' is used twice.");
    }

    if (components.empty()) {
        result.addWarning("empty-brush", "The brush has no components yet.");
    }
    if (generators.empty()) {
        result.addWarning("no-generator", "The brush has no generator, so it cannot paint.");
    }

    // Components: resources, groups, masks.
    for (const auto& c : components) {
        if (needsPixels(c.kind)) {
            if (c.resourceId.empty()) {
                result.addError("missing-resource",
                                "Component '" + c.id + "' needs pixel data but has no resource.");
            } else if (!resource(c.resourceId)) {
                result.addError("unknown-resource",
                                "Component '" + c.id + "' points at missing resource '" +
                                    c.resourceId + "'.");
            }
        }
        if (!c.groupId.empty()) {
            const ComponentGroup* g = group(c.groupId);
            if (!g) {
                result.addError("unknown-group", "Component '" + c.id + "' is in unknown group '" +
                                                     c.groupId + "'.");
            } else if (std::find(g->members.begin(), g->members.end(), c.id) == g->members.end()) {
                result.addError("group-membership",
                                "Component '" + c.id + "' claims group '" + c.groupId +
                                    "' but the group does not list it.");
            }
        }
        if (!c.maskComponentId.empty()) {
            if (c.maskComponentId == c.id) {
                result.addError("mask-self", "Component '" + c.id + "' masks itself.");
            } else if (!component(c.maskComponentId)) {
                result.addError("unknown-mask", "Component '" + c.id + "' is masked by missing '" +
                                                    c.maskComponentId + "'.");
            }
        }
        const auto& t = c.transform;
        if (!std::isfinite(t.offsetX) || !std::isfinite(t.offsetY) ||
            !std::isfinite(t.rotationDegrees) || !(t.scale > 0.0 && std::isfinite(t.scale)) ||
            !(t.aspect > 0.0 && std::isfinite(t.aspect))) {
            result.addError("invalid-transform", "Component '" + c.id +
                            "' needs a finite transform and positive scale and aspect.");
        }
        if (!std::isfinite(c.opacity) || c.opacity < 0.0 || c.opacity > 1.0) {
            result.addError("opacity-range", "Component '" + c.id + "' has opacity outside 0..1.");
        }
        if (c.blend < ComponentBlend::Normal || c.blend > ComponentBlend::Mask) {
            result.addError("invalid-blend", "Component '" + c.id + "' has an invalid blend mode.");
        }
        if (c.weight < 0.0 || !std::isfinite(c.weight)) {
            result.addError("weight-range", "Component '" + c.id + "' needs a finite, nonnegative weight.");
        }
        for (const auto& [key, value] : c.parameters.entries()) {
            const ParameterInfo* info = findParameter(key);
            if (!info) {
                result.addError("unknown-parameter",
                                "Component '" + c.id + "' uses unknown parameter '" + key + "'.");
            } else if (!parameterInRange(*info, value)) {
                result.addError("parameter-out-of-range",
                                "Component '" + c.id + "' sets '" + key + "' to " +
                                    formatNumber(value) + ", outside " + formatNumber(info->minimum) +
                                    ".." + formatNumber(info->maximum) + ".");
            } else if (key == "tip-shape" && value != std::trunc(value)) {
                result.addError("invalid-tip-shape", "Component '" + c.id + "' needs a whole-number tip shape.");
            }
        }
    }

    // Mask cycles.
    {
        const std::size_t n = components.size();
        std::vector<std::size_t> nextIndex(n, n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::string& maskId = components[i].maskComponentId;
            if (maskId.empty()) continue;
            const int idx = componentIndex(maskId);
            if (idx >= 0) nextIndex[i] = static_cast<std::size_t>(idx);
        }
        if (hasCycle(n, [&](std::size_t i) { return nextIndex[i]; })) {
            result.addError("mask-cycle", "Components mask each other in a cycle.");
        }
    }

    // Groups: nesting, membership, selection rules.
    for (const auto& g : groups) {
        if (!g.parentGroupId.empty()) {
            if (!group(g.parentGroupId)) {
                result.addError("unknown-parent-group", "Group '" + g.id + "' has missing parent '" +
                                                            g.parentGroupId + "'.");
            }
        }
        if (g.members.empty() && !(groups.size() == 1 && groups.front().id == g.id)) {
            result.addWarning("empty-group", "Group '" + g.id + "' has no members.");
        }
        std::set<std::string_view> seenMembers;
        for (const auto& memberId : g.members) {
            if (!seenMembers.insert(memberId).second) {
                result.addError("duplicate-member", "Group '" + g.id + "' lists component '" +
                                                   memberId + "' more than once.");
            }
            const BrushComponent* c = component(memberId);
            if (!c) {
                result.addError("unknown-member", "Group '" + g.id + "' lists missing component '" +
                                                      memberId + "'.");
            } else if (c->groupId != g.id) {
                result.addError("group-membership", "Group '" + g.id + "' lists component '" +
                                                        memberId + "' which is not in it.");
            }
        }
        if (g.membersPerStamp < 1) {
            result.addError("members-per-stamp", "Group '" + g.id + "' places fewer than one member.");
        } else if (!g.members.empty() && g.membersPerStamp > static_cast<int>(g.members.size())) {
            result.addWarning("members-per-stamp",
                              "Group '" + g.id + "' asks for more members per stamp than it holds.");
        }
        if (g.mode == SelectionMode::Weighted) {
            double total = 0.0;
            for (const auto& memberId : g.members) {
                if (const BrushComponent* c = component(memberId)) total += std::max(0.0, c->weight);
            }
            if (total <= 0.0 && !g.members.empty()) {
                result.addError("weighted-empty",
                                "Group '" + g.id + "' is weighted but every member weighs zero.");
            }
        }
    }
    {
        const std::size_t n = groups.size();
        std::vector<std::size_t> nextIndex(n, n);
        for (std::size_t i = 0; i < n; ++i) {
            if (groups[i].parentGroupId.empty()) continue;
            for (std::size_t j = 0; j < n; ++j) {
                if (groups[j].id == groups[i].parentGroupId) {
                    nextIndex[i] = j;
                    break;
                }
            }
        }
        if (hasCycle(n, [&](std::size_t i) { return nextIndex[i]; })) {
            result.addError("group-cycle", "Groups are nested inside each other in a cycle.");
        }
    }

    // Generators.
    for (const auto& g : generators) {
        if (!g.groupId.empty() && !group(g.groupId)) {
            result.addError("unknown-generator-group",
                            "Generator '" + g.id + "' draws from missing group '" + g.groupId + "'.");
        }
        for (const auto& [key, value] : g.parameters.entries()) {
            const ParameterInfo* info = findParameter(key);
            if (!info) {
                result.addError("unknown-parameter",
                                "Generator '" + g.id + "' uses unknown parameter '" + key + "'.");
            } else if (!parameterInRange(*info, value)) {
                result.addError("parameter-out-of-range",
                                "Generator '" + g.id + "' sets '" + key + "' outside its range.");
            }
        }
    }

    // Brush-scope values.
    for (const auto& [key, value] : parameters.entries()) {
        const ParameterInfo* info = findParameter(key);
        if (!info) {
            result.addError("unknown-parameter", "Unknown brush parameter '" + key + "'.");
        } else if (!isBrushScope(info->scope)) {
            result.addError("scope-mismatch", "'" + key + "' is a component parameter.");
        } else if (!parameterInRange(*info, value)) {
            result.addError("parameter-out-of-range",
                            "'" + key + "' is " + formatNumber(value) + ", outside " +
                                formatNumber(info->minimum) + ".." + formatNumber(info->maximum) + ".");
        }
    }

    // Dynamics, randomization, exposure.
    std::set<std::string> bindingKeys;
    for (const auto& b : dynamics) {
        if (!findParameter(b.parameter)) {
            result.addError("unknown-parameter", "Dynamics target '" + b.parameter + "' is unknown.");
        }
        if (!(b.amount >= 0.0) || !std::isfinite(b.amount)) {
            result.addError("amount-range", "Dynamics amount must be finite and not negative.");
        }
        const std::string key = b.parameter + "/" + toString(b.signal);
        if (!bindingKeys.insert(key).second) {
            result.addError("duplicate-binding", "Two bindings target '" + key + "'.");
        }
    }
    std::set<std::string> randomized;
    for (const auto& r : randomization) {
        if (!findParameter(r.parameter)) {
            result.addError("unknown-parameter", "Randomization target '" + r.parameter +
                                                     "' is unknown.");
        }
        if (!randomized.insert(r.parameter).second) {
            result.addError("duplicate-randomization",
                            "'" + r.parameter + "' has more than one randomization rule.");
        }
        if (r.minimum > r.maximum) {
            result.addError("random-range", "'" + r.parameter + "' has minimum above maximum.");
        }
    }
    std::set<std::string> exposedParams;
    for (const auto& p : exposed) {
        if (!findParameter(p.parameter)) {
            result.addError("unknown-parameter", "Exposed setting '" + p.parameter + "' is unknown.");
        }
        if (!exposedParams.insert(p.parameter).second) {
            result.addError("duplicate-exposed", "'" + p.parameter + "' is exposed twice.");
        }
        if (p.hasRangeOverride && p.minimumOverride > p.maximumOverride) {
            result.addError("exposed-range", "Exposed '" + p.parameter + "' has an inverted range.");
        }
    }

    // Resources.
    for (const auto& r : resources) {
        if (r.width <= 0 || r.height <= 0) {
            result.addError("resource-size", "Resource '" + r.id + "' has no dimensions.");
        } else if (r.pixels.size() != r.expectedBytes()) {
            result.addError("resource-bytes", "Resource '" + r.id + "' holds " +
                                                  formatNumber(double(r.pixels.size())) +
                                                  " bytes, expected " +
                                                  formatNumber(double(r.expectedBytes())) + ".");
        }
        if (resourceUsage(r.id) == 0) {
            result.addWarning("unused-resource",
                              "Resource '" + r.id + "' is stored but no component uses it.");
        }
    }

    return result;
}

// ---- Migration --------------------------------------------------------------

BrushDocument BrushDocument::fromLegacy(const LegacyBrush& legacy, std::string brushId,
                                        std::string brushName) {
    BrushDocument doc(std::move(brushName));
    doc.id = std::move(brushId);
    doc.engine = legacy.realisticEngine ? Engine::Realistic : Engine::Digital;

    // One component: the tip the old record implied. It is procedural -- the
    // engines generate these masks themselves -- so it carries no pixel data,
    // only which of the five masks it uses.
    BrushComponent tip;
    tip.id = "c1";
    tip.name = "Tip";
    tip.kind = ComponentKind::ProceduralShape;
    tip.erases = legacy.erase;
    tip.parameters.set("tip-shape", double(tipShapeForStyle(legacy.tipStyle)));
    doc.addComponent(std::move(tip));

    // One generator: the continuous stroker both engines use today.
    BrushGenerator generator;
    generator.id = "n1";
    generator.kind = GeneratorKind::Continuous;
    doc.addGenerator(std::move(generator));

    // Values, one for one with the old record.
    // A key the registry does not know would drop a value on the floor during
    // migration. That is data loss, so it trips in debug builds instead.
    auto put = [&doc](const char* key, double value) {
        const bool applied = doc.setParameter(key, value);
        assert(applied && "legacy field has no parameter registry entry");
        (void)applied;
    };
    put("size", legacy.size);
    put("opacity", legacy.opacity);
    put("flow", legacy.flow);
    put("spacing", legacy.spacing);
    put("hardness", legacy.hardness);
    put("stabilization", legacy.stabilization);
    put("min-size-ratio", legacy.minSizeRatio);
    put("pressure-gamma", legacy.pressureGamma);
    put("tilt-size", legacy.tiltSize);
    put("aspect", legacy.aspectRatio);
    put("tip-angle", legacy.tipAngle);
    put("color-mix", legacy.colorMix);
    put("blend-strength", legacy.blendStrength);
    put("follow-direction", legacy.followDirection ? 1.0 : 0.0);
    put("follow-rotation", legacy.stylusRotation ? 1.0 : 0.0);
    put("tilt-angle", legacy.tiltAngle ? 1.0 : 0.0);
    put("persistent-reservoir", legacy.persistentReservoir ? 1.0 : 0.0);

    if (legacy.realisticEngine) {
        put("paint-load", legacy.paintLoad);
        put("pigment-concentration", legacy.pigmentConcentration);
        put("water-amount", legacy.waterAmount);
        put("paint-body", legacy.paintBody);
        put("pickup", legacy.pickup);
        put("depletion", legacy.depletion);
        put("edge-accumulation", legacy.edgeAccumulation);
        put("tooth-response", legacy.toothResponse);
        put("granulation", legacy.granulation);
        put("drying-speed", legacy.dryingSpeed);
        put("pressure-amount", legacy.pressureToAmount);
        put("pressure-contact", legacy.pressureToContact);
    }

    // Pressure behaviour becomes bindings rather than three loose scalars, but
    // only where the old record actually expressed an effect.
    if (legacy.pressureSize > 0.0) {
        DynamicsBinding binding;
        binding.parameter = "size";
        binding.signal = Signal::Pressure;
        binding.amount = legacy.pressureSize / 100.0;
        doc.bindDynamics(std::move(binding));
    }
    if (legacy.pressureOpacity < 100.0) {
        DynamicsBinding binding;
        binding.parameter = "opacity";
        binding.signal = Signal::Pressure;
        binding.amount = (100.0 - legacy.pressureOpacity) / 100.0;
        doc.bindDynamics(std::move(binding));
    }
    if (legacy.pressureFlow > 0.0) {
        DynamicsBinding binding;
        binding.parameter = "flow";
        binding.signal = Signal::Pressure;
        binding.amount = legacy.pressureFlow / 100.0;
        doc.bindDynamics(std::move(binding));
    }
    if (legacy.tiltSize > 0.0) {
        DynamicsBinding binding;
        binding.parameter = "size";
        binding.signal = Signal::Tilt;
        binding.amount = legacy.tiltSize / 100.0;
        doc.bindDynamics(std::move(binding));
    }

    // A starting set of exposed settings; the artist edits this in Stage 11.
    const char* defaults[] = {"size", "opacity", "flow", "spacing", "hardness"};
    int order = 0;
    for (const char* key : defaults) {
        ExposedProperty property;
        property.parameter = key;
        property.order = order++;
        doc.expose(std::move(property));
    }

    return doc;
}

LegacyBrush BrushDocument::toLegacy() const {
    LegacyBrush legacy;
    auto take = [this](const char* key, double fallback) { return parameters.value(key, fallback); };

    legacy.size = take("size", 16);
    legacy.opacity = take("opacity", 100);
    legacy.flow = take("flow", 100);
    legacy.spacing = take("spacing", 15);
    legacy.hardness = take("hardness", 100);
    legacy.stabilization = take("stabilization", 0);
    legacy.minSizeRatio = take("min-size-ratio", 15);
    legacy.pressureGamma = take("pressure-gamma", 100);
    legacy.tiltSize = take("tilt-size", 0);
    legacy.aspectRatio = take("aspect", 100);
    legacy.tipAngle = take("tip-angle", 0);
    legacy.colorMix = take("color-mix", 100);
    legacy.blendStrength = take("blend-strength", 0);
    legacy.followDirection = take("follow-direction", 0) != 0.0;
    legacy.stylusRotation = take("follow-rotation", 0) != 0.0;
    legacy.tiltAngle = take("tilt-angle", 0) != 0.0;
    legacy.persistentReservoir = take("persistent-reservoir", 0) != 0.0;
    legacy.realisticEngine = engine == Engine::Realistic;

    legacy.paintLoad = take("paint-load", 88);
    legacy.pigmentConcentration = take("pigment-concentration", 82);
    legacy.waterAmount = take("water-amount", 22);
    legacy.paintBody = take("paint-body", 78);
    legacy.pickup = take("pickup", 16);
    legacy.depletion = take("depletion", 100);
    legacy.edgeAccumulation = take("edge-accumulation", 8);
    legacy.toothResponse = take("tooth-response", 18);
    legacy.granulation = take("granulation", 5);
    legacy.dryingSpeed = take("drying-speed", 115);
    legacy.pressureToAmount = take("pressure-amount", 62);
    legacy.pressureToContact = take("pressure-contact", 35);

    for (const auto& b : dynamics) {
        if (b.parameter == "size" && b.signal == Signal::Pressure) {
            legacy.pressureSize = b.amount * 100.0;
        } else if (b.parameter == "opacity" && b.signal == Signal::Pressure) {
            legacy.pressureOpacity = 100.0 - b.amount * 100.0;
        } else if (b.parameter == "flow" && b.signal == Signal::Pressure) {
            legacy.pressureFlow = b.amount * 100.0;
        } else if (b.parameter == "size" && b.signal == Signal::Tilt) {
            legacy.tiltSize = b.amount * 100.0;
        }
    }

    if (components.size() == 1) {
        const BrushComponent& tip = components.front();
        legacy.erase = tip.erases;
        legacy.tipStyle = tipStyleForShape(tip.parameters.value("tip-shape", 0.0));
    }
    return legacy;
}

} // namespace komapaint::brush
