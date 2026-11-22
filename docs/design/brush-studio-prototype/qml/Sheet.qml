import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    property string eyebrow: "BRUSH WORKSHOP"
    property string heading: ""
    property string description: ""
    property int wantedWidth: 500
    property int wantedHeight: 410
    property Component inner
    anchors.centerIn: Overlay.overlay
    width: Math.min(wantedWidth, Overlay.overlay ? Overlay.overlay.width - 24 : wantedWidth)
    height: Math.min(wantedHeight, Overlay.overlay ? Overlay.overlay.height - 24 : wantedHeight)
    padding: 20
    modal: true; focus: true
    background: Rectangle { radius: 12; color: Desk.panel; border.color: Desk.line }
    Overlay.modal: Rectangle { color: "#a0111110" }
    contentItem: ColumnLayout {
        spacing: 13
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Tag { text: root.eyebrow; color: Desk.accent }
                Type { text: root.heading; font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            DeskButton { objectName: "closeSheet"; iconName: "close"; accessible: "Close " + root.heading; style: "quiet"; onClicked: root.close() }
        }
        Type { visible: root.description.length > 0; text: root.description; color: Desk.body; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.22 }
        Loader { Layout.fillWidth: true; Layout.fillHeight: true; sourceComponent: root.inner; active: root.visible }
    }
}
