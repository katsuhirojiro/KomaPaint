#include "BrushStudioModel.h"

#include "../brush/BrushEvaluator.h"
#include "../canvas/MemoryAvailability.h"
#include "drawing/DigitalBrushCapabilities.h"

#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
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

QString unitName(brush::ParameterUnit unit) {
    switch (unit) {
    case brush::ParameterUnit::Percent: return QStringLiteral("%");
    case brush::ParameterUnit::Pixels: return QStringLiteral("px");
    case brush::ParameterUnit::Degrees: return QStringLiteral("\u00b0");
    case brush::ParameterUnit::Ratio: return QStringLiteral("%"); // 100 is 1:1 in the registry
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

// There is one source of truth for each setting. Registry percentages are
// views of the component's fractions and scale factors, not another bag of
// values that the evaluator might never read.
struct TransformSetting {
    const char* key;
    const char* parameter;
    double brush::ComponentTransform::* field;
    double units;
};
constexpr TransformSetting kTransformSettings[] = {
    {"offsetX", "component-offset-x", &brush::ComponentTransform::offsetX, 1.0},
    {"offsetY", "component-offset-y", &brush::ComponentTransform::offsetY, 1.0},
    {"rotation", "component-rotation", &brush::ComponentTransform::rotationDegrees, 1.0},
    {"scale", "component-scale", &brush::ComponentTransform::scale, 100.0},
    {"aspect", "component-aspect", &brush::ComponentTransform::aspect, 100.0},
};

bool hasPositiveWeight(const BrushDocument& doc, const brush::ComponentGroup& group,
                       std::string_view except = {}) {
    return std::ranges::any_of(group.members, [&](const std::string& id) {
        const auto* component = doc.component(id);
        return component && id != except && component->weight > 0.0;
    });
}

int kindIndex(ComponentKind kind) {
    for (int i = 0; i < int(std::size(kComponentKinds)); ++i) {
        if (kComponentKinds[i] == kind) return i;
    }
    return 0;
}

QString componentKindLabel(ComponentKind kind) {
    switch (kind) {
    case ComponentKind::Tip:             return BrushStudioModel::tr("Tip mask");
    case ComponentKind::Image:           return BrushStudioModel::tr("Image");
    case ComponentKind::Texture:         return BrushStudioModel::tr("Texture");
    case ComponentKind::AlphaMask:       return BrushStudioModel::tr("Alpha mask");
    case ComponentKind::GrayscaleMask:  return BrushStudioModel::tr("Grayscale mask");
    case ComponentKind::ColourImage:     return BrushStudioModel::tr("Colour image");
    case ComponentKind::ProceduralShape: return BrushStudioModel::tr("Procedural tip");
    case ComponentKind::Particle:        return BrushStudioModel::tr("Particle");
    case ComponentKind::BristleGroup:    return BrushStudioModel::tr("Bristle group");
    case ComponentKind::Pattern:         return BrushStudioModel::tr("Pattern");
    case ComponentKind::Sequence:        return BrushStudioModel::tr("Sequence");
    }
    return {};
}

bool isTexture(ComponentKind kind) {
    return kind == ComponentKind::Texture || kind == ComponentKind::Pattern;
}

std::string newDraftId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

BrushDocument defaultDraft() {
    BrushDocument document;
    document.name = "Untitled brush";
    document.id = newDraftId();
    // A brush you can paint with before you change anything: one procedural tip
    // and one generator, which is what the model's own migration produces.
    BrushComponent tip;
    tip.id = "c1";
    tip.name = "Tip";
    tip.kind = ComponentKind::ProceduralShape;
    tip.parameters.set("tip-shape", 0.0);
    document.addComponent(std::move(tip));
    brush::BrushGenerator generator;
    generator.id = "n1";
    generator.kind = brush::GeneratorKind::Continuous;
    document.addGenerator(std::move(generator));
    // Marker-friendly common controls: the five essentials plus the two tip
    // angles that make a flat nib behave like a marker. These are exposure
    // declarations only; no brush value is changed.
    int order = 0;
    for (const std::string_view key : drawing::recommendedDigitalControls()) {
        brush::ExposedProperty property;
        property.parameter = std::string(key);
        property.order = order++;
        document.expose(std::move(property));
    }
    return document;
}

} // namespace

BrushStudioModel::BrushStudioModel(QObject* parent)
    : BrushStudioModel(parent, [](void*, std::size_t bytes) noexcept { return systemMemoryAvailable(bytes); }, nullptr) {}

BrushStudioModel::BrushStudioModel(QObject* parent, MemoryCheck check, void* context)
    : QObject(parent), m_document(defaultDraft()), m_selectedIndex(0), m_memoryCheck(check), m_memoryContext(context) {}

// ---- document ---------------------------------------------------------------

bool BrushStudioModel::setDocument(const BrushDocument& document) {
    return edit("Open brush", [&] {
        m_document = document;
        m_selectedIndex = m_document.components.empty() ? -1 : 0;
    }, EditMode::Replace, &document);
}

bool BrushStudioModel::setDocument(BrushDocument&& document) {
    return replaceDocument(std::move(document), EditMode::Replace);
}

bool BrushStudioModel::replaceDocument(BrushDocument&& document, EditMode mode, const char* label) {
    return edit(label, [&] {
        m_document = std::move(document);
        m_selectedIndex = m_document.components.empty() ? -1 : 0;
    }, mode);
}

bool BrushStudioModel::load(const QString& path) {
    BrushDocument loaded;
    brush::DecodeResult result;
    std::string failure;
    if (!brush::loadBrushFile(QFileInfo(path).filesystemAbsoluteFilePath(), &loaded,
                              &result, &failure)) {
        fail(QString::fromStdString(failure.empty() ? result.error : failure));
        return false;
    }
    return replaceDocument(std::move(loaded), EditMode::LoadSaved);
}

bool BrushStudioModel::writeBrush(const QString& path) {
    std::string failure;
    if (!brush::saveBrushFile(QFileInfo(path).filesystemAbsoluteFilePath(), m_document, &failure)) {
        return fail(QString::fromStdString(failure));
    }
    return true;
}

bool BrushStudioModel::save(const QString& path) {
    if (!writeBrush(path)) return false;
    commitStep();
    markSaved();
    clearError();
    emit documentChanged();
    return true;
}

bool BrushStudioModel::newDraft() try {
    return replaceDocument(defaultDraft(), EditMode::Replace, "New brush");
} catch (const std::bad_alloc&) { return historyMemoryError(); }

bool BrushStudioModel::duplicateDraft() {
    // Reuse the checkpoint made by edit(); pixel resources remain shared.
    return edit("Duplicate brush", [this] {
        m_document.id = newDraftId();
        m_document.name = tr("%1 copy").arg(name()).toStdString();
    }, EditMode::Replace);
}

bool BrushStudioModel::openDraft(const QUrl& url) {
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) return fail(tr("Choose a local .kb brush file to open."));
    return load(url.toLocalFile());
}

