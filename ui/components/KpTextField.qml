import QtQuick
import QtQuick.Controls
import "../themes"

// A single-line text entry with the same well as KpNumberField.
// Used where the value is not a number: a hex colour, a layout name, a filter.
Item {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property int alignment: TextInput.AlignLeft
    property alias acceptableInput: input.acceptableInput
    signal edited(string text)
    signal submitted(string text)

    implicitWidth: 120
    implicitHeight: KP.controlHeight
    opacity: enabled ? 1 : KP.disabledOpacity

    Rectangle {
        id: well
        anchors.fill: parent
        radius: KP.radiusS
        color: input.activeFocus ? KP.inputBgHover : KP.inputBg
        border.width: 1
        border.color: !root.enabled ? KP.border : input.activeFocus ? KP.accent : KP.border

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 5
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: root.alignment
            color: KP.text
            selectionColor: KP.selection
            selectedTextColor: KP.textOnAccent
            font.pixelSize: KP.fontS + 1
            font.family: KP.fontMono
            clip: true
            selectByMouse: true

            onActiveFocusChanged: if (activeFocus) selectAll()
            onTextChanged: root.edited(text)
            onAccepted: { focus = false; root.submitted(text) }
            onEditingFinished: root.submitted(text)
        }

        Text {
            anchors.fill: input
            anchors.leftMargin: 6
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: root.alignment
            visible: input.text.length === 0 && !input.activeFocus
            text: root.placeholder
            color: KP.textSubtle
            font.pixelSize: KP.fontS + 1
            elide: Text.ElideRight
        }
    }
}
