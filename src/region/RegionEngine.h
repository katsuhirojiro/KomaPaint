#pragma once

#include "../canvas/CpuTileCompositor.h"
#include "../canvas/SparseTileStore.h"
#include "SelectionMask.h"

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace komapaint::region {

enum class ReferenceMode {
    CurrentLayer,
    AllVisible,
    SelectedLayers,
    ExcludeSelected
};

enum class RegionStatus {
    Empty,
    Enclosed,
    PartiallyEnclosed,
    AmbiguousLeak
};

// Describes one source layer participating in shared region analysis.
struct ReferenceLayerDesc {
    const void* layerKey = nullptr;
    SparseTileStore* digital = nullptr;
    // Optional callback to render a material tile (64x64 ARGB premultiplied).
    std::function<bool(TilePoint, std::vector<std::uint32_t>&)> renderMaterialTile;
    std::uint64_t revision = 0;
    double opacity = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    bool visible = true;
    bool selected = false;
    bool active = false;
};

struct ReferenceSet {
    std::int64_t docWidth = 0;
    std::int64_t docHeight = 0;
    std::uint32_t backgroundArgb = 0xFFFFFFFFU;
    bool transparentBackground = true;
    ReferenceMode mode = ReferenceMode::AllVisible;
    std::vector<ReferenceLayerDesc> layersTopFirst; // top-most layer first

    std::uint64_t signature() const noexcept;
};

// Tight bounding-box analysis output (never allocates full-canvas buffers).
struct RegionResult {
    RegionStatus status = RegionStatus::Empty;
    PixelBounds bounds;
    std::vector<std::uint8_t> coverage;           // 0..255 interior coverage
    std::vector<std::uint8_t> boundaryConfidence; // 0..255 detected boundary strength around region
    std::vector<std::uint8_t> uncertainMask;      // 0..255 gap-bridged / ambiguous boundary pixels
    double confidence = 0.0;                      // overall topological confidence [0..1]
    std::size_t uncertainPixels = 0;
    std::size_t filledPixels = 0;
    std::uint64_t sourceSignature = 0;

    bool empty() const noexcept { return bounds.empty() || coverage.empty() || filledPixels == 0; }
    std::uint8_t coverageAt(std::int64_t x, std::int64_t y) const noexcept;
    std::uint8_t uncertainAt(std::int64_t x, std::int64_t y) const noexcept;
};

struct FillParams {
    int tolerance = 28;            // 0..255 perceptual distance threshold
    bool perceptualColor = true;
    bool alphaAware = true;
    int gapClosing = 0;            // 0..32 px
    int areaScaling = 1;           // -16..16 px
    bool antialiasing = true;
    bool colorBleeding = true;
    bool contiguous = true;
};

struct SmartFillParams {
    int tolerance = 34;            // 0..255 interior color tolerance
    int boundarySensitivity = 65;  // 0..100 sensitivity to faint/thin/antialiased ink
    int gapTolerance = 10;         // 0..32 px max accidental gap bridged
    int areaScaling = 1;           // -16..16 px expand/contract
    int edgeQuality = 85;          // 0..100 sketchy-pocket cleanup & AA quality
    bool perceptualColor = true;
    bool alphaAware = true;
    bool antialiasing = true;
    bool colorBleeding = true;
    bool contiguous = true;
};

struct MagicWandParams {
    int tolerance = 26;
    bool perceptualColor = true;
    bool alphaAware = true;
    int gapClosing = 0;
    int boundarySensitivity = 65;
    int areaScaling = 0;
    int feather = 0;
    bool antialiasing = true;
    bool contiguous = true;
};

struct ProtectedRegionParams {
    int boundarySensitivity = 65;
    int gapTolerance = 8;
    int tolerance = 36;
    bool conservativeBoundary = true;
    int rimProtectionPx = 1;
};

// Cached per-tile reference artwork + boundary feature extraction.
// Automatically invalidates dirty tiles whenever layer revisions or reference
// configuration change.
class RegionCache {
public:
    static constexpr std::int64_t kTileSize = 64;
    static constexpr std::size_t kTilePixels = 64 * 64;

    struct CachedTile {
        std::uint64_t tileRevision = 0;
        std::vector<std::uint32_t> argb;         // 4096 premultiplied ARGB
        std::vector<std::uint8_t> edgeStrength;  // 4096 [0..255]
        std::vector<std::uint8_t> strokeScore;   // 4096 [0..255]
        std::vector<std::int8_t> gradX;          // 4096 [-127..127]
        std::vector<std::int8_t> gradY;          // 4096 [-127..127]
    };

    void invalidateAll() noexcept;
    void syncWith(const ReferenceSet& refs);

    const CachedTile* acquireTile(const ReferenceSet& refs, TilePoint tile);
    std::uint32_t pixelAt(const ReferenceSet& refs, std::int64_t x, std::int64_t y);
    std::uint8_t edgeAt(const ReferenceSet& refs, std::int64_t x, std::int64_t y);

    std::size_t cachedTileCount() const noexcept { return m_tiles.size(); }
    std::uint64_t currentSignature() const noexcept { return m_signature; }

private:
    std::uint64_t computeTileRevision(const ReferenceSet& refs, TilePoint tile) const noexcept;
    void buildTile(const ReferenceSet& refs, TilePoint tile, CachedTile& out);

    std::uint64_t m_signature = 0;
    std::int64_t m_docWidth = 0;
    std::int64_t m_docHeight = 0;
    std::map<TilePoint, CachedTile, TilePointLess> m_tiles;
    std::vector<std::uint32_t> m_scratchCompose;
    std::vector<std::vector<std::uint32_t>> m_scratchMaterial;
};

// Shared deterministic Region Understanding Engine serving Conventional Fill,
// Smart Fill, Magic Wand, Magnetic Lasso, and Boundary-Aware Eraser.
class RegionEngine {
public:
    static double perceptualDistance(std::uint32_t argbA, std::uint32_t argbB,
                                     bool perceptual, bool alphaAware) noexcept;

    RegionResult computeConventionalFill(const ReferenceSet& refs,
                                         std::int64_t seedX, std::int64_t seedY,
                                         const FillParams& params,
                                         const SelectionMask* selection = nullptr);

    RegionResult computeSmartFill(const ReferenceSet& refs,
                                  std::int64_t seedX, std::int64_t seedY,
                                  const SmartFillParams& params,
                                  const SelectionMask* selection = nullptr);

    RegionResult computeMagicWand(const ReferenceSet& refs,
                                  std::int64_t seedX, std::int64_t seedY,
                                  const MagicWandParams& params,
                                  const SelectionMask* selection = nullptr);

    RegionResult computeProtectedRegion(const ReferenceSet& refs,
                                        std::int64_t seedX, std::int64_t seedY,
                                        const ProtectedRegionParams& params,
                                        const SelectionMask* selection = nullptr);

    // Edge-assisted path snapping between (ax, ay) and (bx, by) for Magnetic Lasso.
    std::vector<Vec2> snapMagneticPath(const ReferenceSet& refs,
                                       Vec2 from, Vec2 to,
                                       int magneticStrength);

    RegionCache& cache() noexcept { return m_cache; }
    const RegionCache& cache() const noexcept { return m_cache; }

private:
    RegionCache m_cache;
    // Reusable scratch buffers to avoid repeated heap churn.
    std::vector<std::uint8_t> m_workVisited;
    std::vector<std::uint8_t> m_workBarrier;
    std::vector<std::uint8_t> m_workUncertain;
    std::vector<std::uint8_t> m_workBoundary;
    std::vector<std::int64_t> m_workQueue;
};

} // namespace komapaint::region
