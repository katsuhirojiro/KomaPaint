import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Item {
    id: root
    Rectangle {
        anchors.fill: parent
        color: KP.paneHeaderBg
        border.color: KP.border
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        spacing: 3

        Repeater {
            model: ListModel {
                ListElement {
                    tool: "selection"
                    glyph: "⬚"
                    tip: "Selection (V)"
                }
                ListElement {
                    tool: "move"
                    glyph: "✥"
                    tip: "Move (M)"
                }
                ListElement {
                    tool: "brush"
                    glyph: "🖌"
                    tip: "Brush (B)"
                }
                ListElement {
                    tool: "pencil"
                    glyph: "✎"
                    tip: "Pencil (P)"
                }
                ListElement {
                    tool: "eraser"
                    glyph: "⌫"
                    tip: "Eraser (E)"
                }
                ListElement {
                    tool: "fill"
                    glyph: "▣"
                    tip: "Fill (G)"
                }
                ListElement {
                    tool: "eyedropper"
                    glyph: "◉"
                    tip: "Eyedropper (I)"
                }
                ListElement {
                    tool: "text"
                    glyph: "T"
                    tip: "Text (T)"
                }
            }

            ToolButton {
                required property int index
                required property string tool
                required property string glyph
                required property string tip

                Layout.alignment: Qt.AlignHCenter
                width: KP.toolButtonSize
                height: KP.toolButtonSize
                padding: 0
                flat: true
                checkable: true
                checked: Workspace.activeTool === tool
                onClicked: Workspace.activeTool = tool

                ToolTip.text: tip
                ToolTip.visible: hovered
                ToolTip.delay: 400

                background: Rectangle {
                    color: parent.checked ? KP.toolActive : parent.hovered ? KP.buttonBgHover : "transparent"
                    radius: KP.radiusS
                    border.color: parent.checked ? KP.borderLight : "transparent"
                    border.width: 1
                }
                contentItem: Label {
                    text: glyph
                    color: parent.checked ? "#ffffff" : KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontL
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    renderType: Text.NativeRendering
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }

        // Foreground/background color swatch (mini, bottom of palette)
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 36
            height: 36
            color: "transparent"
            Rectangle {
                x: 0
                y: 8
                width: 20
                height: 20
                color: Workspace.colors.background
                border.color: KP.border
                radius: KP.radiusS
            }
            Rectangle {
                x: 10
                y: 0
                width: 20
                height: 20
                color: Workspace.colors.foreground
                border.color: KP.border
                radius: KP.radiusS
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: Workspace.colors.swap()
                }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: Workspace.colors.swap()
            }
        }
    }
}
