# Application-level services (QML engine, main window controller).
add_library(komapaint_app STATIC
    BrushSettings.h
    CanvasViewport.cpp
    CanvasViewport.h
    CommandLine.cpp
    CommandLine.h
    FileAssociation.cpp
    FileAssociation.h
    KomaPaintApp.cpp
    KomaPaintApp.h
    PanelManager.cpp
    PanelManager.h
    WorkspaceState.cpp
    WorkspaceState.h
)

target_include_directories(komapaint_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

if(WIN32)
    # advapi32: registry (file association). shell32: SHChangeNotify.
    # Also in MSVC's default libraries, listed explicitly for clarity.
    set(KOMAPAINT_WIN_LIBS advapi32 shell32)
else()
    set(KOMAPAINT_WIN_LIBS)
endif()

target_link_libraries(komapaint_app PUBLIC
    komapaint::core
    komapaint::canvas
    komapaint::drawing
    komapaint::layers
    komapaint::animation
    komapaint::color
    komapaint::history
    komapaint::files
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    ${KOMAPAINT_WIN_LIBS}
)

# Make QML / resource files reachable relative to this source dir.
target_compile_definitions(komapaint_app PRIVATE
    KOMAPAINT_QML_DIR="${CMAKE_SOURCE_DIR}/ui"
    KOMAPAINT_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"
)

add_library(komapaint::app ALIAS komapaint_app)
