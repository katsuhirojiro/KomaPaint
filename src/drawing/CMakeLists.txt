# Drawing module: brush engine, rasterization. Placeholder in Phase 1.
add_library(komapaint_drawing INTERFACE)
target_include_directories(komapaint_drawing INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_drawing INTERFACE komapaint::core Qt6::Core)
add_library(komapaint::drawing ALIAS komapaint_drawing)