bool BrushStudioModel::saveDraft(const QUrl& url) {
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) return fail(tr("Choose a local .kb brush file to save."));
    return save(url.toLocalFile());
}

bool BrushStudioModel::exportDraft(const QUrl& url) {
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) return fail(tr("Choose a local .kb destination for the copy."));
    if (!writeBrush(url.toLocalFile())) return false;
    clearError();
    return true;
}

void BrushStudioModel::setName(const QString& value) {
    if (value.trimmed().isEmpty()) {
        fail(tr("A brush needs a name."));
        return;
    }
    if (name() == value) return;
    edit("Rename brush", [this, &value] { m_document.name = value.toStdString(); });
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

QVariantMap BrushStudioModel::componentRow(int index) const {
    if (index < 0 || index >= componentCount()) return {};
    const auto& component = m_document.components[static_cast<std::size_t>(index)];
    QVariantMap map;
    map.insert(QStringLiteral("index"), index);
    map.insert(QStringLiteral("id"), QString::fromStdString(component.id));
    map.insert(QStringLiteral("name"), QString::fromStdString(component.name));
    map.insert(QStringLiteral("kind"), kindIndex(component.kind));
    map.insert(QStringLiteral("kindName"), QString::fromUtf8(brush::toString(component.kind)));
    map.insert(QStringLiteral("kindLabel"), componentKindLabel(component.kind));
    map.insert(QStringLiteral("texture"), isTexture(component.kind));
    const auto* resource = m_document.resource(component.resourceId);
    map.insert(QStringLiteral("resourceWidth"), resource ? resource->width : 0);
    map.insert(QStringLiteral("resourceHeight"), resource ? resource->height : 0);
    map.insert(QStringLiteral("resourceFormat"), resource ? QString::fromUtf8(brush::toString(resource->kind)) : QString{});
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
    map.insert(QStringLiteral("selected"), index == m_selectedIndex);
    map.insert(QStringLiteral("supported"), drawing::digitalSupportsComponentKind(component.kind));
    map.insert(QStringLiteral("supportReason"), QString::fromStdString(
        drawing::digitalComponentReason(m_document, component.id)));
    map.insert(QStringLiteral("blendSupported"), drawing::digitalSupportsBlend(component.blend));
    map.insert(QStringLiteral("blendReason"), QString::fromStdString(
        drawing::digitalBlendReason(component.blend)));
    return map;
}

QVariantList BrushStudioModel::components() const {
    QVariantList list;
    list.reserve(componentCount());
    for (int index = 0; index < componentCount(); ++index) list.append(componentRow(index));
    return list;
}

QVariantMap BrushStudioModel::selectedComponent() const { return componentRow(m_selectedIndex); }

const BrushComponent* BrushStudioModel::selectedComponentData() const {
    return m_selectedIndex >= 0 && m_selectedIndex < componentCount()
        ? &m_document.components[static_cast<std::size_t>(m_selectedIndex)] : nullptr;
}

const brush::BrushResource* BrushStudioModel::selectedResource() const {
    const auto* selected = selectedComponentData();
    return selected ? m_document.resource(selected->resourceId) : nullptr;
}

int BrushStudioModel::hiddenComponentCount() const {
    return static_cast<int>(std::ranges::count_if(m_document.components, [](const auto& item) { return !item.visible; }));
}

bool BrushStudioModel::selectComponentInMode(const QString& mode) {
    if (mode != QStringLiteral("tip") && mode != QStringLiteral("texture")) return false;
    const bool texture = mode == QStringLiteral("texture");
    if (const auto* selected = selectedComponentData(); selected && isTexture(selected->kind) == texture) return true;
    for (int index = 0; index < componentCount(); ++index) {
        if (isTexture(m_document.components[static_cast<std::size_t>(index)].kind) == texture) {
            setSelectedIndex(index);
            return true;
        }
    }
    return false;
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
        // Offer only what the production Digital renderer can paint. Existing
        // unsupported components remain in the document and are preserved.
        if (!drawing::digitalSupportsComponentKind(kind)) continue;
        QVariantMap map;
        map.insert(QStringLiteral("value"), kindIndex(kind));
        map.insert(QStringLiteral("name"), QString::fromUtf8(brush::toString(kind)));
        map.insert(QStringLiteral("label"), componentKindLabel(kind));
        list.append(map);
    }
    return list;
}

