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
target_link_libraries(test_undostack PRIVATE komapaint::drawing)
komapaint_add_test(test_layers    TestLayers.cpp)
komapaint_add_test(test_timeline   TestTimeline.cpp)
komapaint_add_test(test_canvas     TestCanvas.cpp)
komapaint_add_test(test_recent     TestRecentFiles.cpp)

komapaint_add_test(test_document_creation TestDocumentCreation.cpp)
target_link_libraries(test_document_creation PRIVATE komapaint::app)

# Geometry is deliberately independent of Qt so it can also be tested standalone.
add_executable(test_tile_geometry TestTileGeometry.cpp ${CMAKE_SOURCE_DIR}/src/canvas/TileGeometry.cpp)
target_include_directories(test_tile_geometry PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(test_tile_geometry PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_tile_geometry COMMAND test_tile_geometry)
add_executable(test_sparse_tile_store TestSparseTileStore.cpp)
target_link_libraries(test_sparse_tile_store PRIVATE komapaint::tile_data)
set_target_properties(test_sparse_tile_store PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_sparse_tile_store COMMAND test_sparse_tile_store)
foreach(test_name IN ITEMS dirty_tile_tracker tile_allocation_failure)
    if(test_name STREQUAL "dirty_tile_tracker")
        set(test_source TestDirtyTileTracker.cpp)
    else()
        set(test_source TestTileAllocationFailure.cpp)
    endif()
    add_executable(test_${test_name} ${test_source})
    target_link_libraries(test_${test_name} PRIVATE komapaint::tile_data)
    set_target_properties(test_${test_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
    add_test(NAME test_${test_name} COMMAND test_${test_name})
endforeach()
add_executable(test_cpu_tile_compositor TestCpuTileCompositor.cpp)
target_link_libraries(test_cpu_tile_compositor PRIVATE komapaint::tile_data)
set_target_properties(test_cpu_tile_compositor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_cpu_tile_compositor COMMAND test_cpu_tile_compositor)
komapaint_add_test(test_canvas_viewport TestCanvasViewport.cpp)
target_sources(test_canvas_viewport PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_canvas_viewport PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)
add_executable(test_brush_engine TestBrushEngine.cpp)
target_link_libraries(test_brush_engine PRIVATE komapaint::drawing)
set_target_properties(test_brush_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_brush_engine COMMAND test_brush_engine)
