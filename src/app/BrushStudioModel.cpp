#include "BrushStudioModel.h"

#include "../brush/BrushEvaluator.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

namespace komapaint {
namespace {

using brush::BrushComponent;
using brush::BrushDocument;
using brush::ComponentBlend;
using brush::ComponentKind;
using brush::Engine;
using brush::SelectionMode;

std::size_t documentBytes(const BrushDocument& document) {
    std::size_t bytes = sizeof(BrushDocument);
    bytes += document.components.size() * sizeof(BrushComponent);
    bytes += document.groups.size() * sizeof(brush::ComponentGroup);
    bytes += document.generators.size() * sizeof(brush::BrushGenerator);
    bytes += document.resources.size() * sizeof(brush::BrushResource);
    for (const auto& resource : document.resources) bytes += resource.pixels.size();
    bytes += document.parameters.entries().size() * sizeof(std::pair<std::string, double>);
    bytes += document.exposed.size() * sizeof(brush::ExposedProperty);
    bytes += document.dynamics.size() * sizeof(brush::DynamicsBinding);
    bytes += document.randomization.size() * sizeof(brush::RandomizationRule);
    return bytes;
}

QString unitName(brush::ParameterUnit unit) {
    switch (unit) {
    case brush::ParameterUnit::Percent: return QStringLiteral("%");
    case brush::ParameterUnit::Pixels: return QStringLiteral("px");
    case brush::ParameterUnit::Degrees: return QStringLiteral("deg");
    case brush::ParameterUnit::Ratio: return QStringLiteral("x");
    case brush::ParameterUnit::Unit: break;
    }
    return QString();
}

const char* controlName(brush::ControlType control) {
    switch (control) {
    case brush::ControlType::Number: return "number";
    case brush::ControlType::Toggle: return "toggle";
    case brush::ControlType::Choice: return "choice";
    case brush::ControlType::Curve: return "curve";
    case brush::ControlType::Slider: break;
    }
    return "slider";
}

QString blendName(ComponentBlend blend) {
    return QString::fromUtf8(brush::toString(blend));
}

// Kind names for the UI. A component kind is data, so the list comes from the
// same toString() the file format uses instead of being typed out again here.
const ComponentKind kComponentKinds[] = {
    ComponentKind::Tip,        ComponentKind::Image,       ComponentKind::Texture,
    ComponentKind::AlphaMask,  ComponentKind::GrayscaleMask, ComponentKind::ColourImage,
    ComponentKind::ProceduralShape, ComponentKind::Particle, ComponentKind::BristleGroup,
    ComponentKind::Pattern,    ComponentKind::Sequence,
};

const SelectionMode kSelectionModes[] = {
    SelectionMode::All,      SelectionMode::Random, SelectionMode::Weighted,
    SelectionMode::Sequence, SelectionMode::Alternating, SelectionMode::Combination,
};

int kindIndex(ComponentKind kind) {
    for (int i = 0; i < int(std::size(kComponentKinds)); ++i) {
        if (kComponentKinds[i] == kind) return i;
    }
    return 0;
}

} // namespace

BrushStudioModel::BrushStudioModel(QObject* parent) : QObject(parent) {
    m_document.name = "Untitled brush";
    m_document.id = "studio-brush";
    // A brush you can paint with before you change anything: one procedural tip
    // and one generator, which is what the model's own migration produces.
    BrushComponent tip;
    tip.id = "c1";
    tip.name = "Tip";
    tip.kind = ComponentKind::ProceduralShape;
    tip.parameters.set("tip-shape", 0.0);
    m_document.addComponent(std::move(tip));
    brush::BrushGenerator generator;
    generator.id = "n1";
    generator.kind = brush::GeneratorKind::Continuous;
    m_document.addGenerator(std::move(generator));
    const char* defaults[] = {"size", "opacity", "flow", "spacing", "hardness"};
    int order = 0;
    for (const char* key : defaults) {
        brush::ExposedProperty property;
        property.parameter = key;
        property.order = order++;
        m_document.expose(std::move(property));
    }
    syncSelection();
}

// ---- document ---------------------------------------------------------------

void BrushStudioModel::setDocument(BrushDocument document) {
    pushUndo("Open brush");
    m_document = std::move(document);
    m_modified = true;
    m_selectedIndex = m_document.components.empty() ? -1 : 0;
    emit documentChanged();
    emit componentsChanged();
    emit selectionChanged();
    emit exposureChanged();
    emit historyChanged();
}

bool BrushStudioModel::load(const QString& path) {
    BrushDocument loaded;
    brush::DecodeResult result;
    std::string failure;
    if (!brush::loadBrushFile(QFileInfo(path).absoluteFilePath().toStdString(), &loaded,
                              &result, &failure)) {
        fail(QString::fromStdString(failure.empty() ? result.error : failure));
        return false;
    }
    clearError();
    setDocument(std::move(loaded));
    m_modified = false;
    emit documentChanged();
    return true;
}

bool BrushStudioModel::save(const QString& path) {
    // A file that cannot be read back is worse than no file: the artist loses
    // work and finds out later. The same validation that guards loading runs
    // here first, and its own sentence is what the window shows.
    const brush::ValidationResult valid = m_document.validate();
    if (!valid.ok()) {
        fail(QString::fromStdString("This brush cannot be saved yet: " + valid.summary()));
        return false;
    }
    std::string failure;
    if (!brush::saveBrushFile(QFileInfo(path).absoluteFilePath().toStdString(), m_document,
                              &failure)) {
        fail(QString::fromStdString(failure));
        return false;
    }
    clearError();
    m_modified = false;
    emit documentChanged();
    return true;
}

void BrushStudioModel::setName(const QString& value) {
    if (value.isEmpty() || name() == value) return;
    edit("Rename brush", [this, value] { m_document.name = value.toStdString(); });
}

QString BrushStudioModel::engine() const {
    return QString::fromUtf8(brush::toString(m_document.engine));
}

void BrushStudioModel::setEngine(const QString& value) {
    Engine parsed = m_document.engine;
    if (!brush::parseEngine(value.toStdString(), &parsed) || parsed == m_document.engine) return;
    edit("Change engine", [this, parsed] { m_document.engine = parsed; });
}

void BrushStudioModel::setDescription(const QString& value) {
    if (description() == value) return;
    edit("Describe brush", [this, value] { m_document.description = value.toStdString(); });
}

// ---- components -------------------------------------------------------------

QVariantList BrushStudioModel::components() const {
    QVariantList list;
    list.reserve(int(m_document.components.size()));
    for (std::size_t i = 0; i < m_document.components.size(); ++i) {
        const BrushComponent& component = m_document.components[i];
        QVariantMap map;
        map.insert(QStringLiteral("index"), int(i));
        map.insert(QStringLiteral("id"), QString::fromStdString(component.id));
        map.insert(QStringLiteral("name"), QString::fromStdString(component.name));
        map.insert(QStringLiteral("kind"), kindIndex(component.kind));
        map.insert(QStringLiteral("kindName"), QString::fromUtf8(brush::toString(component.kind)));
        map.insert(QStringLiteral("visible"), component.visible);
        map.insert(QStringLiteral("opacity"), component.opacity);
        map.insert(QStringLiteral("blend"), int(component.blend));
        map.insert(QStringLiteral("blendName"), blendName(component.blend));
        map.insert(QStringLiteral("erases"), component.erases);
        map.insert(QStringLiteral("weight"), component.weight);
        map.insert(QStringLiteral("groupId"), QString::fromStdString(component.groupId));
        map.insert(QStringLiteral("resourceId"), QString::fromStdString(component.resourceId));
        map.insert(QStringLiteral("maskId"), QString::fromStdString(component.maskComponentId));
        map.insert(QStringLiteral("offsetX"), component.transform.offsetX);
        map.insert(QStringLiteral("offsetY"), component.transform.offsetY);
        map.insert(QStringLiteral("rotation"), component.transform.rotationDegrees);
        map.insert(QStringLiteral("scale"), component.transform.scale);
        map.insert(QStringLiteral("aspect"), component.transform.aspect);
        map.insert(QStringLiteral("flipX"), component.transform.flipX);
        map.insert(QStringLiteral("flipY"), component.transform.flipY);
        map.insert(QStringLiteral("selected"), int(i) == m_selectedIndex);
        list.append(map);
    }
    return list;
}

QVariantList BrushStudioModel::groups() const {
    QVariantList list;
    list.reserve(int(m_document.groups.size()));
    for (const auto& group : m_document.groups) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), QString::fromStdString(group.id));
        map.insert(QStringLiteral("name"), QString::fromStdString(group.name));
        map.insert(QStringLiteral("mode"), int(group.mode));
        map.insert(QStringLiteral("modeName"), QString::fromUtf8(brush::toString(group.mode)));
        map.insert(QStringLiteral("membersPerStamp"), group.membersPerStamp);
        map.insert(QStringLiteral("memberCount"), int(m_document.groupMembers(group.id).size()));
        map.insert(QStringLiteral("parentId"), QString::fromStdString(group.parentGroupId));
        list.append(map);
    }
    return list;
}

