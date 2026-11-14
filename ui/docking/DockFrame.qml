import QtQuick
import "../themes"

// The surface a docked or floating panel is drawn on: one background, one
// border, no shadow and no rounding, so a panel looks the same whether it is
// docked or floating. Panel QML itself never draws its own frame.
Rectangle {
    id: frame

    default property alias content: contentHost.data
    property color fill: KP.paneBg
    property bool framed: true

    color: frame.fill
    border.color: KP.border
    border.width: frame.framed ? 1 : 0
    clip: true

    Item {
        id: contentHost
        anchors.fill: parent
        anchors.margins: frame.framed ? 1 : 0
    }
}
