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

komapaint_add_test(test_fast_rounding TestFastRounding.cpp)
komapaint_add_test(test_tool_cycle TestToolCycle.cpp)
komapaint_add_test(test_undostack TestUndoStack.cpp)
target_link_libraries(test_undostack PRIVATE komapaint::drawing)
komapaint_add_test(test_layers    TestLayers.cpp)
komapaint_add_test(test_timeline   TestTimeline.cpp)
komapaint_add_test(test_canvas     TestCanvas.cpp)
komapaint_add_test(test_recent     TestRecentFiles.cpp)

komapaint_add_test(test_document_creation TestDocumentCreation.cpp)
target_link_libraries(test_document_creation PRIVATE komapaint::app)

komapaint_add_test(test_new_project TestNewProject.cpp)
target_link_libraries(test_new_project PRIVATE komapaint::app)
komapaint_add_test(test_document_file TestDocumentFile.cpp)
target_link_libraries(test_document_file PRIVATE komapaint::app komapaint::files Qt6::Widgets Qt6::Quick)
komapaint_add_test(test_command_line TestCommandLine.cpp)
target_link_libraries(test_command_line PRIVATE komapaint::app)

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
komapaint_add_test(test_dock_ui TestDockUi.cpp)
target_sources(test_dock_ui PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_dock_ui PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)
komapaint_add_test(test_docking TestDocking.cpp)
target_link_libraries(test_docking PRIVATE komapaint::app)
komapaint_add_test(test_canvas_viewport TestCanvasViewport.cpp)
target_sources(test_canvas_viewport PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_canvas_viewport PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)
add_executable(test_brush_engine TestBrushEngine.cpp)
target_link_libraries(test_brush_engine PRIVATE komapaint::drawing)
set_target_properties(test_brush_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_brush_engine COMMAND test_brush_engine)
add_executable(test_stroke_stabilizer TestStrokeStabilizer.cpp)
target_link_libraries(test_stroke_stabilizer PRIVATE komapaint::drawing)
set_target_properties(test_stroke_stabilizer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_stroke_stabilizer COMMAND test_stroke_stabilizer)

# The document brush on the real engines: a migrated brush must paint the same
# pixels, and several components must paint per stamp. Also pure C++.
add_executable(test_brush_document_engine TestBrushDocumentEngine.cpp)
target_link_libraries(test_brush_document_engine PRIVATE komapaint::drawing)
set_target_properties(test_brush_document_engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_brush_document_engine COMMAND test_brush_document_engine)
add_executable(test_digital_capabilities TestDigitalCapabilities.cpp)
target_link_libraries(test_digital_capabilities PRIVATE komapaint::drawing)
set_target_properties(test_digital_capabilities PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_digital_capabilities COMMAND test_digital_capabilities)

