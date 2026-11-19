import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

ColumnLayout {
    id: row
    required property BrushStudioModel draft
    required property var spec
    required property var readback
    readonly property bool choice: spec.editor === "choice"
    spacing: 0
    enabled: readback.available
    opacity: enabled ? 1 : KP.disabledOpacity

    RowLayout {
        Layout.fillWidth: true
        spacing: KP.gapS
        Text {
            id: label
            Layout.fillWidth: true
            text: row.spec.label
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            font.weight: row.readback.changed ? Font.DemiBold : Font.Normal
            ToolTip.visible: hover.hovered
            ToolTip.delay: 600
            ToolTip.text: row.spec.description
            HoverHandler { id: hover }
        }
        Loader {
            Layout.preferredWidth: row.choice ? 104 : row.spec.editor === "number" ? 116 : 74
            sourceComponent: row.choice ? choiceEditor : numberEditor
        }
        Text {
            visible: row.spec.unit.length > 0
            text: row.spec.unit
            color: KP.textSubtle
            font.family: KP.fontUi
            font.pixelSize: KP.fontXS
        }
        KpButton {
            objectName: "brushStudioReset_" + row.spec.key
            small: true
            glyph: "reset"
            tip: qsTr("Reset %1 to its registry default").arg(row.spec.label)
            enabled: row.readback.changed
            opacity: enabled ? 1 : 0
            onClicked: row.draft.setParameterValue(row.spec.key, row.spec.default)
        }
    }

    Component {
        id: numberEditor
        KpTextField {
            id: numberField
            objectName: "brushStudioValue_" + row.spec.key
            text: row.readback.text
            alignment: TextInput.AlignRight
            Accessible.name: row.spec.label
            onSubmitted: function(typed) {
                if (typed !== row.readback.text) row.draft.setParameterText(row.spec.key, typed)
                numberField.text = Qt.binding(function() { return row.readback.text })
            }
        }
    }
    Component {
        id: choiceEditor
        StudioChoice {
            objectName: "brushStudioValue_" + row.spec.key
            model: row.spec.choices
            Accessible.name: row.spec.label
            currentIndex: row.enabled ? row.readback.value : -1
            displayText: row.enabled ? currentText : qsTr("Not applicable")
            onActivated: function(index) {
                row.draft.setParameterValue(row.spec.key, index)
                currentIndex = Qt.binding(function() { return row.enabled ? row.readback.value : -1 })
            }
        }
    }
    Slider {
        id: rail
        objectName: "brushStudioRail_" + row.spec.key
        visible: row.spec.editor === "slider"
        Layout.fillWidth: true
        implicitHeight: 16
        leftPadding: 4
        rightPadding: 4
        from: row.spec.min
        to: row.spec.max
        stepSize: Math.min(1, (to - from) / 100)
        snapMode: Slider.SnapOnRelease
        live: false
        value: row.readback.value
        Accessible.name: row.spec.label
        function submit() {
            if (value !== row.readback.value) row.draft.setParameterValue(row.spec.key, value)
            value = Qt.binding(function() { return row.readback.value })
        }
        // A drag is one model transaction. Keyboard changes still commit
        // immediately and the model remains authoritative after a refusal.
        onPressedChanged: if (!pressed) submit()
        onMoved: if (!pressed) submit()
        background: Rectangle {
            x: rail.leftPadding
            y: (rail.height - height) / 2
            width: rail.availableWidth
            height: 4
            radius: 2
            color: KP.inputBg
            border.width: KP.hairlineWidth
            border.color: rail.activeFocus ? KP.focusRing : KP.hairline
            Rectangle {
                width: rail.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: KP.accentSoft
            }
        }
        handle: Rectangle {
            x: rail.leftPadding + rail.visualPosition * (rail.availableWidth - width)
            y: (rail.height - height) / 2
            implicitWidth: 9
            implicitHeight: 9
            radius: width / 2
            color: KP.accent
        }
    }
}