QVariantList BrushStudioModel::componentKinds() const {
    QVariantList list;
    for (ComponentKind kind : kComponentKinds) {
        QVariantMap map;
        map.insert(QStringLiteral("value"), kindIndex(kind));
        map.insert(QStringLiteral("name"), QString::fromUtf8(brush::toString(kind)));
        list.append(map);
    }
    return list;
}

QVariantList BrushStudioModel::selectionModes() const {
    QVariantList list;
    for (SelectionMode mode : kSelectionModes) {
        QVariantMap map;
        map.insert(QStringLiteral("value"), int(mode));
        map.insert(QStringLiteral("name"), QString::fromUtf8(brush::toString(mode)));
        list.append(map);
    }
    return list;
}

QString BrushStudioModel::selectedId() const {
    if (m_selectedIndex < 0 ||
        std::size_t(m_selectedIndex) >= m_document.components.size()) {
        return QString();
    }
    return QString::fromStdString(m_document.components[std::size_t(m_selectedIndex)].id);
}

int BrushStudioModel::selectedIndex() const { return m_selectedIndex; }

void BrushStudioModel::setSelectedIndex(int index) {
    const int bounded = (index >= 0 && std::size_t(index) < m_document.components.size())
        ? index : -1;
    if (bounded == m_selectedIndex) return;
    m_selectedIndex = bounded;
    emit selectionChanged();
    emit componentsChanged();
}

