# Layers module: layer data model, compositing order. Placeholder in Phase 1.
add_library(komapaint_layers STATIC
    Layer.h
    Layer.cpp
    LayerModel.h
    LayerModel.cpp
)
target_include_directories(komapaint_layers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_layers PUBLIC komapaint::core Qt6::Core Qt6::Gui)
add_library(komapaint::layers ALIAS komapaint_layers)
