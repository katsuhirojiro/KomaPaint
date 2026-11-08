find_package(Qt6 REQUIRED COMPONENTS Core Test Gui)
include(CTest)

function(komapaint_add_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE
        komapaint::core
        komapaint::history
        komapaint::layers
        komapaint::files
        komapaint::animation
        komapaint::color
        komapaint::canvas
        Qt6::Core Qt6::Test Qt6::Gui
    )
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
    )
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

komapaint_add_test(test_undostack TestUndoStack.cpp)
komapaint_add_test(test_layers    TestLayers.cpp)
komapaint_add_test(test_timeline   TestTimeline.cpp)
komapaint_add_test(test_canvas     TestCanvas.cpp)
komapaint_add_test(test_recent     TestRecentFiles.cpp)
