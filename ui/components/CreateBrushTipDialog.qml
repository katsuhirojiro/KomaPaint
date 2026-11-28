import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// "Create Brush Tip from Selection" — the one decision point between drawing
// and Brush Studio.
//
// It asks the previous-brush question first, then only exposes sources that
// genuinely contain artwork. The extracted pixels are made by Workspace from
// the real layer/selection data; this dialog never invents a tip.
KpDialog {
    id: dialog
    objectName: "createBrushTipDialog"
    heading: qsTr("Create Brush Tip from Selection")
    bodyWidth: 460

    property var info: ({})
    property bool hasPrevious: false
    property bool previousRealistic: false
    property var previousTips: []
    property string error: ""
    property string levelsStatus: ""
    signal createRequested(bool continuePrevious)
    signal levelsRequested()

    readonly property bool hasSelection: !!info.hasSelection
    readonly property bool canCreate: hasSelection || wholeLayer.checked
    readonly property bool previousUsable: hasPrevious && !previousRealistic
    readonly property string selectedSource: sourceModel.length > 0 && sourceChoice.currentIndex >= 0
        ? sourceModel[sourceChoice.currentIndex].id : ""
    readonly property string selectedCoverage: coverageChoice.currentIndex === 1 ? "alpha"
        : coverageChoice.currentIndex === 2 ? "luminance" : "colour"
    readonly property bool useSelection: hasSelection && !wholeLayer.checked
    readonly property bool addTipAsNew: addAsNew.checked || !previousUsable
    readonly property string replaceTipId: previousUsable && !addTipAsNew && replaceTarget.currentIndex >= 0
        && replaceTarget.currentIndex < previousTips.length
        ? previousTips[replaceTarget.currentIndex].id : ""

    function sourceRows() {
        const rows = []
        if (info.currentLayer)
            rows.push({ id: "current", label: qsTr("Current layer"),
                        detail: info.currentLayerName ? qsTr("Layer: %1").arg(info.currentLayerName) : "" })
        if (info.selectedLayers)
            rows.push({ id: "selected", label: qsTr("Selected layers (%1)").arg(info.selectedLayerCount),
                        detail: qsTr("The layers selected in the Layers panel.") })
        if (info.mergedVisible)
            rows.push({ id: "merged", label: qsTr("Merged visible artwork"),
                        detail: qsTr("What the canvas shows, with layer order, opacity and blending.") })
        return rows
    }

    function resetChoices() {
        error = ""
        levelsStatus = ""
        sourceModel = sourceRows()
        sourceChoice.currentIndex = 0
        coverageChoice.currentIndex = 0
        addAsNew.checked = true
        wholeLayer.checked = !hasSelection
        if (previousTips.length > 0) replaceTarget.currentIndex = 0
        targetRow.visible = previousUsable && !addAsNew.checked && previousTips.length > 0
    }

    property var sourceModel: []

    onOpened: resetChoices()

    body: [
        ColumnLayout {
            Layout.fillWidth: true
            spacing: KP.gapS

            Text {
                Layout.fillWidth: true
                text: dialog.hasPrevious
                      ? qsTr("Would you like to work on your previous brush?")
                      : qsTr("No previous brush draft is open. Create a new brush from this artwork.")
                color: KP.text
                font.family: KP.fontUi
                font.pixelSize: KP.fontM
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.hasPrevious
                text: dialog.previousRealistic
                      ? qsTr("The previous brush uses Realistic painting, which cannot paint a bitmap tip. It will be kept, and this tip can start a new Digital brush.")
                      : qsTr("Continuing keeps every existing tip, group, texture, dynamic and unsaved edit. The selected artwork is added or replaces one tip, as you choose below.")
                color: dialog.previousRealistic ? KP.warning : KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: !dialog.hasSelection
                text: dialog.info.reason && dialog.info.reason.length > 0
                      ? dialog.info.reason
                      : qsTr("No area is selected. Select an area with a lasso or selection tool, or use the current layer's contents below.")
                color: KP.warning
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
            CheckBox {
                id: wholeLayer
                objectName: "brushTipWholeLayer"
                visible: !dialog.hasSelection && !!dialog.info.currentLayer
                text: qsTr("Use the current layer's whole contents")
                Accessible.name: text
                Accessible.description: qsTr("Uses all drawn pixels on the active layer. Empty margins are trimmed so the tip is not mostly transparency.")
                onToggled: {
                    dialog.error = ""
                    dialog.levelsStatus = ""
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Artwork to use")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            ComboBox {
                id: sourceChoice
                objectName: "brushTipSourceChoice"
                Layout.fillWidth: true
                model: dialog.sourceModel
                textRole: "label"
                valueRole: "id"
                Accessible.name: qsTr("Artwork source")
                Accessible.description: qsTr("The layer or visible artwork the brush tip is made from.")
                ToolTip.text: Accessible.description
                ToolTip.visible: hovered || activeFocus
                ToolTip.delay: 500
                onActivated: {
                    dialog.error = ""
                    dialog.levelsStatus = ""
                }
            }
            Text {
                Layout.fillWidth: true
                visible: sourceChoice.currentIndex >= 0 && sourceChoice.currentIndex < dialog.sourceModel.length
                text: sourceChoice.currentIndex >= 0 && sourceChoice.currentIndex < dialog.sourceModel.length
                      ? dialog.sourceModel[sourceChoice.currentIndex].detail : ""
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("What the tip keeps")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            ComboBox {
                id: coverageChoice
                objectName: "brushTipCoverageChoice"
                Layout.fillWidth: true
                model: [
                    { id: "colour", label: qsTr("Full colour") },
                    { id: "alpha", label: qsTr("Transparency outline") },
                    { id: "luminance", label: qsTr("Darkness becomes the tip") }
                ]
                textRole: "label"
                valueRole: "id"
                Accessible.name: qsTr("What the tip keeps")
                Accessible.description: qsTr("Full colour keeps the painting. Transparency outline keeps the shape and transparency without colour. Darkness turns dark pixels into opaque coverage.")
                ToolTip.text: Accessible.description
                ToolTip.visible: hovered || activeFocus
                ToolTip.delay: 500
                onActivated: {
                    dialog.error = ""
                    dialog.levelsStatus = ""
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: KP.gapS
                KpButton {
                    objectName: "brushTipLevelsButton"
                    label: qsTr("Adjust Levels…")
                    variant: "ghost"
                    enabled: dialog.canCreate && dialog.selectedCoverage === "colour"
                    onClicked: dialog.levelsRequested()
                }
                Text {
                    Layout.fillWidth: true
                    text: dialog.selectedCoverage === "colour"
                          ? (dialog.levelsStatus.length > 0
                             ? dialog.levelsStatus
                             : qsTr("Adjust the extracted artwork's tones before it becomes a tip."))
                          : qsTr("Levels applies to Full colour tips. Transparency and Darkness tips contain no colour to adjust.")
                    color: dialog.levelsStatus.length > 0 ? KP.textMuted : KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                }
            }

            CheckBox {
                id: addAsNew
                objectName: "brushTipAddAsNew"
                visible: dialog.previousUsable
                text: qsTr("Add as another tip")
                checked: true
                Accessible.name: text
                Accessible.description: qsTr("Keeps every existing tip and adds this one. Turn off to replace one existing tip.")
                ToolTip.text: Accessible.description
                ToolTip.visible: hovered || activeFocus
                ToolTip.delay: 500
                onToggled: {
                    targetRow.visible = dialog.previousUsable && !checked && dialog.previousTips.length > 0
                    dialog.error = ""
                    dialog.levelsStatus = ""
                }
            }
            RowLayout {
                id: targetRow
                Layout.fillWidth: true
                visible: dialog.previousUsable && !addAsNew.checked && dialog.previousTips.length > 0
                spacing: KP.gapS
                Text {
                    text: qsTr("Replace")
                    color: KP.textMuted
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                }
                ComboBox {
                    id: replaceTarget
                    objectName: "brushTipReplaceTarget"
                    Layout.fillWidth: true
                    model: dialog.previousTips
                    textRole: "name"
                    valueRole: "id"
                    Accessible.name: qsTr("Tip to replace")
                    Accessible.description: qsTr("The existing tip whose pixels will be replaced. Its position and settings stay.")
                    ToolTip.text: Accessible.description
                    ToolTip.visible: hovered || activeFocus
                    ToolTip.delay: 500
                }
            }

            Text {
                Layout.fillWidth: true
                visible: dialog.error.length > 0
                text: dialog.error
                color: KP.error
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("The illustration and its selection are not changed. You can keep drawing while Brush Studio is open.")
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
                wrapMode: Text.WordWrap
            }
        }
    ]

    actions: [
        KpButton {
            objectName: "brushTipCancelButton"
            label: qsTr("Cancel")
            onClicked: dialog.reject()
        },
        KpButton {
            objectName: "brushTipNewButton"
            label: qsTr("Create a new brush")
            variant: dialog.previousUsable ? "ghost" : "primary"
            enabled: dialog.canCreate
            onClicked: dialog.createRequested(false)
        },
        KpButton {
            objectName: "brushTipContinueButton"
            visible: dialog.previousUsable
            label: qsTr("Continue previous brush")
            variant: "primary"
            enabled: dialog.canCreate
            onClicked: dialog.createRequested(true)
        }
    ]
}