QVariantList BrushStudioModel::componentBlends() const {
    QVariantList list;
    // Only blends the native renderer consumes are offered. An existing
    // unsupported blend is retained in the document but is not a choice.
    for (int raw = 0; raw <= int(ComponentBlend::Mask); ++raw) {
        const auto blend = static_cast<ComponentBlend>(raw);
        if (!drawing::digitalSupportsBlend(blend)) continue;
        QString label = blendName(blend);
        if (!label.isEmpty()) label[0] = label[0].toUpper();
        list.append(QVariantMap{{QStringLiteral("value"), int(blend)}, {QStringLiteral("label"), label}});
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

bool isColourStamp(ComponentKind kind) {
    return kind == ComponentKind::Image || kind == ComponentKind::ColourImage;
}

brush::BrushResource makeDotResource(const QString& name, bool colour) {
    brush::BrushResource resource;
    resource.name = name.toStdString();
    resource.kind = colour ? brush::ResourceKind::Rgba8 : brush::ResourceKind::Grayscale8;
    resource.width = 32;
    resource.height = 32;
    std::vector<std::uint8_t> pixels(resource.expectedBytes());
    const double centre = 15.5;
    const double radius = 15.5;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const double distance = std::hypot(double(x) - centre, double(y) - centre);
            const double coverage = std::clamp(1.0 - distance / radius, 0.0, 1.0);
            const auto index = std::size_t(y) * 32u + std::size_t(x);
            pixels[colour ? index * 4 + 3 : index] =
                static_cast<std::uint8_t>(std::lround(coverage * 255.0));
        }
    }
    resource.setPixels(std::move(pixels));
    return resource;
}

} // namespace

bool BrushStudioModel::canImportTipImage() const {
    if (m_selectedIndex < 0 || std::size_t(m_selectedIndex) >= m_document.components.size()) return false;
    const auto kind = m_document.components[std::size_t(m_selectedIndex)].kind;
    return kind == ComponentKind::Tip || isColourStamp(kind);
}

bool BrushStudioModel::importTipImage(const QUrl& url) try {
    if (!canImportTipImage()) return fail(tr("Select a Bitmap Tip or an Image component first."));
    if (!url.isLocalFile() || url.toLocalFile().isEmpty()) return fail(tr("Choose a local image file."));
    const QString path = url.toLocalFile();
    const auto index = std::size_t(m_selectedIndex);
    const bool colour = isColourStamp(m_document.components[index].kind);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || size.isEmpty()) return fail(tr("Could not read image size: %1").arg(reader.errorString()));
    // Allow space for the decoder, 8-bit conversion and immutable resource.
    // The decoder can still fail explicitly on this system; never downsample.
    const auto pixels = std::uint64_t(size.width()) * std::uint64_t(size.height());
    if (pixels > std::numeric_limits<std::size_t>::max() / 16 ||
        !systemMemoryAvailable(static_cast<std::size_t>(pixels) * 16))
        return fail(tr("Not enough memory to import the full-size tip image."));
    auto image = reader.read();
    if (image.isNull()) return fail(tr("Could not read the tip image: %1").arg(reader.errorString()));
    // Tip: coverage from alpha, or luminance for an opaque image.
    // Image / Colour Image: keep the original colour and coverage.
    const bool rgba = colour || image.hasAlphaChannel();
    image = std::move(image).convertToFormat(rgba ? QImage::Format_RGBA8888 : QImage::Format_Grayscale8);
    if (image.isNull()) return fail(tr("Not enough memory to convert the tip image."));
    brush::BrushResource resource;
    resource.name = QFileInfo(path).fileName().toStdString();
    resource.kind = rgba ? brush::ResourceKind::Rgba8 : brush::ResourceKind::Grayscale8;
    resource.width = image.width();
    resource.height = image.height();
    const auto bytes = resource.expectedBytes();
    if (!bytes || bytes > std::numeric_limits<std::uint32_t>::max())
        return fail(tr("The tip image does not fit the .kb v1 pixel representation."));
    std::vector<std::uint8_t> buffer(bytes);
    const auto stride = bytes / std::size_t(resource.height);
    for (int row = 0; row < resource.height; ++row)
        std::memcpy(buffer.data() + std::size_t(row) * stride, image.constScanLine(row), stride);
    resource.setPixels(std::move(buffer));
    return edit("Import tip image", [&] {
        auto& component = m_document.components[index];
        const auto oldResource = component.resourceId;
        resource.id = m_document.allocateId("r");
        component.resourceId = resource.id;
        if (!m_document.addResource(std::move(resource))) return false;
        // Do not rewrite a source shared with another component or an undo step.
        if (!oldResource.empty() && m_document.resourceUsage(oldResource) == 0)
            std::erase_if(m_document.resources, [&](const auto& asset) { return asset.id == oldResource; });
        return true;
    });
} catch (const std::bad_alloc&) {
    return fail(tr("Not enough memory to import the tip image."));
} catch (const std::length_error&) {
    return fail(tr("This tip image is too large to import on this system."));
}

