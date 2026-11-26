import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import KomaPaint 1.0
import "../components"
import "../themes"
import "brushstudio"

// The native window owns the draft and private canvas. Its library reference
// is used only after a successful explicit Save, not while testing the draft.
Window {
    id: studio
    objectName: "brushStudioWindow"
    title: qsTr("KomaPaint — Brush Studio")
    flags: Qt.Window
    modality: Qt.NonModal
    minimumWidth: 540
    minimumHeight: 400
    width: Math.max(minimumWidth, Math.min(1540, Screen.desktopAvailableWidth - 36))
    height: Math.max(minimumHeight, Math.min(1000, Screen.desktopAvailableHeight - 72))
    color: KP.windowBg

    readonly property bool compact: width < 880
    readonly property bool editingText: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    property int compactPanel: 1
    // -1 follows the available space. A deliberate expand/collapse is remembered
    // for this window, but even expanded the shelf shrinks on short displays.
    property int previewPreference: -1
    readonly property bool previewExpanded: previewPreference < 0 ? height >= 660 : previewPreference === 1
    property string fileNotice: ""
    property string libraryNotice: ""
    property string saveOutcome: ""   // savedInstalled, savedNotInstalled, savedUnsupported, savedOnly, saveFailed
    property var brushLibrary: null
    property var workspace: null
    property var tabletSettings: null
    property color previewInk: "black"

    function saveDraftToLibrary(url) {
        libraryNotice = ""
        fileNotice = ""
        saveOutcome = ""
        if (!brushDraft.saveDraft(url)) {
            saveOutcome = "saveFailed"
            libraryNotice = qsTr("Brush file was not saved: %1").arg(brushDraft.error)
            return false
        }
        if (!brushLibrary) {
            saveOutcome = "savedOnly"
            fileNotice = qsTr("Brush file saved · install it from the Sub-Tool panel")
            return true
        }
        if (workspace) workspace.activeTool = "brush"
        const result = brushLibrary.installNativeBrush(url)
        if (result.installed) {
            saveOutcome = "savedInstalled"
            fileNotice = qsTr("Saved and installed “%1” in Brush → %2 → %3")
                .arg(result.name)
                .arg(brushDraft.engine === "realistic" ? qsTr("Realistic") : qsTr("Digital"))
                .arg(result.tabName)
            // The install already selects the entry; repeat the explicit switch
            // so a just-changed tool/collection cannot leave another tab open.
            brushLibrary.selectTabById(result.tabId)
            brushLibrary.selectById(result.id)
        } else if (result.supported) {
            saveOutcome = "savedNotInstalled"
            fileNotice = qsTr("Brush file saved, but installation failed")
            libraryNotice = qsTr("Nothing was installed: %1").arg(result.error)
        } else {
            saveOutcome = "savedUnsupported"
            fileNotice = qsTr("Brush file saved · the selected engine does not support this brush")
            libraryNotice = qsTr("Nothing was installed: %1").arg(result.error)
        }
        return true
    }

    BrushStudioModel {
        id: brushDraft
        objectName: "brushStudioModel"
        onDocumentChanged: { studio.fileNotice = ""; studio.libraryNotice = ""; studio.saveOutcome = "" }
    }

    function present() {
        if (visibility === Window.Minimized) showNormal()
        else show()
        raise()
        requestActivate()
    }

    function togglePreview() { previewPreference = previewExpanded ? 0 : 1 }

    // When a file shortcut is invoked mid-entry, take focus out of the field
    // first so it submits the same way as a click on the command bar.
    function finishTextEntry() { if (editingText) contentItem.forceActiveFocus() }

    Shortcut { sequences: [StandardKey.New]; onActivated: { studio.finishTextEntry(); brushDraft.newDraft() } }
    Shortcut { sequences: [StandardKey.Open]; onActivated: { studio.finishTextEntry(); openBrush.open() } }
    Shortcut { sequences: [StandardKey.Save]; onActivated: { studio.finishTextEntry(); saveBrush.open() } }
    Shortcut { sequences: [StandardKey.Undo]; enabled: brushDraft.canUndo && !studio.editingText; onActivated: brushDraft.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: brushDraft.canRedo && !studio.editingText; onActivated: brushDraft.redo() }

    FileDialog {
        id: openBrush
        parentWindow: studio
        modality: Qt.WindowModal
        objectName: "brushStudioOpenDialog"
        title: qsTr("Import a KomaPaint brush")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("KomaPaint brushes (*.kb)")]
        onAccepted: if (brushDraft.openDraft(selectedFile)) studio.fileNotice = qsTr("Brush opened from file")
    }
    FileDialog {
        id: saveBrush
        parentWindow: studio
        modality: Qt.WindowModal
        objectName: "brushStudioSaveDialog"
        title: qsTr("Save and install brush")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "kb"
        nameFilters: [qsTr("KomaPaint brushes (*.kb)")]
        onAccepted: studio.saveDraftToLibrary(selectedFile)
    }
    FileDialog {
        id: exportBrush
        parentWindow: studio
        modality: Qt.WindowModal
        objectName: "brushStudioExportDialog"
        title: qsTr("Export a copy of this brush")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "kb"
        nameFilters: [qsTr("KomaPaint brushes (*.kb)")]
        onAccepted: if (brushDraft.exportDraft(selectedFile)) studio.fileNotice = qsTr("Brush copy exported")
    }

    FileDialog {
        id: importTip
        parentWindow: studio
        modality: Qt.WindowModal
        objectName: "brushStudioTipImageDialog"
        title: qsTr("Load a bitmap tip image")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.bmp)")]
        onAccepted: brushDraft.importTipImage(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        StudioToolbar {
            Layout.fillWidth: true
            draft: brushDraft
            previewExpanded: studio.previewExpanded
            onImportRequested: openBrush.open()
            onExportRequested: exportBrush.open()
            onSaveRequested: saveBrush.open()
            onCloseRequested: studio.close()
            onPreviewToggled: studio.togglePreview()
        }
        Rectangle {
            objectName: "brushStudioCompactNavigation"
            visible: studio.compact
            Layout.fillWidth: true
            implicitHeight: KP.tabStripHeight
            color: KP.paneHeaderBg
            SegmentedChoice {
                id: compactNavigation
                objectName: "brushStudioPanelChoice"
                anchors.fill: parent
                anchors.margins: KP.gapS
                options: [qsTr("Components"), qsTr("Editor"), qsTr("Settings")]
                currentIndex: studio.compactPanel
                onChosen: function(index) { studio.compactPanel = index }
            }
        }
        RowLayout {
            objectName: "brushStudioBody"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: KP.hairlineWidth
            StudioComponentRail {
                objectName: "brushStudioComponents"
                visible: !studio.compact || studio.compactPanel === 0
                Layout.fillWidth: studio.compact
                Layout.preferredWidth: studio.compact ? -1 : Math.min(260, Math.round(studio.width * 0.16))
                Layout.fillHeight: true
                draft: brushDraft
            }
            StudioEditor {
                id: editor
                objectName: "brushStudioEditor"
                visible: !studio.compact || studio.compactPanel === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                draft: brushDraft
                onImportImageRequested: importTip.open()
            }
            StudioSettings {
                objectName: "brushStudioSettings"
                visible: !studio.compact || studio.compactPanel === 2
                Layout.fillWidth: studio.compact
                Layout.preferredWidth: studio.compact ? -1 : Math.min(340, Math.round(studio.width * 0.23))
                Layout.fillHeight: true
                draft: brushDraft
                textureMode: editor.mode === "texture"
            }
        }
        Text {
            objectName: "brushStudioRealisticNotice"
            visible: brushDraft.engine === "realistic"
            Layout.fillWidth: true
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.panelInset
            Layout.topMargin: visible ? KP.gapS : 0
            text: qsTr("Realistic brushes preview and paint through the material engine when they use the physical tip. Component marks and Digital-only settings are kept in the file but are not painted by Realistic.")
            textFormat: Text.PlainText
            color: KP.warning
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.Wrap
        }
        Text {
            objectName: "brushStudioError"
            Layout.fillWidth: true
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.panelInset
            Layout.topMargin: visible ? KP.gapS : 0
            Layout.bottomMargin: visible ? KP.gapS : 0
            visible: brushDraft.error.length > 0 || studio.libraryNotice.length > 0
            text: brushDraft.error.length > 0 ? brushDraft.error : studio.libraryNotice
            textFormat: Text.PlainText
            color: KP.error
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        StudioPreview {
            objectName: "brushStudioPreview"
            Layout.fillWidth: true
            draft: brushDraft
            tabletSettings: studio.tabletSettings
            ink: studio.previewInk
            expanded: studio.previewExpanded
            expandedHeight: Math.min(250, Math.max(104, Math.round(studio.height * 0.23)))
            onToggleRequested: studio.togglePreview()
        }
        Rectangle {
            objectName: "brushStudioStatus"
            Layout.fillWidth: true
            implicitHeight: KP.statusbarHeight
            color: KP.statusbarBg
            Rectangle {
                width: parent.width
                height: KP.hairlineWidth
                color: KP.hairline
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.panelInset
                anchors.rightMargin: KP.panelInset
                spacing: KP.gapM
                Text {
                    Layout.fillWidth: true
                    text: studio.compact && studio.fileNotice.length > 0 ? studio.fileNotice : qsTr("Sub-Tool  /  %1  /  %2").arg(brushDraft.engine === "realistic" ? qsTr("Realistic") : qsTr("Digital")).arg(brushDraft.name)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                }
                Text {
                    visible: studio.width >= 1060
                    text: (brushDraft.componentCount === 1 ? qsTr("1 component") : qsTr("%1 components").arg(brushDraft.componentCount)) + " · " + qsTr("%1 hidden").arg(brushDraft.hiddenComponentCount)
                    color: KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                }
                Text {
                    visible: !studio.compact && studio.fileNotice.length > 0 && brushDraft.error.length === 0
                    text: studio.fileNotice
                    color: KP.textMuted
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                }
                Rectangle { width: 5; height: 5; radius: 2.5; color: brushDraft.modified ? KP.warning : KP.success }
                Text {
                    text: brushDraft.modified ? qsTr("Unsaved edits") : qsTr("No unsaved edits")
                    color: KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                }
            }
        }
    }
}
