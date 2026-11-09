# Color management: palettes, color picker state. Placeholder in Phase 1.
add_library(komapaint_color STATIC
    ColorPalette.h
    ColorPalette.cpp
)
target_include_directories(komapaint_color PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_color PUBLIC komapaint::core Qt6::Core Qt6::Gui)
add_library(komapaint::color ALIAS komapaint_color)