namespace {

// Kinds that paint pixels need pixels. A row that exists but cannot be saved is
// a trap, so the Studio starts them from a soft round mask the artist replaces.
bool kindNeedsPixels(ComponentKind kind) {
    switch (kind) {
    case ComponentKind::ProceduralShape:
    case ComponentKind::Particle:
    case ComponentKind::BristleGroup:
        return false;
    default:
        return true;
    }
}

brush::BrushResource makeDotResource(const QString& name) {
    brush::BrushResource resource;
    resource.name = name.toStdString();
    resource.kind = brush::ResourceKind::Grayscale8;
    resource.width = 32;
    resource.height = 32;
    resource.pixels.resize(32u * 32u);
    const double centre = 15.5;
    const double radius = 15.5;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const double distance = std::hypot(double(x) - centre, double(y) - centre);
            const double coverage = std::clamp(1.0 - distance / radius, 0.0, 1.0);
            resource.pixels[std::size_t(y) * 32u + std::size_t(x)] =
                static_cast<std::uint8_t>(std::lround(coverage * 255.0));
        }
    }
    return resource;
}

} // namespace

QString BrushStudioModel::addComponent(int kind) {
    if (kind < 0 || kind >= int(std::size(kComponentKinds))) return QString();
    const ComponentKind componentKind = kComponentKinds[kind];
    QString id;
    edit("Add component", [this, componentKind, &id] {
        BrushComponent component;
        component.id = m_document.allocateId("c");
        component.kind = componentKind;
        component.name = componentKind == ComponentKind::ProceduralShape
            ? "Tip " + component.id : "Component " + component.id;
        if (componentKind == ComponentKind::ProceduralShape) {
            component.parameters.set("tip-shape", 0.0);
        }
        if (kindNeedsPixels(componentKind)) {
            brush::BrushResource resource = makeDotResource(QStringLiteral("Dot"));
            resource.id = m_document.allocateId("r");
            component.resourceId = resource.id;
            m_document.addResource(std::move(resource));
        }
        id = QString::fromStdString(component.id);
        m_document.addComponent(std::move(component));
        m_selectedIndex = int(m_document.components.size()) - 1;
    });
    emit selectionChanged();
    return id;
}

QString BrushStudioModel::duplicateComponent(const QString& id) {
    QString created;
    const std::string source = id.toStdString();
    if (!m_document.component(source)) {
        fail(QStringLiteral("There is no component with that id."));
        return QString();
    }
    edit("Duplicate component", [this, source, &created] {
        std::string newId;
        if (m_document.duplicateComponent(source, &newId)) created = QString::fromStdString(newId);
    });
    if (created.isEmpty()) return QString();
    setSelectedIndex(m_document.componentIndex(created.toStdString()));
    return created;
}

