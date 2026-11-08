# Undo/Redo history. Placeholder command stack in Phase 1.
add_library(komapaint_history STATIC
    UndoStack.h
    UndoStack.cpp
)
target_include_directories(komapaint_history PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(komapaint_history PUBLIC komapaint::core Qt6::Core)
add_library(komapaint::history ALIAS komapaint_history)
