# Desktop executable entry point.
qt_add_executable(komapaint
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

target_link_libraries(komapaint PRIVATE
    komapaint::app
    Qt6::Widgets  # QApplication
)

set_target_properties(komapaint PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE   TRUE
)
