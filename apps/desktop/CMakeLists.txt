# Desktop executable entry point.
#
# All QML files are bundled into the executable via a Qt resource collection
# file (resources/qml.qrc). This ensures that windeployqt/macdeployqt produce
# a self-contained binary that finds its UI without any path tricks or
# post-install copies of ui/ next to the executable.

qt_add_executable(komapaint WIN32
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
    ${CMAKE_SOURCE_DIR}/resources/qml.qrc
)

target_link_libraries(komapaint PRIVATE
    komapaint::app
    Qt6::Widgets   # QApplication for native menus / file dialogs
)

# The WIN32 keyword above makes komapaint a Windows GUI-subsystem
# executable: the Windows loader will not allocate a separate console
# window for it. Qt's entry-point support still calls our portable
# main(). Development diagnostics use Qt logging instead of stdio; on
# Windows they are routed to the debugger output channel (see main.cpp),
# so no console is needed to inspect them during development.

# No explicit qt_finalize_executable() call here: since Qt 6.3 qt_add_executable
# finalises the target at the end of this directory scope by itself (it runs the
# AUTOMOC/AUTORCC final pass and registers the windeployqt / macdeployqt
# deployment hooks). Calling it again produced "Tried to call
# qt6_finalize_target twice on target 'komapaint'" on every reconfigure. The
# project requires Qt 6.5, so the automatic pass is always available.