QString BrushStudioModel::addComponent(int kind) {
    if (kind < 0 || kind >= int(std::size(kComponentKinds))) {
        fail(QStringLiteral("That component kind is not valid."));
        return {};
    }
    const ComponentKind componentKind = kComponentKinds[kind];
    QString id;
    const bool added = edit("Add component", [this, componentKind, &id] {
        BrushComponent component;
        component.id = m_document.allocateId("c");
        component.kind = componentKind;
        component.name = componentKindLabel(componentKind).toStdString();
        if (componentKind == ComponentKind::ProceduralShape) {
            component.parameters.set("tip-shape", 0.0);
        }
        if (kindNeedsPixels(componentKind)) {
            brush::BrushResource resource = makeDotResource(QStringLiteral("Dot"), isColourStamp(componentKind));
            resource.id = m_document.allocateId("r");
            component.resourceId = resource.id;
            if (!m_document.addResource(std::move(resource))) return false;
        }
        id = QString::fromStdString(component.id);
        if (!m_document.addComponent(std::move(component))) return false;
        m_selectedIndex = int(m_document.components.size()) - 1;
        return true;
    });
    return added ? id : QString{};
}

QString BrushStudioModel::duplicateComponent(const QString& id) {
    QString created;
    const std::string source = id.toStdString();
    if (!m_document.component(source)) {
        fail(QStringLiteral("There is no component with that id."));
        return QString();
    }
    const bool duplicated = edit("Duplicate component", [this, &source, &created] {
        std::string newId;
        if (!m_document.duplicateComponent(source, &newId)) return false;
        m_selectedIndex = m_document.componentIndex(newId);
        created = QString::fromStdString(newId);
        return true;
    });
    return duplicated ? created : QString{};
}

bool BrushStudioModel::removeComponent(const QString& id) {
    const std::string target = id.toStdString();
    if (!m_document.component(target)) {
        fail(QStringLiteral("There is no component with that id."));
        return false;
    }
    return edit("Delete component", [this, &target] {
        if (!m_document.removeComponent(target)) return false;
        if (m_selectedIndex >= int(m_document.components.size())) {
            m_selectedIndex = int(m_document.components.size()) - 1;
        }
        return true;
    });
}

bool BrushStudioModel::moveComponent(const QString& id, int newIndex) {
    const std::string target = id.toStdString();
    if (!m_document.component(target)) {
        fail(QStringLiteral("There is no component with that id."));
        return false;
    }
    if (newIndex < 0 || static_cast<std::size_t>(newIndex) >= m_document.components.size()) {
        return fail(QStringLiteral("That component position is outside the list."));
    }
    return edit("Reorder components", [this, &target, newIndex] {
        if (!m_document.moveComponent(target, newIndex)) return false;
        m_selectedIndex = newIndex;
        return true;
    });
}

BrushComponent* BrushStudioModel::editableComponent(const QString& id) {
    auto* component = m_document.component(id.toStdString());
    if (!component) fail(QStringLiteral("There is no component with that id."));
    return component;
}

bool BrushStudioModel::setComponentName(const QString& id, const QString& value) try {
    auto* component = editableComponent(id);
    if (!component) return false;
    if (value.trimmed().isEmpty()) return fail(QStringLiteral("A component needs a name."));
    return changeSetting("Rename component", component->name, value.toStdString());
} catch (const std::bad_alloc&) { return historyMemoryError(); }

bool BrushStudioModel::setComponentVisible(const QString& id, bool visible) {
    auto* component = editableComponent(id);
    return component && changeSetting("Toggle component", component->visible, visible);
}

bool BrushStudioModel::setComponentOpacity(const QString& id, double opacity) {
    auto* component = editableComponent(id);
    if (!component) return false;
    if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
        return fail(QStringLiteral("Component opacity must be between 0 and 1."));
    }
    return changeSetting("Component opacity", component->opacity, opacity);
}

bool BrushStudioModel::setComponentBlend(const QString& id, int blend) {
    auto* component = editableComponent(id);
    if (!component) return false;
    if (blend < 0 || blend > int(ComponentBlend::Mask)) {
        return fail(QStringLiteral("That component blend mode is not valid."));
    }
    return changeSetting("Component blend", component->blend, static_cast<ComponentBlend>(blend));
}

