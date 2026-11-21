import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

ColumnLayout {
    id: bar
    required property BrushStudioModel draft
    required property bool previewExpanded
    signal importRequested
    signal exportRequested
    signal saveRequested
    signal closeRequested
    signal previewToggled
    spacing: 0
    readonly property bool showCommands: width >= 860
    readonly property bool showDestination: width >= 1220

    Action { id: newBrush; text: qsTr("New brush"); onTriggered: bar.draft.newDraft() }
    Action { id: duplicate; text: qsTr("Duplicate draft"); onTriggered: bar.draft.duplicateDraft() }
    Action { id: openBrush; text: qsTr("Import .kb…"); onTriggered: bar.importRequested() }
    Action { id: exportBrush; text: qsTr("Export .kb copy…"); onTriggered: bar.exportRequested() }
    Action { id: saveBrush; text: qsTr("Save .kb…"); onTriggered: bar.saveRequested() }
    Action { id: closeStudio; text: qsTr("Close Brush Studio"); onTriggered: bar.closeRequested() }

    component Rule: Rectangle {
        implicitHeight: KP.controlHeightSmall
        implicitWidth: KP.hairlineWidth
        color: KP.hairline
    }

    MenuBar {
        objectName: "brushStudioMenuBar"
        Layout.fillWidth: true
        Layout.preferredHeight: KP.menubarHeight
        padding: 0
        leftPadding: KP.gapS
        background: Rectangle {
            color: KP.menubarBg
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: KP.hairlineWidth
                color: KP.hairline
            }
        }
        delegate: MenuBarItem {
            id: entry
            leftPadding: KP.gapM
            rightPadding: KP.gapM
            contentItem: Text {
                text: entry.text
                color: entry.highlighted ? KP.text : KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontM
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: entry.highlighted ? KP.hoverOverlayStrong : "transparent"
            }
        }
        KpMenu {
            title: qsTr("File")
            KpMenuItem { action: newBrush }
            KpMenuItem { action: openBrush }
            KpMenuItem { action: saveBrush }
            KpMenuItem { action: exportBrush }
            KpMenuSeparator {}
            KpMenuItem { text: qsTr("Save to Library (unavailable)"); enabled: false }
        }
        KpMenu {
            title: qsTr("Edit")
            KpMenuItem { text: qsTr("Undo"); enabled: bar.draft.canUndo; onTriggered: bar.draft.undo() }
            KpMenuItem { text: qsTr("Redo"); enabled: bar.draft.canRedo; onTriggered: bar.draft.redo() }
        }
        KpMenu {
            title: qsTr("View")
            KpMenuItem {
                text: bar.previewExpanded ? qsTr("Collapse Live Preview") : qsTr("Expand Live Preview")
                onTriggered: bar.previewToggled()
            }
        }
        KpMenu {
            title: qsTr("Brush")
            KpMenuItem { action: duplicate }
            KpMenuItem { text: qsTr("Digital"); checkable: true; checked: bar.draft.engine === "digital"; onTriggered: bar.draft.engine = "digital" }
            KpMenuItem { text: qsTr("Realistic"); checkable: true; checked: bar.draft.engine === "realistic"; onTriggered: bar.draft.engine = "realistic" }
        }
        KpMenu {
            title: qsTr("Window")
            KpMenuItem { action: closeStudio }
        }
    }

    Rectangle {
        objectName: "brushStudioCommandBar"
        Layout.fillWidth: true
        implicitHeight: KP.commandBarHeight
        color: KP.toolbarBg
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: KP.hairlineWidth
            color: KP.hairline
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: KP.gapM
            anchors.rightMargin: KP.gapM
            spacing: KP.gapM
            KpGlyph { name: "brush" }
            KpTextField {
                id: nameField
                objectName: "brushStudioDraftName"
                Layout.fillWidth: !bar.showCommands
                Layout.minimumWidth: 96
                Layout.preferredWidth: 180
                Layout.maximumWidth: 240
                text: bar.draft.name
                placeholder: qsTr("Brush name")
                onSubmitted: function(name) {
                    bar.draft.name = name
                    nameField.text = Qt.binding(function() { return bar.draft.name })
                }
            }
            SegmentedChoice {
                objectName: "brushStudioEngineChoice"
                Layout.preferredWidth: 158
                options: [qsTr("Digital"), qsTr("Realistic")]
                currentIndex: bar.draft.engine === "realistic" ? 1 : 0
                onChosen: function(index) { bar.draft.engine = index === 1 ? "realistic" : "digital" }
            }
            Rule {}
            Row {
                visible: bar.showCommands
                spacing: KP.gapXS
                KpButton { objectName: "brushStudioNew"; action: newBrush; glyph: "new"; label: qsTr("New"); tip: qsTr("Start a new draft (undoable)") }
                KpButton { objectName: "brushStudioDuplicate"; action: duplicate; glyph: "duplicate"; label: qsTr("Duplicate"); tip: qsTr("Continue editing a separate copy of this draft") }
                KpButton { objectName: "brushStudioImport"; action: openBrush; glyph: "open"; label: qsTr("Import"); tip: qsTr("Open a KomaPaint .kb file; other formats are unavailable") }
                KpButton { objectName: "brushStudioExport"; action: exportBrush; glyph: "save"; label: qsTr("Export"); tip: qsTr("Write a .kb copy without marking the draft saved") }
            }
            KpButton {
                objectName: "brushStudioBrushActions"
                visible: !bar.showCommands
                glyph: "dots-h"
                tip: qsTr("Brush actions")
                onClicked: actions.popup(this, 0, height)
                KpMenu {
                    id: actions
                    KpMenuItem { action: newBrush }
                    KpMenuItem { action: duplicate }
                    KpMenuItem { action: openBrush }
                    KpMenuItem { action: exportBrush }
                }
            }
            Item { Layout.fillWidth: true; visible: bar.showCommands }
            Text {
                visible: bar.showDestination
                text: qsTr("Saves a local brush file · library unavailable")
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            KpButton {
                objectName: "brushStudioSave"
                action: saveBrush
                glyph: "save"
                label: qsTr("Save .kb…")
                variant: "primary"
                tip: qsTr("Save this draft as .kb and install it if it can paint in Digital mode")
            }
            KpButton {
                objectName: "brushStudioClose"
                action: closeStudio
                glyph: "close"
                label: bar.showCommands ? qsTr("Close") : ""
                variant: "solid"
                tip: qsTr("Close Studio and keep the draft in this session")
            }
        }
    }
}
