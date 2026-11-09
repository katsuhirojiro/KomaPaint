# Canvas module: coordinate spaces, viewport, document canvas base types.
add_library(komapaint_canvas STATIC
    CanvasDocument.h
    CanvasDocument.cpp
)
target_include_directories(komapaint_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_canvas PUBLIC komapaint::core Qt6::Core Qt6::Gui)
add_library(komapaint::canvas ALIAS komapaint_canvas)
