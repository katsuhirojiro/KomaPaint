import QtQuick
import QtQuick.Layouts
import "../themes"

// A deliberately empty, separate workspace for the future asset manager.
Rectangle {
    color: KP.paneBg
    Text {
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 2 * KP.panelInset)
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("No assets yet")
        color: KP.textMuted
        font.pixelSize: KP.fontS
        elide: Text.ElideRight
    }
}
