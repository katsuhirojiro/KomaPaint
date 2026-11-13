import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Button {
    id: btn

    property string iconText: ""
    property string tooltip: ""
    property int fontPx: KP.fontM
    property bool checkableButton: false
    property bool checkedButton: false

    width: 30
    height: KP.toolbarHeight - 6
    padding: 0
    flat: true

    ToolTip.text: tooltip
    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.delay: 400

    background: Rectangle {
        color: btn.down ? KP.buttonBgPress : (btn.hovered ? KP.buttonBgHover : (btn.checkedButton ? KP.toolActive : "transparent"))
        radius: KP.radiusS
        border.color: btn.checkedButton ? KP.borderLight : "transparent"
        border.width: 1
    }

    contentItem: Label {
        text: btn.iconText
        color: btn.enabled ? (btn.checkedButton ? KP.textOnActive : KP.text) : KP.textSubtle
        font.family: KP.fontUi
        font.pixelSize: btn.fontPx
        font.bold: false
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        renderType: Text.NativeRendering
    }
}
