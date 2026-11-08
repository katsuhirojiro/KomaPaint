# File management: open/save/new, format support. Placeholder in Phase 1.
add_library(komapaint_files STATIC
    RecentFiles.h
    RecentFiles.cpp
)
target_include_directories(komapaint_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_files PUBLIC komapaint::core Qt6::Core)
add_library(komapaint::files ALIAS komapaint_files)