bool BrushStudioModel::setComponentErases(const QString& id, bool erases) {
    auto* component = editableComponent(id);
    return component && changeSetting("Component eraser", component->erases, erases);
}

bool BrushStudioModel::setComponentTransform(const QString& id, const QString& key, double value) {
    auto* component = editableComponent(id);
    if (!component) return false;
    for (const auto& setting : kTransformSettings) {
        if (key != QLatin1String(setting.key)) continue;
        const auto& info = *brush::findParameter(setting.parameter);
        const double low = info.minimum / setting.units;
        const double high = info.maximum / setting.units;
        if (!std::isfinite(value) || value < low || value > high) {
            return fail(QStringLiteral("Component %1 must be between %2 and %3.")
                        .arg(key).arg(low).arg(high));
        }
        return changeSetting("Component transform", component->transform.*setting.field, value);
    }
    return fail(QStringLiteral("There is no component transform called '%1'.").arg(key));
}

bool BrushStudioModel::setComponentWeight(const QString& id, double weight) {
    auto* component = editableComponent(id);
    if (!component) return false;
    const auto& info = *brush::findParameter("component-weight");
    if (!brush::parameterInRange(info, weight)) {
        return fail(QStringLiteral("Component weight must be between %1 and %2.")
                    .arg(info.minimum).arg(info.maximum));
    }
    if (const auto* group = m_document.group(component->groupId);
        weight == 0.0 && group && group->mode == SelectionMode::Weighted &&
        !hasPositiveWeight(m_document, *group, component->id)) {
        return fail(QStringLiteral("A weighted group needs at least one member with a positive weight."));
    }
    return changeSetting("Component weight", component->weight, weight);
}

QString BrushStudioModel::createGroup(const QString& name) {
    QString id;
    const bool added = edit("Add group", [this, &name, &id] {
        brush::ComponentGroup group;
        group.id = m_document.allocateId("g");
        group.name = name.isEmpty() ? "Group " + group.id : name.toStdString();
        group.mode = SelectionMode::All;
        id = QString::fromStdString(group.id);
        return m_document.addGroup(std::move(group));
    });
    return added ? id : QString{};
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
    return edit("Move component to group", [&] { return m_document.setComponentGroup(id, destination); });
}

bool BrushStudioModel::setGroupMode(const QString& groupId, int mode) {
    auto* group = m_document.group(groupId.toStdString());
    if (!group) return fail(QStringLiteral("There is no group with that id."));
    if (mode < 0 || mode > int(SelectionMode::Combination)) {
        return fail(QStringLiteral("That group selection mode is not valid."));
    }
    const auto value = static_cast<SelectionMode>(mode);
    if (value == SelectionMode::Weighted && !group->members.empty() &&
        !hasPositiveWeight(m_document, *group)) {
        return fail(QStringLiteral("A weighted group needs at least one member with a positive weight."));
    }
    return changeSetting("Group selection mode", group->mode, value);
}

bool BrushStudioModel::setGroupMembersPerStamp(const QString& groupId, int count) {
    auto* group = m_document.group(groupId.toStdString());
    if (!group) return fail(QStringLiteral("There is no group with that id."));
    if (count < 1) return fail(QStringLiteral("Members per stamp must be at least one."));
    return changeSetting("Members per stamp", group->membersPerStamp, count);
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
    return edit("Delete group", [&] { return m_document.removeGroup(id); });
}

// ---- settings ---------------------------------------------------------------

namespace {

QVariantMap parameterPresentation(const brush::ParameterInfo& info) {
    const QString key = QString::fromUtf8(info.name);
    QVariantMap map{
        {QStringLiteral("key"), key},
        {QStringLiteral("label"), QString::fromUtf8(info.label)},
        {QStringLiteral("description"), QString::fromUtf8(info.description)},
        {QStringLiteral("section"), QString::fromUtf8(brush::parameterSection(info.name))},
        {QStringLiteral("scope"), int(info.scope)},
        {QStringLiteral("unit"), unitName(info.unit)},
        {QStringLiteral("min"), info.minimum},
        {QStringLiteral("max"), info.maximum},
        {QStringLiteral("default"), info.defaultValue},
    };
    if (key == QStringLiteral("tip-shape")) {
        // Existing procedural tip choices; the numeric storage values are
        // documented by the registry and validated in setParameterValue().
        map.insert(QStringLiteral("editor"), QStringLiteral("choice"));
        map.insert(QStringLiteral("choices"), QStringList{BrushStudioModel::tr("Round"), BrushStudioModel::tr("Textured"),
            BrushStudioModel::tr("Bristle"), BrushStudioModel::tr("Graphite"), BrushStudioModel::tr("Stipple")});
    } else {
        // Very wide domains (such as size) use precision entry, not a rail
        // where the usual values would all be at the very first pixel.
        map.insert(QStringLiteral("editor"), info.maximum - info.minimum > 10000
                   ? QStringLiteral("number") : QStringLiteral("slider"));
    }
    return map;
}

bool parameterApplies(const brush::ParameterInfo& info, const BrushComponent* selected) {
    if (info.scope == brush::ParameterScope::Brush) return true;
    if (!selected) return false;
    const std::string_view key(info.name);
    if (key == "tip-shape") return selected->kind == ComponentKind::ProceduralShape;
    return key == "component-opacity" || key == "component-weight" ||
        std::ranges::any_of(kTransformSettings, [key](const auto& setting) { return key == setting.parameter; });
}

} // namespace

