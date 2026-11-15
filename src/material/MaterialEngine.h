#pragma once

#include "../canvas/SparseTileStore.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace komapaint::material {

class MaterialTileStore;

// A compact 12-band visible spectrum is the optical state used for pigment
// mixing. RGB is an output encoding only; it is never used to combine known
// pigments.
inline constexpr std::size_t SpectralBands = 12;
using Spectrum = std::array<double, SpectralBands>;

struct PigmentDefinition {
    std::string id;
    std::string name;
    Spectrum absorption{};       // Kubelka-Munk K, per unit dry pigment
    Spectrum scattering{};       // Kubelka-Munk S, per unit dry pigment
    double particleSizeMicrons = 1.0;
    double physicalDensity = 1.0; // relative particle density; not concentration
    double tintStrength = 1.0;
    double transparency = 0.0;
    double granulation = 0.0;
    double staining = 0.5;
    double liftability = 0.5;
    double settling = 0.0;
    std::vector<std::string> media;
};

class PigmentLibrary {
public:
    PigmentLibrary();
    const PigmentDefinition* find(const std::string& id) const noexcept;
    const std::vector<PigmentDefinition>& pigments() const noexcept { return m_pigments; }
private:
    std::vector<PigmentDefinition> m_pigments;
};

struct MediumProfile {
    std::string id;
    std::string name;
    double viscosity = 0.5;
    double pigmentSuspension = 0.5;
    double binderResponse = 0.5;
    double hidingPower = 0.5;
    double waterSpread = 0.5;
    double absorption = 0.5;
    double dryingRate = 0.5;
    double matte = 0.5;
    double rewetting = 0.5;
    double adhesion = 0.5;
    double liftability = 0.5;
};

const MediumProfile& gouacheProfile() noexcept;
const MediumProfile& watercolourProfile() noexcept;
const MediumProfile& inkProfile() noexcept;
const MediumProfile* findMediumProfile(const std::string& id) noexcept;

// An identified component has a pigmentId and no residual colour. An
// arbitrary RGB colour uses an empty id plus residualArgb, explicitly marking
// it as an optical approximation rather than a named real-world pigment.
struct PigmentComponent {
    std::string pigmentId;
    std::uint32_t residualArgb = 0;
    double mass = 0.0;
};

struct PaintBatch {
    std::string mediumId = "gouache";
    std::vector<PigmentComponent> pigments;
    double water = 0.0;
    double binder = 0.0;
    double filler = 0.0;
    double wetness = 1.0;
    double consistency = 0.5;
    // Persistent process state: drying progresses from wet (0) to equilibrated
    // dry film (1); settling records the suspended-pigment fraction deposited.
    double drying = 0.0;
    double settling = 0.0;

    double pigmentMass() const noexcept;
    double totalMass() const noexcept;
    double pigmentConcentration() const noexcept;
    bool valid() const noexcept;
    void addWater(double quantity);
    void addBinder(double quantity);
    void addFiller(double quantity);
    void scale(double factor) noexcept;
    bool addPigment(std::string pigmentId, double quantity);
    bool addResidual(std::uint32_t straightArgb, double quantity);
};

// Material batches combine conserved quantities, never their rendered RGB.
// When media differ, the explicitly selected result medium owns the behavior.
PaintBatch mix(const PaintBatch& a, const PaintBatch& b,
               const std::string& resultMedium = {});
PaintBatch makeResidualPaint(std::uint32_t straightArgb, double pigmentMass,
                            double water, double binder,
                            const std::string& mediumId = "gouache");

struct SensorCurve {
    std::array<double, 3> points{0.0, 0.5, 1.0};
    double evaluate(double input) const noexcept;
};

enum class TipGeometry : std::uint8_t { Round, Flat, Bristle, Mop, Spatula };

