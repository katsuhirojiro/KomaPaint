import QtQuick
import KomaPaint 1.0
import "../themes"

// Isolated so MainWindow can load without registering the painted preview type
// in tests or contexts that never open an image Levels session.
Item {
    id: pane
    property var controller: null
    property color borderColor: KP.border

    LevelsPreview {
        objectName: "levelsImagePreview"
        anchors.fill: parent
        controller: pane.controller
        borderColor: pane.borderColor
    }
}