QVariantList BrushStudioModel::parameters() const {
    QVariantList list;
    for (const auto& info : brush::parameterRegistry()) {
        auto row = parameterPresentation(info);
        const auto key = row.value(QStringLiteral("key")).toString();
        row.insert(QStringLiteral("value"), parameterValue(key));
        row.insert(QStringLiteral("exposed"), isExposed(key));
        list.append(row);
    }
    return list;
}

QVariantList BrushStudioModel::settingSections() const {
    QVariantList list;
    for (const char* name : brush::parameterSections()) {
        const std::string_view section(name);
        QVariantList rows;
        for (const auto& info : brush::parameterRegistry()) {
            const char* group = brush::parameterSection(info.name);
            if (group && section == group) rows.append(parameterPresentation(info));
        }
        list.append(QVariantMap{
            {QStringLiteral("title"), QString::fromUtf8(name)},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("tipFirst"), section == "Brush Tip"},
            {QStringLiteral("textureFirst"), section == "Texture"},
        });
    }
    return list;
}

QVariantMap BrushStudioModel::settingState() const {
    QVariantMap values;
    int changed = 0;
    bool hasUnsupported = false;
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    const auto* selected = selectedComponentData();
    // One capability pass for the whole readback. Asking per row would re-run
    // the document validator dozens of times on every slider drag.
    const auto capability = drawing::inspectDigitalBrush(m_document);
    const auto parameterReason = [&](std::string_view name) -> QString {
        if (!drawing::digitalSupportsParameter(name))
            return QString::fromStdString(drawing::digitalParameterReason(m_document, name));
        if (const auto* found = capability.find("parameter:" + std::string(name)))
            return QString::fromStdString(found->message);
        return {};
    };
    QString componentSupport;
    if (selected) {
        if (!drawing::digitalSupportsComponentKind(selected->kind)) {
            componentSupport = QString::fromStdString(
                drawing::digitalComponentReason(m_document, selected->id));
        } else if (const auto* found = capability.find("component:" + selected->id)) {
            componentSupport = QString::fromStdString(found->message);
        }
    }
    for (const auto& info : brush::parameterRegistry()) {
        const auto key = QString::fromUtf8(info.name);
        const double value = parameterValue(key);
        const bool available = parameterApplies(info, selected);
        const bool differs = available && value != info.defaultValue;
        changed += differs ? 1 : 0;
        QString reason;
        if (!available) reason = tr("Select a component that has this setting.");
        else if (info.scope == brush::ParameterScope::Component && !componentSupport.isEmpty())
            reason = componentSupport;
        else reason = parameterReason(info.name);
        const bool supported = available && reason.isEmpty();
        values.insert(key, QVariantMap{
            {QStringLiteral("value"), value},
            {QStringLiteral("text"), locale.toString(value, 'g', QLocale::FloatingPointShortest)},
            {QStringLiteral("available"), available},
            {QStringLiteral("supported"), supported},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("changed"), differs},
            {QStringLiteral("resetEnabled"), available && supported && differs},
        });
        if (available && !supported) hasUnsupported = true;
    }
    return {{QStringLiteral("values"), values}, {QStringLiteral("changedCount"), changed},
            {QStringLiteral("hasUnsupported"), hasUnsupported}};
}

bool BrushStudioModel::setParameterText(const QString& key, const QString& text) {
    bool ok = false;
    // Use the same locale as the displayed value and reject ambiguity rather
    // than truncating a fractional value or passing NaN into the document.
    const double value = QLocale().toDouble(text, &ok);
    if (!ok) return fail(tr("Enter a number for '%1'.").arg(key));
    return setParameterValue(key, value);
}

double BrushStudioModel::parameterValue(const QString& key) const {
    const std::string name = key.toStdString();
    const auto* info = brush::findParameter(name);
    if (!info) return 0.0;
    if (info->scope == brush::ParameterScope::Brush) {
        return brush::BrushEvaluator::baseParameter(m_document, name);
    }
    if (m_selectedIndex < 0 || std::size_t(m_selectedIndex) >= m_document.components.size()) {
        return info->defaultValue;
    }
    const auto& component = m_document.components[std::size_t(m_selectedIndex)];
    if (name == "component-opacity") return component.opacity * 100.0;
    if (name == "component-weight") return component.weight;
    for (const auto& setting : kTransformSettings) {
        if (name == setting.parameter) return (component.transform.*setting.field) * setting.units;
    }
    return component.parameters.value(name, info->defaultValue);
}

