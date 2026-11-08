# Animation module: timeline, frames, onion skinning. Placeholder in Phase 1.
add_library(komapaint_animation STATIC
    TimelineState.h
    TimelineState.cpp
)
target_include_directories(komapaint_animation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_animation PUBLIC komapaint::core Qt6::Core)
add_library(komapaint::animation ALIAS komapaint_animation)
