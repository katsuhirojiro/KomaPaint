import QtQuick
import QtQuick.Controls.Basic as C
import "."

Item {
    id: row
    property var info: ({})
    property real localValue: info.value !== undefined ? Number(info.value) : 0
    property string localText: info.kind === "toggle" ? (Number(info.value) ? "On" : "Off") : (info.valueText || "")
    property string curveBinding: info.bindingId || ""
    property bool curveOpen: false
    property bool editingValue: false
    property bool hovered: false
    objectName: "setting_" + (info.key || "")
    implicitHeight: body.implicitHeight + 6

    HoverHandler {
        onHoveredChanged: {
            row.hovered = hovered
            if (hovered && row.info.key) studio.focusSetting(row.info.key)
        }
    }

    Column {
        id: body
        width: parent.width
        spacing: 4

        Row {
            width: parent.width
            height: 22
            spacing: 5
            Text {
                width: parent.width - 118
                text: row.info.label || ""
                color: Theme.text
                font.family: Theme.ui
                font.pixelSize: Theme.body
                font.weight: Font.Normal
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: 1; height: 1 }
            Rectangle {
                id: valuePill
                height: 20
                width: Math.max(42, valueLabel.implicitWidth + 14)
                radius: 2
                color: row.editingValue || valueMouse.containsMouse ? Theme.elevated : "transparent"
                Text {
                    id: valueLabel
                    visible: !row.editingValue
                    anchors.centerIn: parent
                    text: row.localText
                    color: Theme.secondary
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    font.features: {"tnum": 1}
                }
                TextInput {
                    id: input
                    visible: row.editingValue
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    text: row.localText.replace("%", "").replace("°", "").replace("px", "").replace("×", "").trim()
                    color: Theme.text
                    selectionColor: Theme.raised
                    selectedTextColor: Theme.text
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    font.features: {"tnum": 1}
                    selectByMouse: true
                    onEditingFinished: {
                        studio.setSettingText(row.info.key, text)
                        row.editingValue = false
                    }
                    Keys.onEscapePressed: { row.editingValue = false; input.focus = false }
                }
                MouseArea {
                    id: valueMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !row.editingValue
                    cursorShape: Qt.IBeamCursor
                    onClicked: {
                        if (row.info.kind === "slider") {
                            row.editingValue = true
                            input.forceActiveFocus()
                            input.selectAll()
                        }
                    }
                }
            }
            Text {
                visible: row.info.badge === "ORGANISATION ONLY" || row.info.badge === "DEVICE ONLY"
                text: row.info.badge === "DEVICE ONLY" ? "DEV" : "ORG"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 7
                font.letterSpacing: 0.7
                anchors.verticalCenter: parent.verticalCenter
            }
            LineIcon {
                visible: row.info.overridden === true
                kind: "reset"
                width: 12; height: 12
                tint: Theme.secondary
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -3
                    cursorShape: Qt.PointingHandCursor
                    onClicked: studio.resetSetting(row.info.key)
                }
            }
            LineIcon {
                kind: "pin"
                width: 13; height: 13
                tint: row.info.pinned ? Theme.text : Theme.meta
                opacity: row.info.pinned || row.hovered ? 1 : 0.35
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -3
                    cursorShape: Qt.PointingHandCursor
                    onClicked: studio.togglePin(row.info.key)
                }
            }
            LineIcon {
                visible: row.info.kind === "slider"
                kind: "dynamics"
                width: 13; height: 13
                tint: row.curveOpen ? Theme.text : (row.info.bindingId ? Theme.secondary : Theme.meta)
                anchors.verticalCenter: parent.verticalCenter
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -3
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!row.curveBinding.length)
                            row.curveBinding = studio.ensureBinding(row.info.key)
                        else
                            studio.selectBinding(row.curveBinding)
                        row.curveOpen = !row.curveOpen
                    }
                }
            }
        }

        Item {
            width: parent.width
            height: row.info.kind === "toggle" ? 18 : (row.info.kind === "choice" ? 24 : 24)
            MiniSlider {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2
                visible: row.info.kind === "slider"
                minimum: row.info.minimum !== undefined ? row.info.minimum : 0
                maximum: row.info.maximum !== undefined ? row.info.maximum : 100
                value: row.localValue
                onPressed: studio.beginEdit()
                onMoved: function(value) {
                    var step = row.info.step || 1
                    var snapped = Math.round(value / step) * step
                    row.localValue = snapped
                    var suffix = row.info.unit === "°" ? "°" : (row.info.unit === "%" ? "%" : (row.info.unit === "px" ? " px" : (row.info.unit === "×" ? "×" : "")))
                    var text = (step < 1 ? snapped.toFixed(1) : String(Math.round(snapped))) + suffix
                    row.localText = text
                    studio.setSetting(row.info.key, snapped)
                }
                onReleased: studio.endEdit()
            }

            Rectangle {
                visible: row.info.kind === "toggle"
                width: 34; height: 18; radius: 9
                color: row.localValue ? Theme.toggleOn : Theme.toggleOff
                border.width: 1
                border.color: row.localValue ? "transparent" : Theme.hairline
                Rectangle {
                    width: 12; height: 12; radius: 6
                    y: 3
                    x: row.localValue ? 19 : 3
                    color: Theme.toggleKnob
                    Behavior on x { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var next = row.localValue ? 0 : 1
                        row.localValue = next
                        row.localText = next ? "On" : "Off"
                        studio.setSetting(row.info.key, next)
                    }
                }
            }

            SelectField {
                visible: row.info.kind === "choice"
                width: parent.width
                height: 24
                options: row.info.choices || []
                current: String(row.info.value || "")
                onSelected: function(value) { studio.setChoice(row.info.key, value) }
            }
        }

        Item {
            id: curveBlock
            width: parent.width
            height: row.curveOpen ? 186 : 0
            visible: row.curveOpen
            CurveEditor {
                anchors.fill: parent
                bindingId: row.curveBinding
                settingKey: row.info.key
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }
}
