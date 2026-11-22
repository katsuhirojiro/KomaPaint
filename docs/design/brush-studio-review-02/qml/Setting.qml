import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

Item {
    id: root
    property string caption
    property string setting
    property string unit: "%"
    property int minimum: 0
    property int maximum: 100
    property int amount: 0
    property int captionWidth: 106
    signal edited(int next)
    implicitHeight: 31
    implicitWidth: 205
    function commit(value) { if (Number.isFinite(value)) edited(Math.round(Math.max(minimum, Math.min(maximum, value)))) }
    RowLayout {
        anchors.fill: parent
        spacing: 7
        LabelText { text: root.caption; Layout.preferredWidth: 100; Layout.minimumWidth: 70 }
        C.Slider {
            id: slider
            objectName: root.setting + "Slider"
            Layout.fillWidth: true; Layout.minimumWidth: 48; Layout.preferredHeight: 30
            from: root.minimum; to: root.maximum; value: root.amount
            stepSize: 1
            leftPadding: 4; rightPadding: 4
            Accessible.name: root.caption
            onMoved: root.commit(value)
            background: Rectangle {
                x: slider.leftPadding; y: slider.height/2 - 2
                width: slider.availableWidth; height: 4; radius: 1; color: Theme.field
                Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 1; color: "#85888b" }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition*(slider.availableWidth-width)
                y: slider.height/2-height/2
                implicitWidth: 7; implicitHeight: 13; radius: 1
                color: slider.pressed || slider.visualFocus ? Theme.focus : "#b9b9b9"
                border.color: slider.visualFocus ? Theme.text : Theme.field
            }
        }
        EditField {
            id: number
            objectName: root.setting + "Value"
            Layout.preferredWidth: 43; Layout.maximumWidth: 43; Layout.preferredHeight: 23
            horizontalAlignment: TextInput.AlignRight
            Accessible.name: root.caption + " value"
            text: String(root.amount)
            validator: IntValidator { bottom: root.minimum; top: root.maximum }
            onEditingFinished: {
                if (text.length) root.commit(Number(text))
                text = Qt.binding(() => String(root.amount))
            }
        }
        LabelText { text: root.unit; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 16 }
    }
}