# Brush model and the .kb container are pure C++ (no Qt): the same test
# binaries run in CI and on a machine with only a compiler.
foreach(brush_test IN ITEMS brush_parameters brush_model brush_format brush_evaluator)
    if(brush_test STREQUAL "brush_model")
        set(brush_source TestBrushModel.cpp)
    elseif(brush_test STREQUAL "brush_format")
        set(brush_source TestBrushFormat.cpp)
    elseif(brush_test STREQUAL "brush_parameters")
        set(brush_source TestBrushParameters.cpp)
    else()
        set(brush_source TestBrushEvaluator.cpp)
    endif()
    add_executable(test_${brush_test} ${brush_source})
    target_link_libraries(test_${brush_test} PRIVATE komapaint::brush)
    set_target_properties(test_${brush_test} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
    add_test(NAME test_${brush_test} COMMAND test_${brush_test})
endforeach()

# Material/pigment model is pure C++ and is exercised independently of QML.
add_executable(test_material_engine TestMaterialEngine.cpp)
target_link_libraries(test_material_engine PRIVATE komapaint::material)
set_target_properties(test_material_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_material_engine COMMAND test_material_engine)

add_executable(test_paint_mixer TestPaintMixer.cpp)
target_link_libraries(test_paint_mixer PRIVATE komapaint::material)
set_target_properties(test_paint_mixer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_paint_mixer COMMAND test_paint_mixer)

add_executable(test_performance_benchmark TestPerformanceBenchmark.cpp)
target_link_libraries(test_performance_benchmark PRIVATE komapaint::drawing)
set_target_properties(test_performance_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_performance_benchmark COMMAND test_performance_benchmark)

add_executable(test_pointer_input TestPointerInput.cpp)
target_link_libraries(test_pointer_input PRIVATE komapaint::input_core komapaint::drawing)
set_target_properties(test_pointer_input PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_pointer_input COMMAND test_pointer_input)

add_executable(test_input_performance_benchmark TestInputPerformanceBenchmark.cpp)
target_link_libraries(test_input_performance_benchmark PRIVATE komapaint::input_core komapaint::drawing)
set_target_properties(test_input_performance_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_input_performance_benchmark COMMAND test_input_performance_benchmark)

add_executable(test_region_engine TestRegionEngine.cpp)
target_link_libraries(test_region_engine PRIVATE komapaint::region)
set_target_properties(test_region_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_region_engine COMMAND test_region_engine)

komapaint_add_test(test_tablet_viewport TestTabletViewport.cpp)
target_sources(test_tablet_viewport PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_tablet_viewport PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)

komapaint_add_test(test_paint_mixer_app TestPaintMixerApp.cpp)
target_link_libraries(test_paint_mixer_app PRIVATE komapaint::app komapaint::files Qt6::Widgets Qt6::Quick)
komapaint_add_test(test_material_document TestMaterialDocument.cpp)
target_link_libraries(test_material_document PRIVATE komapaint::drawing)

komapaint_add_test(test_project_workflow TestProjectWorkflow.cpp)
target_sources(test_project_workflow PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_project_workflow PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)

# Non-production reproducible measurement of the existing history budget.
komapaint_add_test(test_history_audit TestHistoryAudit.cpp)
target_link_libraries(test_history_audit PRIVATE komapaint::drawing)
if(WIN32)
    target_link_libraries(test_history_audit PRIVATE Psapi)
endif()

add_executable(test_document_memory_budget TestDocumentMemoryBudget.cpp)
target_link_libraries(test_document_memory_budget PRIVATE komapaint::tile_data)
set_target_properties(test_document_memory_budget PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
add_test(NAME test_document_memory_budget COMMAND test_document_memory_budget)

komapaint_add_test(test_history_allocation_failure TestHistoryAllocationFailure.cpp)

# Isolated codec correctness and allocation-failure tests.
add_executable(test_history_tile_codec TestHistoryTileCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/history/HistoryTileCodec.cpp)
target_include_directories(test_history_tile_codec PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_history_tile_codec PRIVATE komapaint::drawing)
set_target_properties(test_history_tile_codec PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${KOMAPAINT_TEST_OUTPUT_DIR}")
if(WIN32)
    target_link_libraries(test_history_tile_codec PRIVATE Psapi)
endif()
add_test(NAME test_history_tile_codec COMMAND test_history_tile_codec)

komapaint_add_test(test_history_journal TestHistoryJournal.cpp)

komapaint_add_test(test_workspace_recovery TestWorkspaceRecovery.cpp)
target_link_libraries(test_workspace_recovery PRIVATE komapaint::app)

komapaint_add_test(test_recovery_ui TestRecoveryUi.cpp)
target_sources(test_recovery_ui PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_recovery_ui PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)

# Phase 3: independent documents, per-entry recovery, session restore.
komapaint_add_test(test_workspace_session TestWorkspaceSession.cpp)
target_link_libraries(test_workspace_session PRIVATE komapaint::app)

komapaint_add_test(test_workspace_ui TestWorkspaceUi.cpp)
komapaint_add_test(test_qml_responsiveness TestQmlResponsiveness.cpp)
target_sources(test_qml_responsiveness PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_qml_responsiveness PRIVATE komapaint::app komapaint::layers Qt6::Widgets Qt6::Quick)
target_sources(test_workspace_ui PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_workspace_ui PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)

# Icons, tool palette and the brush panels through the real main window.
# Brush Studio's Qt-facing model: the lists QML will read, the edits, the undo
# history and the .kb round trip. No window is needed, so it runs headless.
komapaint_add_test(test_brush_studio_model TestBrushStudioModel.cpp)
target_link_libraries(test_brush_studio_model PRIVATE komapaint::app)
komapaint_add_test(test_native_brush_library TestNativeBrushLibrary.cpp)
target_link_libraries(test_native_brush_library PRIVATE komapaint::app)
komapaint_add_test(test_native_brush_controls TestNativeBrushControls.cpp)
target_link_libraries(test_native_brush_controls PRIVATE komapaint::app)
komapaint_add_test(test_brush_ui TestBrushUi.cpp)
target_sources(test_brush_ui PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
# The human artist check opens the same .kb fixtures that ship in the ZIP.
target_compile_definitions(test_brush_ui PRIVATE
    KOMAPAINT_ARTIST_BRUSH_DIR="${CMAKE_SOURCE_DIR}/docs/testing/brushes")
target_link_libraries(test_brush_ui PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)

# TEMPORARY: visual capture harness for the design pass (removed afterwards).
komapaint_add_test(test_shots TestShots.cpp)
target_sources(test_shots PRIVATE ${CMAKE_SOURCE_DIR}/resources/qml.qrc)
target_link_libraries(test_shots PRIVATE komapaint::app Qt6::Widgets Qt6::Quick)
