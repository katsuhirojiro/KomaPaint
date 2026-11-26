import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

Flickable {
    id: exposure
    required property BrushStudioModel draft
    readonly property var metadata: draft.exposureState
    // A viewport layer keeps software Shape nodes inside the scroll clip.
    layer.enabled: GraphicsInfo.api === GraphicsInfo.Software
    clip: true
    contentWidth: width
    contentHeight: form.implicitHeight + 2 * KP.panelInset
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: KpScrollBar { opacity: size < 1 ? 1 : 0 }

    Rectangle { parent: exposure; anchors.fill: parent; z: -1; color: KP.paneBg }

    // Use the viewport width, not a column layout's shared minimum. A
    // wide translated label or fallback font must not move the row actions
    // into the scrollbar or outside the window.
    Column {
        id: form
        x: KP.panelInset
        y: KP.panelInset
        width: Math.max(0, exposure.width - 2 * KP.panelInset)
        spacing: KP.gapM
        Text {
            width: form.width
            text: qsTr("Choose which properties the brush should expose by default. This metadata is saved in the .kb; it does not change personal Tool Controls or app-wide stroke controls.")
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.Wrap
        }
        StudioChoice {
            objectName: "brushStudioExposeChoice"
            width: form.width
            model: exposure.draft.exposureCandidates
            textRole: "label"
            valueRole: "key"
            enabled: count > 0
            currentIndex: -1
            displayText: qsTr("Expose a property…")
            Accessible.name: displayText
            onActivated: function(index) {
                if (index < 0) return
                const key = valueAt(index)
                // The selected entry is about to leave the candidates model.
                popup.close()
                exposure.draft.expose(key)
                currentIndex = -1
            }
        }
        Repeater {
            model: exposure.metadata.keys
            delegate: RowLayout {
                id: exposedRow
                required property string modelData
                readonly property var entry: exposure.metadata.entries[modelData] || {}
                width: form.width
                spacing: KP.gapS
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: KP.gapXS
                    KpTextField {
                        id: displayName
                        objectName: "brushStudioExposureLabel_" + exposedRow.modelData
                        Layout.fillWidth: true
                        placeholder: exposedRow.entry.label || ""
                        text: exposedRow.entry.customLabel ? exposedRow.entry.label : ""
                        Accessible.name: qsTr("Display label for %1").arg(exposedRow.modelData)
                        onSubmitted: function(typed) {
                            if (exposedRow.entry.key && typed !== (exposedRow.entry.customLabel ? exposedRow.entry.label : ""))
                                exposure.draft.setExposure(exposedRow.modelData, { label: typed })
                            displayName.text = Qt.binding(function() { return exposedRow.entry.customLabel ? exposedRow.entry.label : "" })
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: exposedRow.entry.key
                            ? (exposedRow.entry.supported
                                ? qsTr("%1 · %2").arg(exposedRow.entry.section).arg(exposedRow.entry.control)
                                : exposedRow.entry.supportReason)
                            : ""
                        textFormat: Text.PlainText
                        wrapMode: exposedRow.entry.supported ? Text.NoWrap : Text.WordWrap
                        elide: exposedRow.entry.supported ? Text.ElideRight : Text.ElideNone
                        color: exposedRow.entry.supported ? KP.textSubtle : KP.warning
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontXS
                    }
                }
                KpButton {
                    objectName: "brushStudioUnexpose_" + exposedRow.modelData
                    small: true
                    glyph: "minus"
                    tip: qsTr("Remove %1 from creator exposure").arg(exposedRow.entry.label || exposedRow.modelData)
                    onClicked: exposure.draft.unexpose(exposedRow.modelData)
                }
            }
        }
        KpButton {
            objectName: "brushStudioRestoreExposure"
            width: Math.min(implicitWidth, form.width)
            clip: true
            label: qsTr("Restore defaults")
            glyph: "reset"
            variant: "solid"
            tip: qsTr("Restore the default creator list without changing any brush values")
            onClicked: exposure.draft.resetExposure()
        }
    }
}
