# Low-level tile data is independent of layers, document and UI.
add_library(komapaint_tile_data STATIC DocumentMemoryBudget.h MemoryAvailability.h MemoryAvailability.cpp TileGeometry.h TileGeometry.cpp SparseTileStore.h SparseTileStore.cpp DirtyTileTracker.h DirtyTileTracker.cpp CpuTileCompositor.h CpuTileCompositor.cpp)
target_include_directories(komapaint_tile_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(komapaint::tile_data ALIAS komapaint_tile_data)

# Canvas module: coordinate spaces, viewport, document canvas base types.
add_library(komapaint_canvas STATIC
    CanvasDocument.h
    CanvasDocument.cpp
)
target_include_directories(komapaint_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_canvas PUBLIC komapaint::core komapaint::layers Qt6::Core Qt6::Gui)
add_library(komapaint::canvas ALIAS komapaint_canvas)
