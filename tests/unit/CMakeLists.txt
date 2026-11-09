find_package(Qt6 REQUIRED COMPONENTS Core Test Gui)

# Put test executables in their own directory so they never get swept into the
# application deployment or the release ZIP. Qt6::Test is a test-only library
# and must NOT end up next to komapaint.exe.
set(KOMAPAINT_TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/tests/bin")

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
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}"
    )
    add_test(NAME ${NAME} COMMAND ${NAME})
    # Run headless, and ensure Qt DLLs are discoverable at test runtime on
    # Windows by prepending Qt's bin directory to PATH.
    set_tests_properties(${NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${KOMAPAINT_TEST_OUTPUT_DIR}"
    )
endfunction()

komapaint_add_test(test_undostack TestUndoStack.cpp)
komapaint_add_test(test_layers    TestLayers.cpp)
komapaint_add_test(test_timeline   TestTimeline.cpp)
komapaint_add_test(test_canvas     TestCanvas.cpp)
komapaint_add_test(test_recent     TestRecentFiles.cpp)
