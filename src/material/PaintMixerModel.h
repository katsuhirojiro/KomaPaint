#pragma once

#include "MaterialEngine.h"
#include "MaterialTileStore.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace komapaint::material {

struct MixerDeposit {
    std::string id;
    std::string name;
    double x = 48.0;
    double y = 48.0;
    double radius = 22.0;
    PaintBatch batch;
    bool containsDigitalImport() const noexcept;
};

struct DepositOpticalSummary {
    std::uint32_t straightArgb = 0;
    std::uint32_t porcelainArgb = 0xfffaf8f4U;
    std::uint32_t darkGroundArgb = 0xff282a2eU;
    double alpha = 0.0;
    double granulation = 0.0;
    double pigmentShare = 0.0;
    double waterShare = 0.0;
    double binderShare = 0.0;
    double fillerShare = 0.0;
};

class PaintMixerModel {
public:
    static constexpr int SurfaceWidth = 256;
    static constexpr int SurfaceHeight = 160;
    static constexpr int SurfaceTileSize = 64;

    PaintMixerModel();

    const MaterialEngine& engine() const noexcept { return m_engine; }
    const PigmentLibrary& pigments() const noexcept { return m_pigments; }
    const MaterialTileStore& surfaceStore() const noexcept { return m_surfaceStore; }

    const std::string& activeMediumId() const noexcept { return m_mediumId; }
    bool setMedium(const std::string& mediumId);

    const std::vector<MixerDeposit>& deposits() const noexcept { return m_deposits; }
    const std::string& activeDepositId() const noexcept { return m_activeDepositId; }
    const MixerDeposit* activeDeposit() const noexcept;
    const MixerDeposit* findDeposit(const std::string& id) const noexcept;
    bool selectDeposit(const std::string& id) noexcept;
    std::optional<std::string> depositAt(double x, double y) const noexcept;

    std::string createDeposit(const std::string& name, const PaintBatch& batch,
                              double x = -1.0, double y = -1.0);
    std::string createEmptyDeposit(const std::string& name = {},
                                   double x = -1.0, double y = -1.0);
    bool replaceDepositBatch(const std::string& id, const PaintBatch& batch,
                             const std::string& newName = {},
                             const std::string& undoLabel = "Edit deposit");
    bool renameDeposit(const std::string& id, const std::string& name);
    bool moveDeposit(const std::string& id, double x, double y);
    bool resetDeposit(const std::string& id);
    bool removeDeposit(const std::string& id);
    void clearAllDeposits();

    bool addPigmentToActive(const std::string& pigmentId, double quantity = 1.0,
                            bool createIfNone = true);
    std::string addPigmentDeposit(const std::string& pigmentId, double quantity = 1.0,
                                  double x = -1.0, double y = -1.0);

    struct ColorImportResult {
        bool ok = false;
        std::string depositId;
        bool matchedKnownPigment = false;
        std::string matchedPigmentId;
    };
    ColorImportResult addColorFromWheel(std::uint32_t straightArgb,
                                        bool allowPigmentMatch = true,
                                        bool createNewDeposit = true,
                                        double quantity = 1.0);
    bool addResidualToActive(std::uint32_t straightArgb, double quantity = 1.0);

    bool setComponentMass(std::size_t index, double mass);
    bool removeComponent(std::size_t index);
    bool addWaterToActive(double delta);
    bool setWaterOnActive(double water);
    bool setBinderOnActive(double binder);
    bool setFillerOnActive(double filler);
    bool addPaintBodyToActive(double factorDelta);

    // Combines two deposits using conserved physical material mixing (Kubelka-Munk
    // pigment spectra + conserved water/binder/filler). If createNew is true, a
    // new mixed deposit is placed on the palette; otherwise target receives the mix.
    std::optional<std::string> mixDeposits(const std::string& sourceId,
                                           const std::string& targetId,
                                           double sourceShare = 0.5,
                                           bool createNew = false);

    DepositOpticalSummary opticalSummary(const MixerDeposit& deposit,
                                       const SurfaceProfile* surface = nullptr) const noexcept;
    DepositOpticalSummary activeOpticalSummary(const SurfaceProfile* surface = nullptr) const noexcept;

    // Renders the isolated non-absorbent palette MaterialTileStore via MaterialRenderer.
    bool renderSurface(std::vector<std::uint32_t>& outPremultipliedArgb) const;

    // Decoupled mixer undo/redo stack.
    bool canUndo() const noexcept { return m_undoIndex > 0; }
    bool canRedo() const noexcept { return m_undoIndex + 1 < m_history.size(); }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool undo();
    bool redo();
    void clearHistory();

    // Used for session persistence restore without inflating undo history.
    void restoreWorkspaceState(const std::string& mediumId,
                               std::vector<MixerDeposit> deposits,
                               const std::string& activeDepositId);

private:
    struct HistorySnapshot {
        std::string label;
        std::string mediumId;
        std::string activeDepositId;
        std::vector<MixerDeposit> deposits;
    };

    PaintBatch defaultBatchForMedium(const std::string& mediumId) const;
    void defaultWaterBinderFiller(const std::string& mediumId,
                                  double& water, double& binder, double& filler) const noexcept;
    std::pair<double, double> nextAvailableSpot() const noexcept;
    MixerDeposit* mutableDeposit(const std::string& id) noexcept;
    std::string nextDepositId();
    void pushHistory(std::string label);
    void rebuildSurfaceTiles();

    PigmentLibrary m_pigments;
    MaterialEngine m_engine;
    MaterialTileStore m_surfaceStore;
    std::string m_mediumId = "gouache";
    std::vector<MixerDeposit> m_deposits;
    std::string m_activeDepositId;
    std::uint64_t m_nextId = 1;
    std::vector<HistorySnapshot> m_history;
    std::size_t m_undoIndex = 0;
};

} // namespace komapaint::material