bool BrushStudioModel::removeComponent(const QString& id) {
    const std::string target = id.toStdString();
    if (!m_document.component(target)) {
        fail(QStringLiteral("There is no component with that id."));
        return false;
    }
    edit("Delete component", [this, target] {
        m_document.removeComponent(target);
        if (m_selectedIndex >= int(m_document.components.size())) {
            m_selectedIndex = int(m_document.components.size()) - 1;
        }
    });
    emit selectionChanged();
    return true;
}

bool BrushStudioModel::moveComponent(const QString& id, int newIndex) {
    const std::string target = id.toStdString();
    if (!m_document.component(target)) {
        fail(QStringLiteral("There is no component with that id."));
        return false;
    }
    if (newIndex < 0 || newIndex >= int(m_document.components.size())) return false;
    edit("Reorder components", [this, target, newIndex] {
        m_document.moveComponent(target, newIndex);
        m_selectedIndex = m_document.componentIndex(target);
    });
    emit selectionChanged();
    return true;
}

bool BrushStudioModel::setComponentName(const QString& id, const QString& value) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component) return false;
    const QString current = QString::fromStdString(component->name);
    if (current == value || value.isEmpty()) return false;
    edit("Rename component", [component, value] { component->name = value.toStdString(); });
    return true;
}

bool BrushStudioModel::setComponentVisible(const QString& id, bool visible) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component || component->visible == visible) return false;
    edit("Toggle component", [component, visible] { component->visible = visible; });
    return true;
}

bool BrushStudioModel::setComponentOpacity(const QString& id, double opacity) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component) return false;
    const double clamped = std::clamp(opacity, 0.0, 1.0);
    if (component->opacity == clamped) return false;
    edit("Component opacity", [component, clamped] { component->opacity = clamped; });
    return true;
}

bool BrushStudioModel::setComponentBlend(const QString& id, int blend) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component || blend < 0 || blend > int(ComponentBlend::Mask)) return false;
    const auto value = static_cast<ComponentBlend>(blend);
    if (component->blend == value) return false;
    edit("Component blend", [component, value] { component->blend = value; });
    return true;
}

bool BrushStudioModel::setComponentErases(const QString& id, bool erases) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component || component->erases == erases) return false;
    edit("Component eraser", [component, erases] { component->erases = erases; });
    return true;
}

bool BrushStudioModel::setComponentTransform(const QString& id, const QString& key, double value) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component) return false;
    brush::ComponentTransform& transform = component->transform;
    double* field = nullptr;
    if (key == QStringLiteral("offsetX")) field = &transform.offsetX;
    else if (key == QStringLiteral("offsetY")) field = &transform.offsetY;
    else if (key == QStringLiteral("rotation")) field = &transform.rotationDegrees;
    else if (key == QStringLiteral("scale")) field = &transform.scale;
    else if (key == QStringLiteral("aspect")) field = &transform.aspect;
    if (!field || *field == value) return false;
    edit("Component transform", [field, value] { *field = value; });
    return true;
}

bool BrushStudioModel::setComponentWeight(const QString& id, double weight) {
    BrushComponent* component = m_document.component(id.toStdString());
    if (!component) return false;
    const double clamped = std::max(0.0, weight);
    if (component->weight == clamped) return false;
    edit("Component weight", [component, clamped] { component->weight = clamped; });
    return true;
}

QString BrushStudioModel::createGroup(const QString& name) {
    QString id;
    edit("Add group", [this, name, &id] {
        brush::ComponentGroup group;
        group.id = m_document.allocateId("g");
        group.name = name.isEmpty() ? "Group " + group.id : name.toStdString();
        group.mode = SelectionMode::All;
        id = QString::fromStdString(group.id);
        m_document.addGroup(std::move(group));
    });
    return id;
}

bool BrushStudioModel::assignToGroup(const QString& componentId, const QString& groupId) {
    const std::string id = componentId.toStdString();
    const std::string destination = groupId.toStdString();
    const BrushComponent* component = m_document.component(id);
    if (!component) {
        fail(QStringLiteral("There is no component with that id."));
        return false;
    }
    if (!destination.empty() && !m_document.group(destination)) {
        fail(QStringLiteral("There is no group with that id."));
        return false;
    }
    if (component->groupId == destination) return false;
    edit("Move component to group", [&] { m_document.setComponentGroup(id, destination); });
    clearError();
    return true;
}

