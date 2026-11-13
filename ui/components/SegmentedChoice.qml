import QtQuick
import QtQuick.Controls
import "../themes"

// A row of mutually exclusive text choices (e.g. tip shape: Round | Textured).
Row {
    id: root
    property var options: []      // list of labels
    property int currentIndex: 0
    signal chosen(int index)
    spacing: 0
    opacity: enabled ? 1 : 0.45

    Repeater {
        model: root.options
        AbstractButton {
            id: segment
            required property int index
            required property string modelData
            readonly property bool current: index === root.currentIndex
            width: Math.floor(root.width / Math.max(1, root.options.length))
            height: KP.controlHeight
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            onClicked: if (!current) root.chosen(index)
            background: Rectangle {
                color: segment.current ? KP.toolActive : (segment.hovered ? KP.buttonBgHover : KP.inputBg)
                border.width: 1
                border.color: segment.current ? KP.accent : KP.border
                radius: KP.radiusS
            }
            contentItem: Label {
                text: segment.modelData
                color: segment.current ? KP.textOnActive : KP.text
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
