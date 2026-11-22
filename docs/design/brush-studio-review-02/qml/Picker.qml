import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import QtQuick.Dialogs
import ReviewArt 1.0

C.Dialog {
    id: root
    property string kind: "mark"
    property string heading: "Add a Mark"
    property string explanation: ""
    property string target: studio.addTo.uid
    property bool replaceAllowed: false
    property bool replace: false
    signal importWanted(string kind)
    signal tipWanted()
    modal: true
        width: Math.min(560, Window.width - 80)
    height: Math.min(486, Window.height - 60)
    padding: 0
    closePolicy: C.Popup.CloseOnEscape
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 2 }

    readonly property var entries: studio.options(kind, studio.chosen.uid)
    readonly property bool visual: kind === "mark" || kind === "texture" || kind === "mask"
    readonly property bool selectedMatches: studio.chosen.kind === kind

    function center() {
        const pw = parent && parent.width > 20 ? parent.width : Window.width
        const ph = parent && parent.height > 20 ? parent.height : Window.height
        width = Math.min(560, pw - 70)
        height = Math.min(486, ph - 50)
        x = Math.max(0, (pw - width) / 2)
        y = Math.max(0, (ph - height) / 2)
    }
    onAboutToShow: center()
    function take(key) {
        if (kind === "move") studio.use("move", key, "", studio.chosen.uid)
        else studio.use(kind, key, target, replace ? studio.chosen.uid : "")
        root.close()
    }

    contentItem: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 33
            color: Theme.chrome
            Rule { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.seam }
            LabelText {
                anchors.left: parent.left; anchors.right: closeButton.left
                anchors.margins: 11; anchors.verticalCenter: parent.verticalCenter
                text: root.heading; font.weight: Font.DemiBold; font.pixelSize: Theme.label
            }
            Command {
                id: closeButton
                anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
                glyph: "close"; hint: "Close"; quiet: true; onClicked: root.close()
            }
        }
        LabelText {
            Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
            Layout.topMargin: 9; Layout.bottomMargin: 4
            text: root.explanation
            color: Theme.mid; font.pixelSize: Theme.small; wrapMode: Text.WordWrap
            visible: text.length > 0
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            GridView {
                id: sourceGrid
                objectName: "sourceGrid"
                anchors.fill: parent
                visible: root.visual
                cellWidth: Math.max(102, width / Math.max(3, Math.floor(width / 112)))
                cellHeight: 85
                topMargin: 5; bottomMargin: 8; leftMargin: 8
                clip: true
                model: root.entries
                delegate: Rectangle {
                    id: cell
                    required property var modelData
                    objectName: "pick_" + modelData.key
                    width: sourceGrid.cellWidth - 6; height: sourceGrid.cellHeight - 5
                    radius: 2
                    color: cellMouse.containsMouse ? Theme.hover : "transparent"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 3
                        spacing: 1
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            color: "#252525"; border.color: "#4f4f4f"
                            Image {
                                anchors.fill: parent; anchors.margins: 2
                                source: root.visual ? "image://brushes/" + (root.kind === "mark" ? "pale/" : "grain/") + cell.modelData.key : ""
                                fillMode: Image.PreserveAspectFit; smooth: true
                                sourceSize: Qt.size(120, 90)
                            }
                        }
                        LabelText {
                            Layout.fillWidth: true; text: cell.modelData.name
                            horizontalAlignment: Text.AlignHCenter; font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: cellMouse
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.take(cell.modelData.key)
                    }
                }
            }
            ListView {
                id: optionList
                objectName: "optionList"
                anchors.fill: parent
                visible: !root.visual
                model: root.entries
                topMargin: 5; bottomMargin: 8
                clip: true
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    objectName: "pick_" + modelData.key
                    width: optionList.width; height: 47
                    color: rowMouse.containsMouse ? Theme.itemSelected : "transparent"
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 58; Layout.preferredHeight: 37
                            color: "#252525"; border.color: "#4f4f4f"
                            Artwork {
                                visible: root.kind === "brush"
                                anchors.fill: parent; anchors.margins: 1
                                kind: "preview"; source: row.modelData.key
                            }
                            Glyph {
                                visible: root.kind !== "brush"
                                anchors.centerIn: parent
                                name: root.kind === "move" ? "group" : "layers"
                                width: 20; height: 20; tint: Theme.mid
                            }
                        }
                        Column {
                            Layout.fillWidth: true; spacing: 1
                            LabelText { text: row.modelData.name; width: parent.width }
                            LabelText {
                                text: root.kind === "move" ? "Place inside" : (row.modelData.count > 0 ? row.modelData.count + " parts" : "Whole Brush")
                                color: Theme.muted; font.pixelSize: 10
                            }
                        }
                        Glyph { name: "right"; tint: Theme.mid; width: 14; height: 14 }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: root.take(row.modelData.key)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 45
            Layout.leftMargin: 10; Layout.rightMargin: 10
            spacing: 6
            CheckRow {
                visible: root.selectedMatches && root.visual
                text: "Replace selected " + (root.kind === "texture" ? "texture" : root.kind)
                checked: root.replace
                onToggled: root.replace = checked
            }
            Item { Layout.fillWidth: true }
            Command {
                visible: root.kind === "mark"
                text: "Draw a Tip"; glyph: "draw"
                onClicked: { root.close(); root.tipWanted() }
            }
            Command {
                visible: root.kind === "mark" || root.kind === "texture" || root.kind === "mask"
                text: "Import Image"; glyph: "import"
                onClicked: { root.close(); root.importWanted(root.kind) }
            }
        }
    }
}
