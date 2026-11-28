# Colour adjustments are pure C++: parameters, LUTs and histograms. Keeping Qt
# out means the same maths runs in document edits, image imports and tests.
add_library(komapaint_adjustments STATIC
    Levels.h
    Levels.cpp
)
target_include_directories(komapaint_adjustments PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(komapaint::adjustments ALIAS komapaint_adjustments)