bool BrushStudioModel::setGroupMode(const QString& groupId, int mode) {
    brush::ComponentGroup* group = m_document.group(groupId.toStdString());
    if (!group || mode < 0 || mode > int(SelectionMode::Combination)) return false;
    const auto value = static_cast<SelectionMode>(mode);
    if (group->mode == value) return false;
    edit("Group selection mode", [group, value] { group->mode = value; });
    return true;
}

bool BrushStudioModel::setGroupMembersPerStamp(const QString& groupId, int count) {
    brush::ComponentGroup* group = m_document.group(groupId.toStdString());
    if (!group) return false;
    const int bounded = std::max(1, count);
    if (group->membersPerStamp == bounded) return false;
    edit("Members per stamp", [group, bounded] { group->membersPerStamp = bounded; });
    return true;
}

bool BrushStudioModel::removeGroup(const QString& groupId) {
    const std::string id = groupId.toStdString();
    if (!m_document.group(id)) {
        fail(QStringLiteral("There is no group with that id."));
        return false;
    }
    if (m_document.groupHasGenerator(id)) {
        fail(QStringLiteral("Move or remove this group's generators before deleting it."));
        return false;
    }
    edit("Delete group", [&] { m_document.removeGroup(id); });
    clearError();
    return true;
}

// ---- settings ---------------------------------------------------------------

QVariantList BrushStudioModel::parameters() const {
    QVariantList list;
    for (const brush::ParameterInfo& info : brush::parameterRegistry()) {
        QVariantMap map;
        map.insert(QStringLiteral("key"), QString::fromUtf8(info.name));
        map.insert(QStringLiteral("label"), QString::fromUtf8(info.label));
        map.insert(QStringLiteral("description"), QString::fromUtf8(info.description));
        map.insert(QStringLiteral("section"), QString::fromUtf8(
            brush::parameterSection(info.name) ? brush::parameterSection(info.name) : "Advanced"));
        map.insert(QStringLiteral("scope"), int(info.scope));
        map.insert(QStringLiteral("unit"), unitName(info.unit));
        map.insert(QStringLiteral("min"), info.minimum);
        map.insert(QStringLiteral("max"), info.maximum);
        map.insert(QStringLiteral("default"), info.defaultValue);
        map.insert(QStringLiteral("value"), brush::BrushEvaluator::baseParameter(m_document, info.name));
        map.insert(QStringLiteral("exposed"), isExposed(QString::fromUtf8(info.name)));
        list.append(map);
    }
    return list;
}

double BrushStudioModel::parameterValue(const QString& key) const {
    return brush::BrushEvaluator::baseParameter(m_document, key.toStdString());
}

bool BrushStudioModel::setParameterValue(const QString& key, double value) {
    const brush::ParameterInfo* info = brush::findParameter(key.toStdString());
    if (!info) {
        fail(QStringLiteral("There is no brush setting called '%1'.").arg(key));
        return false;
    }
    if (!brush::parameterInRange(*info, value)) {
        fail(QStringLiteral("'%1' must be between %2 and %3.")
                 .arg(key)
                 .arg(info->minimum)
                 .arg(info->maximum));
        return false;
    }
    if (m_document.parameters.value(info->name, info->defaultValue) == value) return true;
    edit("Change setting", [this, info, value] {
        m_document.setParameter(info->name, value);
    });
    return true;
}

QVariantMap BrushStudioModel::parameterInfo(const QString& key) const {
    QVariantMap map;
    const brush::ParameterInfo* info = brush::findParameter(key.toStdString());
    if (!info) return map;
    map.insert(QStringLiteral("key"), QString::fromUtf8(info->name));
    map.insert(QStringLiteral("label"), QString::fromUtf8(info->label));
    map.insert(QStringLiteral("description"), QString::fromUtf8(info->description));
    map.insert(QStringLiteral("section"),
               QString::fromUtf8(brush::parameterSection(info->name)));
    map.insert(QStringLiteral("unit"), unitName(info->unit));
    map.insert(QStringLiteral("min"), info->minimum);
    map.insert(QStringLiteral("max"), info->maximum);
    map.insert(QStringLiteral("default"), info->defaultValue);
    map.insert(QStringLiteral("value"), brush::BrushEvaluator::baseParameter(m_document, info->name));
    return map;
}

