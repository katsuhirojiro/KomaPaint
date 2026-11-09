# Desktop executable entry point.
#
# All QML files are bundled into the executable via a Qt resource collection
# file (resources/qml.qrc). This ensures that windeployqt/macdeployqt produce
# a self-contained binary that finds its UI without any path tricks or
# post-install copies of ui/ next to the executable.

qt_add_executable(komapaint
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
    ${CMAKE_SOURCE_DIR}/resources/qml.qrc
)

target_link_libraries(komapaint PRIVATE
    komapaint::app
    Qt6::Widgets   # QApplication for native menus / file dialogs
)

# TEMPORARY: build komapaint as a console app during startup diagnostics so
# stdout/stderr are visible from cmd / PowerShell / CI without needing
# AttachConsole tricks or log-file redirection. Restore WIN32_EXECUTABLE TRUE
# once the smoke test passes and the artifact launches cleanly.
set_target_properties(komapaint PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE   FALSE
)

# Finalize the executable in the same directory scope where it is declared.
# This runs the AUTOMOC/AUTORCC final pass and registers deployment hooks
# so windeployqt / macdeployqt can pick up the correct dependencies.
qt_finalize_executable(komapaint)
