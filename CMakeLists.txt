cmake_minimum_required(VERSION 3.21)

project(KomaPaint
    VERSION 0.1.0
    DESCRIPTION "Professional 2D digital painting and animation application."
    HOMEPAGE_URL "https://komapaint.app"
    LANGUAGES CXX
)

# ---- Global configuration ---------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Honor visibility properties for all targets.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Export compile commands for IDEs / linters.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build output directories.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---- Options ----------------------------------------------------------------
option(KOMAPAINT_BUILD_TESTS      "Build unit tests."             ON)
option(KOMAPAINT_ENABLE_WARNINGS  "Enable strict compiler warnings." ON)

if(KOMAPAINT_ENABLE_WARNINGS)
    if(MSVC)
        add_compile_options(/W4 /permissive- /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14640 /w14826 /w14905 /w14906 /w14928)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual)
    endif()
endif()

# ---- CMake helpers ----------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---- Qt ---------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Widgets       # for QApplication (native menus, file dialogs)
)

qt_standard_project_setup()

# ---- Subdirectories ---------------------------------------------------------
add_subdirectory(src)
add_subdirectory(apps/desktop)

if(KOMAPAINT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Install ----------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS komapaint
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)
qt_finalize_executable(komapaint)
