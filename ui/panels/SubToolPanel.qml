import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Sub tools of the active painting tool. Each entry is a set of real brush
// engine settings (see SubToolLibrary); selecting one loads those settings.
Rectangle {
    id: panel
    color: KP.paneBg
    border.color: KP.border
    border.width: 1

    readonly property var library: Workspace.subTools
    // Label of the active tool, taken from the tool palette's own list.
    property string toolLabel: Workspace.activeTool

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Sub Tool")
            onCloseClicked: Panels.setVisible("subtool", false)
        }

        // Active tool
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 30
            color: KP.paneBgAlt
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapM
                anchors.rightMargin: KP.gapM
                spacing: KP.gapM
                KpIcon {
                    name: "tools/" + Workspace.activeTool
                    size: 18
                    color: KP.text
                }
                Label {
                    Layout.fillWidth: true
                    text: panel.toolLabel
                    color: KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontL
                    font.bold: true
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.separator
            }
        }

        ListView {
            id: list
            objectName: "subToolList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: panel.library.paintTool
            model: panel.library.subTools
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                readonly property bool current: index === panel.library.currentIndex
                objectName: "subTool" + index
                width: list.width
                height: 30
                color: current ? KP.toolActive : (rowMouse.containsMouse ? KP.buttonBgHover : "transparent")

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.library.select(row.index)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: KP.gapM
                    anchors.rightMargin: KP.gapM
                    spacing: KP.gapM
                    // Tip preview: solid for a crisp tip, ringed for a soft
                    // one, dotted for a textured one.
                    Item {
                        implicitWidth: 18
                        implicitHeight: 18
                        Rectangle {
                            anchors.centerIn: parent
                            readonly property real soft: 1 - row.modelData.hardness / 100
                            width: 14
                            height: 14
                            radius: 7
                            color: row.modelData.textured ? "transparent" : (row.current ? KP.textOnActive : KP.text)
                            opacity: row.modelData.textured ? 1 : 1 - 0.65 * soft
                            border.width: row.modelData.textured ? 2 : 0
                            border.color: row.current ? KP.textOnActive : KP.text
                            Rectangle {
                                visible: row.modelData.textured
                                anchors.centerIn: parent
                                width: 4
                                height: 4
                                radius: 2
                                color: row.current ? KP.textOnActive : KP.text
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.name + (row.modelData.modified ? " *" : "")
                        color: row.current ? KP.textOnActive : KP.text
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontM
                        elide: Text.ElideRight
                    }
                    Label {
                        text: row.modelData.size + " px"
                        color: row.current ? KP.textOnActiveMuted : KP.textMuted
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontS
                    }
                }
            }
        }

        Label {
            visible: !panel.library.paintTool
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: KP.gapL
            text: qsTr("This tool has no sub tools.")
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignTop
        }
    }
}
