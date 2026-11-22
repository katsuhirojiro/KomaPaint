import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var picks: []
    property string mode: "In Order"
    signal edited(string mode)
    spacing: 9
    Section { Layout.fillWidth: true; heading: "Choose marks"; ruled: false }
    Segments {
        Layout.fillWidth: true
        choices: ["In Order","Random","Weighted"]
        current: root.mode; onChosen: key => root.edited(key)
    }
    Type {
        Layout.fillWidth: true
        text: root.mode === "In Order" ? "Cycle through visible marks from top to bottom."
            : root.mode === "Random" ? "Each visible mark has an equal chance at every step."
            : "Marks with a higher Frequency appear more often. Set it on an individual mark."
        color: Desk.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.2
    }
    Rectangle {
        visible: root.picks.length > 0
        Layout.fillWidth: true; implicitHeight: 65; radius: 6; color: Desk.base; border.color: Desk.line
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 7; spacing: 3
            RowLayout {
                id: orderTiles
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 3
                Repeater {
                    model: root.picks.slice(0, orderTiles.width > 275 ? 8 : 6)
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true; Layout.minimumWidth: 12; Layout.preferredWidth: 1; spacing: 2
                        Well { asset: modelData.asset; Layout.fillWidth: true; Layout.fillHeight: true; radius: 3 }
                        Tag { text: modelData.number; Layout.alignment: Qt.AlignHCenter; font.pixelSize: 9; font.letterSpacing: 0 }
                    }
                }
            }
        }
    }
    Tag { visible: root.picks.length > 0; text: "SAME EXAMPLE · FIRST PICKS"; font.pixelSize: 8; font.letterSpacing: .94 }
}
