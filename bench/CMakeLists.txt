# Qt-free workload harnesses. They link the same core libraries the editor
# uses, so a number measured here is the cost of the real code path -- no
# reimplementation of the hot loops lives in this directory.
#
# Off by default: benchmarks are for measuring, not for every developer build,
# and CI only builds them to prove the harnesses still compile.
function(komapaint_add_bench NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE komapaint::tile_data komapaint::drawing komapaint::material)
    target_include_directories(${NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    set_target_properties(${NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bench/bin")
endfunction()

komapaint_add_bench(bench_compositing compositing_bench.cpp)
komapaint_add_bench(bench_brush brush_bench.cpp)
komapaint_add_bench(bench_material material_bench.cpp)
komapaint_add_bench(bench_viewport viewport_bench.cpp)
