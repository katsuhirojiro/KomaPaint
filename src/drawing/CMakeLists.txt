# CPU brush processing; independent of QML and Qt input.
add_library(komapaint_drawing STATIC BrushEngine.h BrushEngine.cpp StrokeStabilizer.h StrokeStabilizer.cpp)
target_include_directories(komapaint_drawing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_drawing PUBLIC komapaint::tile_data)
add_library(komapaint::drawing ALIAS komapaint_drawing)
