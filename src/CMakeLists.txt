# Core static library shared by the application and tests.
# Each module is kept in its own subdirectory and added as a library; we aggregate
# them here so the desktop app can link against a single `komapaint_core` target.

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(canvas)
add_subdirectory(drawing)
add_subdirectory(layers)
add_subdirectory(animation)
add_subdirectory(color)
add_subdirectory(assets)
add_subdirectory(files)
add_subdirectory(history)
