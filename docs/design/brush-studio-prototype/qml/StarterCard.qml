import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

Button {
    id: root
    property string key: ""
    property string title: ""
    property string caption: ""
    property string iconName: ""
    property string artwork: ""
    property bool compact: false
    property string cardPaper: Desk.paper
    implicitHeight: compact ? 68 : 134
    implicitWidth: 255
    text: title
    hoverEnabled: true
    padding: 0
    background: Rectangle {
        radius: 8
        color: root.down ? Desk.active : root.hovered ? Desk.hover : Desk.panel
        border.color: root.visualFocus || root.hovered ? Desk.accentLine : Desk.line
        border.width: root.visualFocus ? 2 : 1
    }
    contentItem: Item {
        Loader {
            anchors.fill: parent; anchors.margins: root.compact ? 10 : 9
            sourceComponent: root.compact ? small : full
        }
    }
    Component {
        id: full
        ColumnLayout {
            spacing: 7
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: 5; color: root.cardPaper; clip: true
                ArtSheet { anchors.fill: parent; anchors.margins: 1; kind: "sample"; sample: root.key }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 4; Layout.rightMargin: 3; Layout.bottomMargin: 3
                spacing: 5
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 3
                    Type { text: root.title; Layout.fillWidth: true; font.pixelSize: 16; font.weight: Font.DemiBold; elide: Text.ElideRight }
                    Type { text: root.caption; Layout.fillWidth: true; font.pixelSize: 12; color: Desk.muted; elide: Text.ElideRight }
                }
                Glyph { name: "arrow"; tint: root.hovered ? Desk.accent : Desk.muted; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
            }
        }
    }
    Component {
        id: small
        RowLayout {
            spacing: 10
            Item {
                Layout.preferredWidth: 46; Layout.preferredHeight: 46
                Well { anchors.fill: parent; asset: root.artwork }
                Glyph {
                    visible: !root.artwork.length
                    anchors.centerIn: parent; width: 23; height: 23; name: root.iconName; tint: Desk.ink
                }
                Rectangle {
                    visible: root.iconName.length > 0 && root.artwork.length > 0
                    color: "#f4eadb"; radius: 3; width: 17; height: width; anchors.right: parent.right; anchors.bottom: parent.bottom
                    Glyph { anchors.centerIn: parent; width: 14; height: 14; name: root.iconName; tint: Desk.ink }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Type { text: root.title; Layout.fillWidth: true; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideRight }
                Type { text: root.caption; Layout.fillWidth: true; font.pixelSize: 12; color: Desk.muted; elide: Text.ElideRight }
            }
            Glyph { name: "right"; tint: root.hovered ? Desk.accent : Desk.muted; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
        }
    }
}
