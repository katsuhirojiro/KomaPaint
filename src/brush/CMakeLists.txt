# Universal brush model and the native .kb format.
#
# Deliberately free of Qt: the model is data, and the tests for it run with
# nothing but a C++ compiler (see tests/unit/TestBrushModel.cpp). The QML
# binding used by Brush Studio is a later adapter on top of this library.
add_library(komapaint_brush STATIC
    BrushTypes.h BrushTypes.cpp
    BrushParameters.h BrushParameters.cpp
    BrushModel.h BrushModel.cpp
    BrushEvaluator.h BrushEvaluator.cpp
    BrushFormat.h BrushFormat.cpp
    BrushFile.cpp BrushFile_p.h
)
target_include_directories(komapaint_brush PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_library(komapaint::brush ALIAS komapaint_brush)
