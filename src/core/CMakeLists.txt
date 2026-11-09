# Core module: non-UI application services.
add_library(komapaint_core_base STATIC
    ApplicationConfig.cpp
    ApplicationConfig.h
    NewProjectSettings.cpp
    NewProjectSettings.h
    Settings.cpp
    Settings.h
    ModuleInfo.h
)

target_include_directories(komapaint_core_base PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(komapaint_core_base PUBLIC
    Qt6::Core
)

add_library(komapaint::core ALIAS komapaint_core_base)