bool BrushStudioModel::setParameterValue(const QString& key, double value) {
    const auto* info = brush::findParameter(key.toStdString());
    if (!info) return fail(QStringLiteral("There is no brush setting called '%1'.").arg(key));
    if (!brush::parameterInRange(*info, value)) {
        return fail(QStringLiteral("'%1' must be between %2 and %3.")
                    .arg(key).arg(info->minimum).arg(info->maximum));
    }
    if (info->scope == brush::ParameterScope::Brush && !drawing::digitalSupportsParameter(info->name)) {
        return fail(QString::fromStdString(drawing::digitalParameterReason(m_document, info->name)));
    }
    if (info->scope == brush::ParameterScope::Component) {
        const auto id = selectedId();
        if (id.isEmpty()) return fail(QStringLiteral("Select a component before editing '%1'.").arg(key));
        if (key == QStringLiteral("component-opacity")) return setComponentOpacity(id, value / 100.0);
        if (key == QStringLiteral("component-weight")) return setComponentWeight(id, value);
        for (const auto& setting : kTransformSettings) {
            if (key == QLatin1String(setting.parameter)) {
                return setComponentTransform(id, QString::fromLatin1(setting.key), value / setting.units);
            }
        }
        if (key == QStringLiteral("tip-shape")) {
            auto* component = editableComponent(id);
            if (!component) return false;
            if (component->kind != ComponentKind::ProceduralShape || value != std::trunc(value)) {
                return fail(QStringLiteral("Tip shape must be a built-in shape on a procedural component."));
            }
            if (component->parameters.value(info->name, info->defaultValue) != value) {
                return edit("Component tip shape", [&] { component->parameters.set(info->name, value); });
            }
        } else {
            return fail(QStringLiteral("There is no component editor for '%1'.").arg(key));
        }
    } else if (m_document.parameters.value(info->name, info->defaultValue) != value) {
        return edit("Change setting", [&] { return m_document.setParameter(info->name, value); });
    }
    clearError();
    return true;
}

QVariantMap BrushStudioModel::parameterInfo(const QString& key) const {
    const auto* info = brush::findParameter(key.toStdString());
    if (!info) return {};
    auto row = parameterPresentation(*info);
    row.insert(QStringLiteral("value"), parameterValue(key));
    return row;
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
        const QString support = QString::fromStdString(
            brush::findParameter(property->parameter)
                ? drawing::digitalParameterReason(m_document, property->parameter) : std::string{});
        map.insert(QStringLiteral("supported"), support.isEmpty());
        map.insert(QStringLiteral("supportReason"), support);
        map.insert(QStringLiteral("hasRange"), property->hasRangeOverride);
        map.insert(QStringLiteral("minimum"), property->hasRangeOverride && info
                                                   ? property->minimumOverride : info ? info->minimum : 0.0);
        map.insert(QStringLiteral("maximum"), property->hasRangeOverride && info
                                                   ? property->maximumOverride : info ? info->maximum : 1.0);
        list.append(map);
    }
    return list;
}

QVariantMap BrushStudioModel::exposureState() const {
    QStringList keys;
    QVariantMap entries;
    for (const auto& property : exposedProperties()) {
        const auto key = property.toMap().value(QStringLiteral("key")).toString();
        keys.append(key);
        entries.insert(key, property);
    }
    // Relabeling an entry only changes its readback, not the delegate model
    // or the positions of its buttons in the scrolling exposure page.
    return {{QStringLiteral("keys"), keys}, {QStringLiteral("entries"), entries}};
}

QVariantList BrushStudioModel::exposureCandidates() const {
    QVariantList list;
    for (const auto& info : brush::parameterRegistry()) {
        const auto key = QString::fromUtf8(info.name);
        if (isExposed(key) || !drawing::digitalSupportsParameter(info.name)) continue;
        list.append(QVariantMap{
            {QStringLiteral("key"), key},
            {QStringLiteral("label"), tr("%1 · %2").arg(QString::fromUtf8(info.label), QString::fromUtf8(brush::parameterSection(info.name)))},
        });
    }
    return list;
}

bool BrushStudioModel::isExposed(const QString& key) const {
    const auto name = key.toStdString();
    for (const auto& property : m_document.exposed) {
        if (property.parameter == name) return property.visible;
    }
    return false;
}

bool BrushStudioModel::expose(const QString& key) {
    const brush::ParameterInfo* info = brush::findParameter(key.toStdString());
    if (!info) {
        fail(QStringLiteral("There is no brush setting called '%1'.").arg(key));
        return false;
    }
    if (isExposed(key)) {
        clearError();
        return true;
    }
    return edit("Expose setting", [this, info] {
        brush::ExposedProperty property;
        property.parameter = info->name;
        property.order = int(m_document.exposed.size());
        return m_document.expose(std::move(property));
    });
}

bool BrushStudioModel::unexpose(const QString& key) {
    if (!isExposed(key)) return fail(QStringLiteral("'%1' is not exposed.").arg(key));
    return edit("Hide setting", [this, &key] { return m_document.unexpose(key.toStdString()); });
}

