import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string heading: ""
    property string detail: ""
    property bool ruled: true
    spacing: 7
    Rectangle { visible: root.ruled; Layout.fillWidth: true; implicitHeight: 1; color: Desk.softLine; Layout.bottomMargin: 7 }
    Type { text: root.heading; Layout.fillWidth: true; font.weight: Font.DemiBold; font.pixelSize: 15; wrapMode: Text.WordWrap }
    Type { visible: root.detail.length > 0; text: root.detail; Layout.fillWidth: true; color: Desk.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.23 }
}
