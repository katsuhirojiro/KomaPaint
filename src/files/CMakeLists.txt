# File management: illustration open/save (.koma format).
add_library(komapaint_files STATIC
    DocumentFile.h
    DocumentFile.cpp
    RecentFiles.h
    RecentFiles.cpp
)
target_include_directories(komapaint_files PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_files PUBLIC komapaint::canvas komapaint::layers komapaint::core Qt6::Core Qt6::Gui)
add_library(komapaint::files ALIAS komapaint_files)
