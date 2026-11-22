import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string label: ""
    property string unit: "%"
    property string hint: ""
    property string keyName: ""
    property real value: 0
    property int low: 0
    property int high: 100
    property bool narrow: false
    signal changed(real next)
    implicitHeight: controls.implicitHeight
    implicitWidth: 225

    ColumnLayout {
        id: controls
        anchors.left: parent.left; anchors.right: parent.right
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Type {
                Layout.fillWidth: true
                text: root.label
                wrapMode: Text.WordWrap
                font.weight: Font.Medium
                font.pixelSize: 13
            }
            Field {
                id: input
                objectName: root.keyName ? root.keyName + "Value" : ""
                Layout.preferredWidth: root.unit.length ? 52 : 48
                implicitHeight: 36
                font.pixelSize: 13
                font.family: Desk.mono
                horizontalAlignment: TextInput.AlignHCenter
                padding: 4
                Accessible.name: root.label + (root.unit.length ? " (" + root.unit + ")" : "")
                validator: IntValidator { bottom: root.low; top: root.high }
                text: Math.round(root.value).toString()
                onEditingFinished: {
                    const val = parseInt(text)
                    if (Number.isFinite(val)) root.changed(Math.max(root.low, Math.min(root.high, val)))
                    text = Qt.binding(()=> Math.round(root.value).toString())
                }
                Keys.onEscapePressed: { text = String(Math.round(root.value)); focus = false }
            }
            Type { visible: root.unit.length > 0; text: root.unit; color: Desk.muted; font.pixelSize: 12; Layout.minimumWidth: 13 }
        }
        Slider {
            id: dial
            objectName: root.keyName ? root.keyName + "Slider" : ""
            Layout.fillWidth: true
            implicitHeight: root.narrow ? 25 : 30
            from: root.low; to: root.high; value: root.value
            stepSize: 1
            padding: 0; leftPadding: 7; rightPadding: 7
            hoverEnabled: true
            Accessible.name: root.label
            onMoved: root.changed(Math.round(value))
            background: Rectangle {
                x: dial.leftPadding; y: dial.topPadding + dial.availableHeight / 2 - height/2
                width: dial.availableWidth; height: 3; radius: 2; color: "#494441"
                Rectangle { width: dial.visualPosition * parent.width; height: parent.height; radius: 2; color: Desk.accent }
            }
            handle: Rectangle {
                x: dial.leftPadding + dial.visualPosition * dial.availableWidth - width/2
                y: dial.topPadding + dial.availableHeight/2 - height/2
                width: dial.pressed ? 15 : 13; height: width; radius: width/2
                color: dial.hovered || dial.pressed || dial.activeFocus ? "#ffbd9c" : Desk.accent
                border.color: dial.activeFocus ? Desk.text : Desk.active
                border.width: dial.activeFocus ? 2 : 1
            }
        }
        Type {
            visible: root.hint.length > 0 && !root.narrow
            text: root.hint
            Layout.fillWidth: true
            font.pixelSize: 12; color: Desk.muted; wrapMode: Text.WordWrap; lineHeight: 1.21
        }
    }
}
