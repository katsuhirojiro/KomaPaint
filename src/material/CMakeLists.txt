# Compact CPU material and pigment model. Kept independent of Qt/QML so the
# authoritative mixing and reservoir operations are easy to test directly.
add_library(komapaint_material STATIC
    MaterialEngine.h
    MaterialEngine.cpp
    MaterialTileStore.h
    MaterialTileStore.cpp
    PaintMixerModel.h
    PaintMixerModel.cpp
    BrushPresets.cpp
)
target_include_directories(komapaint_material PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_material PUBLIC komapaint::tile_data)
add_library(komapaint::material ALIAS komapaint_material)
