import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// A simple working palette. Swatches select a colour; these are not user-saved palettes.
Rectangle {
    color: KP.paneBg
    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: paletteColumn.height + 2 * KP.panelInset
        clip: true
        Column {
            id: paletteColumn
            x: KP.panelInset; y: KP.panelInset
            width: parent.width - 2 * KP.panelInset
            spacing: KP.gapS
            Text { text: qsTr("Working colours"); color: KP.textMuted; font.pixelSize: KP.fontS }
            Grid {
                id: swatches
                width: parent.width
                columns: Math.max(1, Math.floor(width / 26))
                spacing: 3
                Repeater {
                    model: ["#000000", "#333333", "#666666", "#999999", "#cccccc", "#ffffff",
                            "#c45050", "#cc8748", "#d0bd5a", "#70a260", "#4d9292", "#5576aa", "#8a6e9c"]
                    AbstractButton {
                        id: swatch
                        required property string modelData
                        width: 23; height: 23
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: qsTr("Select colour %1").arg(modelData)
                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                        onClicked: Workspace.colors.foreground = modelData
                        background: Rectangle {
                            color: swatch.modelData
                            border.width: swatch.activeFocus ? 2 : 1
                            border.color: swatch.activeFocus ? KP.focusRing : KP.borderLight
                        }
                    }
                }
            }
        }
    }
}
