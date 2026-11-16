import QtQuick
import "../components"
import "../themes"

// Dockable host for the shared Physical Paint Mixer (lives primarily inside
// the Colors panel's Paint Mixer tab, and also stays available here if the
// user opens the standalone Color Mixer dock panel).
Rectangle {
    id: panel
    color: KP.paneBg

    PaintMixerView {
        anchors.fill: parent
    }
}