// ---- exposure ---------------------------------------------------------------

QVariantList BrushStudioModel::exposedProperties() const {
    QVariantList list;
    for (const auto* property : m_document.visibleExposed()) {
        const brush::ParameterInfo* info = brush::findParameter(property->parameter);
        QVariantMap map;
        map.insert(QStringLiteral("key"), QString::fromStdString(property->parameter));
        map.insert(QStringLiteral("label"), QString::fromStdString(
            property->label.empty() && info ? std::string(info->label) : property->label));
        map.insert(QStringLiteral("customLabel"), !property->label.empty());
        map.insert(QStringLiteral("section"),
                   QString::fromStdString(brush::effectiveSection(*property)));
        map.insert(QStringLiteral("order"), property->order);
        map.insert(QStringLiteral("control"), QString::fromUtf8(controlName(property->control)));
        map.insert(QStringLiteral("hasRange"), property->hasRangeOverride);
        map.insert(QStringLiteral("minimum"), property->hasRangeOverride && info
                                                   ? property->minimumOverride : info ? info->minimum : 0.0);
        map.insert(QStringLiteral("maximum"), property->hasRangeOverride && info
                                                   ? property->maximumOverride : info ? info->maximum : 1.0);
        list.append(map);
    }
    return list;
}

bool BrushStudioModel::isExposed(const QString& key) const {
    for (const auto& property : m_document.exposed) {
        if (property.parameter == key.toStdString()) return property.visible;
    }
    return false;
}

bool BrushStudioModel::expose(const QString& key) {
    const brush::ParameterInfo* info = brush::findParameter(key.toStdString());
    if (!info) {
        fail(QStringLiteral("There is no brush setting called '%1'.").arg(key));
        return false;
    }
    if (isExposed(key)) return true;
    edit("Expose setting", [this, info] {
        brush::ExposedProperty property;
        property.parameter = info->name;
        property.order = int(m_document.exposed.size());
        m_document.expose(std::move(property));
    });
    return true;
}

bool BrushStudioModel::unexpose(const QString& key) {
    if (!isExposed(key)) return false;
    edit("Hide setting", [this, key] { m_document.unexpose(key.toStdString()); });
    return true;
}

bool BrushStudioModel::setExposure(const QString& key, const QVariantMap& fields) {
    bool found = false;
    for (auto& property : m_document.exposed) {
        if (property.parameter != key.toStdString()) continue;
        found = true;
        if (fields.contains(QStringLiteral("label"))) {
            property.label = fields.value(QStringLiteral("label")).toString().toStdString();
        }
        if (fields.contains(QStringLiteral("section"))) {
            property.section = fields.value(QStringLiteral("section")).toString().toStdString();
        }
        if (fields.contains(QStringLiteral("order"))) {
            property.order = fields.value(QStringLiteral("order")).toInt();
        }
        if (fields.contains(QStringLiteral("control"))) {
            brush::ControlType control = property.control;
            if (brush::parseControlType(
                    fields.value(QStringLiteral("control")).toString().toStdString(), &control)) {
                property.control = control;
            }
        }
        if (fields.contains(QStringLiteral("minimum")) ||
            fields.contains(QStringLiteral("maximum"))) {
            property.hasRangeOverride = true;
            property.minimumOverride = fields.value(QStringLiteral("minimum"),
                                                    property.minimumOverride).toDouble();
            property.maximumOverride = fields.value(QStringLiteral("maximum"),
                                                    property.maximumOverride).toDouble();
        }
        if (fields.contains(QStringLiteral("hasRange"))) {
            property.hasRangeOverride = fields.value(QStringLiteral("hasRange")).toBool();
        }
        break;
    }
    if (!found) {
        fail(QStringLiteral("'%1' is not exposed, so it has nothing to rename.").arg(key));
        return false;
    }
    m_modified = true;
    emit exposureChanged();
    emit documentChanged();
    return true;
}

