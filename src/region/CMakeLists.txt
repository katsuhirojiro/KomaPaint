# Pure C++20 shared Region Understanding Engine & Selection Mask architecture.
add_library(komapaint_region STATIC
    SelectionMask.h
    SelectionMask.cpp
    RegionEngine.h
    RegionEngine.cpp
)

target_include_directories(komapaint_region PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(komapaint_region PUBLIC
    komapaint::tile_data
)

add_library(komapaint::region ALIAS komapaint_region)