bool BrushStudioModel::setExposure(const QString& key, const QVariantMap& fields) try {
    const auto name = key.toStdString();
    const auto* info = brush::findParameter(name);
    const auto it = std::ranges::find(m_document.exposed, name, &brush::ExposedProperty::parameter);
    if (!info || it == m_document.exposed.end()) return fail(QStringLiteral("'%1' is not exposed.").arg(key));

    auto next = *it;
    double low = next.hasRangeOverride ? next.minimumOverride : info->minimum;
    double high = next.hasRangeOverride ? next.maximumOverride : info->maximum;
    bool rangeEdited = false;
    for (auto field = fields.cbegin(); field != fields.cend(); ++field) {
        const auto& k = field.key();
        const auto& v = field.value();
        if (k == QStringLiteral("label") || k == QStringLiteral("section")) {
            if (v.typeId() != QMetaType::QString) return fail(QStringLiteral("'%1' must be text.").arg(k));
            (k == QStringLiteral("label") ? next.label : next.section) = v.toString().toStdString();
        } else if (k == QStringLiteral("order")) {
            bool ok;
            const double order = v.toDouble(&ok);
            if (!ok || !std::isfinite(order) || order != std::trunc(order) ||
                order < std::numeric_limits<int>::min() || order > std::numeric_limits<int>::max()) {
                return fail(QStringLiteral("Exposure order must be a whole number in range."));
            }
            next.order = static_cast<int>(order);
        } else if (k == QStringLiteral("control")) {
            if (!brush::parseControlType(v.toString().toStdString(), &next.control)) {
                return fail(QStringLiteral("That exposure control is not valid."));
            }
        } else if (k == QStringLiteral("minimum") || k == QStringLiteral("maximum")) {
            bool ok;
            const double value = v.toDouble(&ok);
            if (!ok || !brush::parameterInRange(*info, value)) {
                return fail(QStringLiteral("The exposure range must stay within the brush setting's range."));
            }
            (k == QStringLiteral("minimum") ? low : high) = value;
            rangeEdited = true;
        } else if (k != QStringLiteral("hasRange")) {
            return fail(QStringLiteral("There is no exposure field called '%1'.").arg(k));
        }
    }
    const auto hasRange = fields.constFind(QStringLiteral("hasRange"));
    if (hasRange != fields.cend()) {
        if (hasRange->typeId() != QMetaType::Bool) return fail(QStringLiteral("Exposure range must be on or off."));
        next.hasRangeOverride = hasRange->toBool();
    } else if (rangeEdited) {
        next.hasRangeOverride = true;
    }
    if (rangeEdited || next.hasRangeOverride) {
        if (!brush::parameterInRange(*info, low) || !brush::parameterInRange(*info, high) || low > high) {
            return fail(QStringLiteral("Set an exposure minimum and maximum in order, within the brush setting's range."));
        }
        next.minimumOverride = low;
        next.maximumOverride = high;
    }
    return changeSetting("Edit exposed setting", *it, std::move(next));
} catch (const std::bad_alloc&) { return historyMemoryError(); }

bool BrushStudioModel::moveExposure(const QString& key, int delta) try {
    if (!delta) return fail(QStringLiteral("Specify a position to move the exposed setting to."));
    if (m_document.exposed.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return fail(QStringLiteral("Too many exposed settings to order."));
    }
    const auto name = key.toStdString();
    std::vector<brush::ExposedProperty*> ordered;
    ordered.reserve(m_document.exposed.size());
    for (auto& property : m_document.exposed) ordered.push_back(&property);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto* a, const auto* b) { return brush::exposedLess(*a, *b); });
    const auto from = std::ranges::find_if(ordered, [&](const auto* p) { return p->parameter == name; });
    if (from == ordered.end()) return fail(QStringLiteral("'%1' is not exposed.").arg(key));
    const auto target = static_cast<std::int64_t>(from - ordered.begin()) + delta;
    if (target < 0 || target >= static_cast<std::int64_t>(ordered.size()) ||
        brush::effectiveSection(*ordered[static_cast<std::size_t>(target)]) != brush::effectiveSection(**from)) {
        return fail(QStringLiteral("An exposed setting can only move inside its own section."));
    }
    return edit("Move exposed setting", [&] {
        const auto to = ordered.begin() + static_cast<std::ptrdiff_t>(target);
        if (to > from) std::rotate(from, from + 1, to + 1);
        else std::rotate(to, from, from + 1);
        for (std::size_t i = 0; i < ordered.size(); ++i) ordered[i]->order = static_cast<int>(i);
    });
} catch (const std::bad_alloc&) { return historyMemoryError(); }

void BrushStudioModel::resetExposure() {
    edit("Reset creator exposure", [this] {
        m_document.exposed.clear();
        int order = 0;
        for (const std::string_view key : drawing::recommendedDigitalControls()) {
            brush::ExposedProperty property;
            property.parameter = std::string(key);
            property.order = order++;
            m_document.expose(std::move(property));
        }
    });
}

bool BrushStudioModel::fail(const QString& message) {
    m_error = message;
    emit errorChanged();
    return false;
}

void BrushStudioModel::clearError() {
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorChanged();
}

QString BrushStudioModel::validationSummary() const {
    const brush::ValidationResult result = m_document.validate();
    return QString::fromStdString(result.summary());
}

} // namespace komapaint