bool BrushStudioModel::moveExposure(const QString& key, int delta) {
    if (delta == 0) return false;
    std::vector<brush::ExposedProperty*> ordered;
    ordered.reserve(m_document.exposed.size());
    for (auto& property : m_document.exposed) ordered.push_back(&property);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* a, const auto* b) { return brush::exposedLess(*a, *b); });

    std::size_t index = ordered.size();
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (ordered[i]->parameter == key.toStdString()) { index = i; break; }
    }
    if (index == ordered.size()) return false;
    // Moving across a group boundary would be undone by the sort, so a move
    // stops at the edge of the property's own group rather than silently doing
    // nothing.
    const std::string section = brush::effectiveSection(*ordered[index]);
    long target = long(index) + delta;
    while (target >= 0 && target < long(ordered.size()) &&
           brush::effectiveSection(*ordered[std::size_t(target)]) != section) {
        target += delta > 0 ? -1 : 1;
    }
    if (target < 0 || target >= long(ordered.size()) || target == long(index)) return false;

    if (target > long(index)) {
        std::rotate(ordered.begin() + long(index), ordered.begin() + long(index) + 1,
                    ordered.begin() + target + 1);
    } else {
        std::rotate(ordered.begin() + target, ordered.begin() + long(index),
                    ordered.begin() + long(index) + 1);
    }
    for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i]->order = int(i);
    m_modified = true;
    emit exposureChanged();
    emit documentChanged();
    return true;
}

void BrushStudioModel::resetExposure() {
    edit("Reset Sub-Tool settings", [this] {
        m_document.exposed.clear();
        const char* defaults[] = {"size", "opacity", "flow", "spacing", "hardness"};
        int order = 0;
        for (const char* key : defaults) {
            brush::ExposedProperty property;
            property.parameter = key;
            property.order = order++;
            m_document.expose(std::move(property));
        }
    });
}

// ---- history ----------------------------------------------------------------

void BrushStudioModel::pushUndo(const char* label) {
    Step step;
    step.document = m_document;
    step.selectedIndex = m_selectedIndex;
    step.label = QString::fromUtf8(label);
    step.bytes = documentBytes(m_document);
    m_undo.push_back(std::move(step));
    m_historyBytes += m_undo.back().bytes;
    m_redo.clear();
    // Memory, not an arbitrary step count: the oldest steps go first, which is
    // what an artist expects and what the undo stack in the editor already does.
    while (m_historyBytes > HistoryBudget && m_undo.size() > 1) {
        m_historyBytes -= m_undo.front().bytes;
        m_undo.erase(m_undo.begin());
    }
}

void BrushStudioModel::undo() {
    if (m_undo.empty()) return;
    Step step = std::move(m_undo.back());
    m_undo.pop_back();
    m_historyBytes -= std::min(m_historyBytes, step.bytes);
    Step forward;
    forward.document = m_document;
    forward.selectedIndex = m_selectedIndex;
    forward.bytes = documentBytes(m_document);
    m_redo.push_back(std::move(forward));
    m_document = std::move(step.document);
    m_selectedIndex = step.selectedIndex;
    m_modified = true;
    emit documentChanged();
    emit componentsChanged();
    emit selectionChanged();
    emit exposureChanged();
    emit historyChanged();
}

void BrushStudioModel::redo() {
    if (m_redo.empty()) return;
    Step step = std::move(m_redo.back());
    m_redo.pop_back();
    Step back;
    back.document = m_document;
    back.selectedIndex = m_selectedIndex;
    back.bytes = documentBytes(m_document);
    m_undo.push_back(std::move(back));
    m_historyBytes += m_undo.back().bytes;
    m_document = std::move(step.document);
    m_selectedIndex = step.selectedIndex;
    m_modified = true;
    emit documentChanged();
    emit componentsChanged();
    emit selectionChanged();
    emit exposureChanged();
    emit historyChanged();
}

void BrushStudioModel::beginStep(const QString& label) {
    if (m_inStep) return;
    pushUndo(label.toUtf8().constData());
    m_inStep = true;
    emit historyChanged();
}

void BrushStudioModel::commitStep() {
    if (!m_inStep) return;
    m_inStep = false;
    m_modified = true;
    emit documentChanged();
    emit componentsChanged();
    emit historyChanged();
}

void BrushStudioModel::setModified(bool modified) {
    if (m_modified == modified) return;
    m_modified = modified;
    emit documentChanged();
}

void BrushStudioModel::fail(const QString& message) {
    m_error = message;
    emit errorChanged();
}

void BrushStudioModel::clearError() {
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorChanged();
}

void BrushStudioModel::syncSelection() {
    m_selectedIndex = m_document.components.empty() ? -1 : 0;
    emit selectionChanged();
}

QString BrushStudioModel::validationSummary() const {
    const brush::ValidationResult result = m_document.validate();
    return QString::fromStdString(result.summary());
}

} // namespace komapaint
