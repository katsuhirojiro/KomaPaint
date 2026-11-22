import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import ReviewArt 1.0

Rectangle {
    id: root
    color: Theme.chrome
    Layout.preferredWidth: 206
    Layout.minimumWidth: 184
    Layout.fillHeight: true
    Rule { anchors.right: parent.right; height: parent.height; width: 1; color: Theme.seam }

    property string query: ""
    readonly property var allBrushes: studio.brushes
    function matching(section) {
        query = search.text.toLowerCase()
        return allBrushes.filter(b => b.section === section &&
            (!query || b.name.toLowerCase().indexOf(query) >= 0))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        DockLabel { Layout.fillWidth: true; text: "Sub Tool" }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 37
            Layout.leftMargin: 8; Layout.rightMargin: 8; color: "transparent"
            EditField {
                id: search
                objectName: "brushSearch"
                anchors.fill: parent
                anchors.topMargin: 5; anchors.bottomMargin: 5
                placeholderText: "Search brushes"
                Accessible.name: "Search brushes"
                leftPadding: 25
                Glyph { x: 7; anchors.verticalCenter: parent.verticalCenter; name: "search"; width: 15; height: 15; tint: Theme.mid }
                onTextChanged: root.query = text.toLowerCase()
                Keys.onEscapePressed: { text = ""; root.forceActiveFocus() }
            }
        }
        C.ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            C.ScrollBar.vertical: ScrollRail {}
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 0
                Repeater {
                    model: ["Starters", "Study Brushes", "Brushes in This Session"]
                    delegate: ColumnLayout {
                        id: section
                        required property string modelData
                        readonly property var entries: root.matching(modelData)
                        Layout.fillWidth: true
                        spacing: 0
                        visible: entries.length > 0
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: 25
                            Layout.topMargin: 4
                            color: Theme.stripe
                            LabelText {
                                anchors.fill: parent; anchors.leftMargin: 10
                                text: section.modelData.toUpperCase()
                                color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold
                                font.letterSpacing: .8
                            }
                        }
                        Repeater {
                            model: section.entries
                            delegate: Rectangle {
                                id: tile
                                required property var modelData
                                objectName: "brush_" + modelData.key
                                Layout.fillWidth: true
                                Layout.preferredHeight: 57
                                Layout.leftMargin: 5; Layout.rightMargin: 5
                                radius: 2
                                color: studio.currentKey === modelData.key ? Theme.itemSelected
                                       : hit.containsMouse ? Theme.hover : "transparent"
                                Row {
                                    anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 5
                                    spacing: 7
                                    Rectangle {
                                        width: 63; height: 47; anchors.verticalCenter: parent.verticalCenter
                                        color: "#252525"; border.color: "#4f4f4f"
                                        Artwork {
                                            anchors.fill: parent; anchors.margins: 1
                                            kind: "preview"; source: tile.modelData.key
                                        }
                                    }
                                    Column {
                                        width: parent.width - 75; anchors.verticalCenter: parent.verticalCenter
                                        spacing: 2
                                        LabelText {
                                            width: parent.width; text: tile.modelData.name
                                            font.pixelSize: Theme.small
                                            font.weight: studio.currentKey === tile.modelData.key ? Font.DemiBold : Font.Normal
                                        }
                                        LabelText {
                                            width: parent.width
                                            text: tile.modelData.section === "Study Brushes" ? "Layered study" :
                                                  tile.modelData.section === "Brushes in This Session" ? "Session copy" : "Starter"
                                            color: Theme.muted; font.pixelSize: 9
                                        }
                                    }
                                }
                                MouseArea {
                                    id: hit
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: studio.selectBrush(tile.modelData.key)
                                }
                            }
                        }
                    }
                }
                LabelText {
                    Layout.fillWidth: true; Layout.margins: 12
                    visible: root.matching("Starters").length + root.matching("Study Brushes").length +
                             root.matching("Brushes in This Session").length === 0
                    text: "No brush matches “" + search.text + "”."
                    color: Theme.muted; wrapMode: Text.WordWrap
                }
                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 43; color: Theme.panel
            Rule { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.seam }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                spacing: 4
                Command {
                    objectName: "newBrush"
                    Layout.fillWidth: true; text: "New Brush"; glyph: "plus"; leftAligned: true
                    hint: "Start a brush from a single round mark"
                    onClicked: studio.newBrush()
                }
                Command { objectName: "duplicateBrush"; glyph: "copy"; hint: "Duplicate this brush in this session"; quiet: true; onClicked: studio.duplicateBrush() }
            }
        }
    }
}
