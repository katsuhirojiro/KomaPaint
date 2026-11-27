# Pure C++20 normalized pointer abstraction (no Qt dependency).
add_library(komapaint_input_core STATIC
    PointerInput.cpp
    PointerInput.h
    SpeedDynamics.h
    SpeedTracker.h
)

target_include_directories(komapaint_input_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_library(komapaint::input_core ALIAS komapaint_input_core)

# Qt/OS backend adapter, WinTab dynamic integration, and persistent settings.
add_library(komapaint_input STATIC
    QtPointerAdapter.cpp
    QtPointerAdapter.h
    TabletInputSettings.cpp
    TabletInputSettings.h
    WinTabBackend.cpp
    WinTabBackend.h
)

target_include_directories(komapaint_input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(komapaint_input PUBLIC
    komapaint::input_core
    komapaint::core
    Qt6::Core
    Qt6::Gui
)

add_library(komapaint::input ALIAS komapaint_input)
