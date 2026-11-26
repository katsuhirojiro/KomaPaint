import QtQuick
import QtQuick.Controls
import "../themes"

// A row of mutually exclusive text choices (e.g. tip shape: Round | Textured).
Row {
    id: root
    property var options: []      // list of labels
    property var explanations: [] // one sentence per label, shown on hover, focus and press
    property int currentIndex: 0
    signal chosen(int index)
    spacing: KP.gapXS
    opacity: enabled ? 1 : 0.45

    Repeater {
        model: root.options
        AbstractButton {
            id: segment
            required property int index
            required property string modelData
            readonly property bool current: index === root.currentIndex
            width: Math.floor((root.width - Math.max(0, root.options.length - 1) * root.spacing) / Math.max(1, root.options.length))
            height: KP.controlHeight
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            readonly property string explanation: index < root.explanations.length ? root.explanations[index] : ""
            Accessible.name: modelData
            Accessible.description: explanation
            ToolTip.text: explanation
            ToolTip.visible: explanation.length > 0 && (hovered || activeFocus || pressed)
            ToolTip.delay: 400
            onClicked: if (!current) root.chosen(index)
            background: Rectangle {
                color: segment.current ? KP.accentWash
                     : segment.hovered ? KP.hoverOverlay : "transparent"
                border.width: 1
                border.color: segment.current ? KP.accent : KP.border
                radius: KP.radiusS
            }
            contentItem: Label {
                text: segment.modelData
                color: segment.current ? KP.textAccent : KP.text
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                font.weight: segment.current ? Font.Medium : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