struct BrushDefinition {
    std::string id;
    std::string name;
    TipGeometry tip = TipGeometry::Round;
    double size = 16.0;
    double spacingRatio = 0.15;
    double hardness = 0.0;
    double aspect = 1.0;
    double rotationRadians = 0.0;
    std::uint32_t bristleCount = 0;
    double bristleSpread = 0.0;
    double reservoirCapacity = 1.0;
    double initialLoad = 0.75;
    double releaseRate = 0.1;
    double depletionRate = 1.0;
    double pickupRate = 0.0;
    double reloadThreshold = 0.0;
    double pressureTransfer = 0.5;
    double speedTransfer = 0.0;
    double tiltTransfer = 0.0;
    double contactTransfer = 1.0;
    double waterSpread = 0.0;
    double pigmentDiffusion = 0.0;
    double toothResponse = 0.0;
    double edgeAccumulation = 0.0;
    double granulationResponse = 0.0;
    double paintLiftability = 0.0;
    double staining = 0.5;
    double rewetting = 0.0;
    SensorCurve pressureCurve{};
    SensorCurve speedCurve{{1.0, 0.5, 0.0}};
    SensorCurve tiltCurve{};
    PaintBatch initialPaint;
    std::string mediumId = "gouache";
    bool autoReloadOnStroke = true;

    bool valid() const noexcept;
};

std::vector<BrushDefinition> defaultBrushDefinitions();

struct BrushInput {
    double pressure = 1.0;
    double speed = 0.0;            // normalized [0,1]
    double tilt = 0.0;             // normalized [0,1]
    double rotationRadians = 0.0;
};

class BrushReservoir {
public:
    explicit BrushReservoir(double capacity = 1.0);
    double capacity() const noexcept { return m_capacity; }
    double quantity() const noexcept { return m_batch.totalMass(); }
    double remainingCapacity() const noexcept { const double remaining = m_capacity - quantity(); return remaining > 0.0 ? remaining : 0.0; }
    double fillRatio() const noexcept;
    double availablePigment() const noexcept { return m_batch.pigmentMass(); }
    const PaintBatch& batch() const noexcept { return m_batch; }
    bool load(const PaintBatch& paint, double amount = -1.0);
    bool reload(const BrushDefinition& brush);
    bool reloadIfNeeded(const BrushDefinition& brush);
    PaintBatch withdraw(double amount);
    bool pickup(const PaintBatch& paint, double amount);
    bool pickupFromSurface(const BrushDefinition& brush, const PaintBatch& paint, double contactArea);
    void clean(double waterAmount = 1.0, const std::string& mediumId = "gouache");
    void setCapacity(double capacity);
    double transferredAmount(const BrushDefinition& brush,
                             const BrushInput& input,
                             double contactArea) const noexcept;
private:
    double m_capacity = 1.0;
    PaintBatch m_batch;
    bool m_loaded = false;
};

struct SurfaceProfile {
    std::string id;
    double absorbency = 0.5;
    double tooth = 0.5;
    double roughness = 0.0;
    double paletteNonAbsorbency = 0.0;
    double porosity = 0.5;
    double waterRetention = 0.7;
    double pigmentRetention = 0.9;
    double binderRetention = 0.9;
    double flowResistance = 0.5;
    double dryingAcceleration = 1.0;
    double edgeAccumulation = 0.2;
    double rewetting = 0.5;
};

const SurfaceProfile& absorbentPaper() noexcept;
const SurfaceProfile& hotPressedPaper() noexcept;
const SurfaceProfile& coldPressedPaper() noexcept;
const SurfaceProfile& roughPaper() noexcept;
const SurfaceProfile& nonAbsorbentPalette() noexcept;

struct OpticalResult {
    std::uint32_t straightArgb = 0;
    double alpha = 0.0;
    double granulation = 0.0;
};

struct MaterialSimulationStats {
    std::size_t activeTiles = 0;
    std::size_t cellsVisited = 0;
    std::size_t cellsChanged = 0;
    std::size_t transfers = 0;
    double waterEvaporated = 0.0;
    double materialMoved = 0.0;
    bool failed = false;
};

