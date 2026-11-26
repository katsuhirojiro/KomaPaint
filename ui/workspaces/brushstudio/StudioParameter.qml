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
    readonly property string explanation: readback.reason.length > 0
        ? spec.description + "\n\n" + readback.reason
        : spec.description
    spacing: 0
    enabled: true
    opacity: 1

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
            Accessible.name: row.spec.label
            Accessible.description: row.explanation
            ToolTip.visible: hover.hovered
            ToolTip.delay: 500
            ToolTip.text: row.explanation
            HoverHandler { id: hover }
        }
        // The explanation is reachable three ways: hover, keyboard focus and
        // a tap. It is never hover-only.
        KpButton {
            objectName: "brushStudioInfo_" + row.spec.key
            small: true
            glyph: "info"
            variant: "quiet"
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            tip: row.explanation
            Accessible.name: qsTr("Explain %1").arg(row.spec.label)
            Accessible.description: row.explanation
            focusPolicy: Qt.StrongFocus
            onClicked: infoPopup.opened ? infoPopup.close() : infoPopup.open()
            onActiveFocusChanged: if (activeFocus) infoPopup.open()
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
            tip: row.readback.supported
                ? qsTr("Reset %1 to its registry default").arg(row.spec.label)
                : row.readback.reason
            enabled: row.readback.resetEnabled
            opacity: enabled ? 1 : 0
            onClicked: row.draft.setParameterValue(row.spec.key, row.spec.default)
        }
    }

    Text {
        objectName: "brushStudioReason_" + row.spec.key
        Layout.fillWidth: true
        visible: row.readback.reason.length > 0 && (row.readback.available || row.readback.changed)
        text: row.readback.reason
        color: KP.textSubtle
        font.family: KP.fontUi
        font.pixelSize: KP.fontXS
        wrapMode: Text.WordWrap
        leftPadding: 0
    }

    Component {
        id: numberEditor
        KpTextField {
            id: numberField
            objectName: "brushStudioValue_" + row.spec.key
            text: row.readback.text
            alignment: TextInput.AlignRight
            Accessible.name: row.spec.label
            enabled: row.readback.supported
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
            enabled: row.readback.supported
            currentIndex: row.readback.supported ? row.readback.value : -1
            displayText: row.readback.supported ? currentText : qsTr("Unavailable")
            onActivated: function(index) {
                row.draft.setParameterValue(row.spec.key, index)
                currentIndex = Qt.binding(function() { return row.readback.supported ? row.readback.value : -1 })
            }
        }
    }
    Slider {
        id: rail
        objectName: "brushStudioRail_" + row.spec.key
        visible: row.spec.editor === "slider"
        enabled: row.readback.supported
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

    Popup {
        id: infoPopup
        parent: row
        x: 0
        y: Math.min(label.height + 2, row.height)
        width: Math.min(300, Math.max(200, row.width))
        padding: KP.gapM
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: KP.paneBgAlt
            radius: KP.radiusS
            border.width: KP.hairlineWidth
            border.color: KP.border
        }
        contentItem: Text {
            text: row.explanation
            textFormat: Text.PlainText
            color: KP.text
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.WordWrap
        }
    }
}
