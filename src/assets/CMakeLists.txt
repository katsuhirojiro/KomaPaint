# Asset management: brushes, textures, presets. Placeholder in Phase 1.
add_library(komapaint_assets INTERFACE)
target_include_directories(komapaint_assets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_assets INTERFACE komapaint::core Qt6::Core)
add_library(komapaint::assets ALIAS komapaint_assets)