class MaterialEngine {
public:
    explicit MaterialEngine(const PigmentLibrary& pigments) : m_pigments(pigments) {}
    const PigmentLibrary& pigments() const noexcept { return m_pigments; }
    Spectrum reflectance(const PaintBatch& paint, double depositShare = 1.0) const noexcept;
    OpticalResult opticalResult(const PaintBatch& paint, const MediumProfile& medium,
                                const SurfaceProfile& surface,
                                double depositShare = 1.0) const noexcept;
    std::uint32_t composite(std::uint32_t destinationPremultiplied,
                            const PaintBatch& paint, const MediumProfile& medium,
                            const SurfaceProfile& surface, double coverage,
                            double depositShare = 1.0,
                            double toothContact = 1.0) const noexcept;
    // Advance only tiles containing mobile material. Dry cells remain sparse
    // and unchanged; elapsed time is integrated analytically for stable lazy
    // catch-up after a tile has been inactive.
    MaterialSimulationStats advanceWetMaterial(MaterialTileStore& store,
                                                double elapsedSeconds,
                                                const SurfaceProfile& surface) const;
private:
    const PigmentLibrary& m_pigments;
};

// A real material dab: transfer consumes the reservoir, computes optical
// pigment output from its current composition, responds to medium/substrate,
// and writes only the dab's local bounds to sparse raster storage. The surface
// material model remains independently queryable and mixer-compatible.
class MaterialDab {
public:
    MaterialDab(const MaterialEngine& engine, const MediumProfile& medium,
                const SurfaceProfile& surface)
        : m_engine(engine), m_medium(medium), m_surface(surface) {}
    SparseTileStore::WriteResult apply(SparseTileStore& surfacePixels,
                                       const BrushDefinition& brush,
                                       BrushReservoir& reservoir,
                                       double centerX, double centerY,
                                       const BrushInput& input);
    SparseTileStore::WriteResult apply(MaterialTileStore& surfaceMaterial,
                                       const BrushDefinition& brush,
                                       BrushReservoir& reservoir,
                                       double centerX, double centerY,
                                       const BrushInput& input);
private:
    const MaterialEngine& m_engine;
    const MediumProfile& m_medium;
    const SurfaceProfile& m_surface;
};

struct MaterialSample {
    double x = 0.0;
    double y = 0.0;
    BrushInput input{};
};

class MaterialStroke {
public:
    MaterialStroke(SparseTileStore& surfacePixels, BrushDefinition brush,
                   BrushReservoir& reservoir, const MaterialEngine& engine,
                   const MediumProfile& medium, const SurfaceProfile& surface);
    MaterialStroke(MaterialTileStore& surfaceMaterial, BrushDefinition brush,
                   BrushReservoir& reservoir, const MaterialEngine& engine,
                   const MediumProfile& medium, const SurfaceProfile& surface,
                   bool eraseMaterial = false);
    bool begin(MaterialSample sample);
    bool extend(MaterialSample sample);
    bool end(MaterialSample sample);
    bool failed() const noexcept { return m_failed; }
    SparseTileStore::WriteResult failureReason() const noexcept { return m_failureReason; }
private:
    bool stamp(MaterialSample sample);
    SparseTileStore::WriteResult eraseMaterialDab(MaterialSample sample);
    SparseTileStore* m_surfacePixels = nullptr;
    MaterialTileStore* m_surfaceMaterial = nullptr;
    BrushDefinition m_brush;
    BrushReservoir& m_reservoir;
    MaterialDab m_dab;
    MaterialSample m_previous{};
    double m_spacing = 1.0;
    double m_remaining = 1.0;
    bool m_started = false;
    bool m_eraseMaterial = false;
    bool m_failed = false;
    SparseTileStore::WriteResult m_failureReason = SparseTileStore::WriteResult::Unchanged;
};

} // namespace komapaint::material
