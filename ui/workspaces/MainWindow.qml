import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import "../components"
import "../panels"
import "../themes"

ApplicationWindow {
    id: root

    visible: true
    // Keep "KomaPaint" as the leading token (packaging smoke test relies on it).
    title: App.versionDisplay + " — " + Workspace.document.title + (Workspace.document.modified ? " *" : "")
    width: Settings.windowSize.width
    height: Settings.windowSize.height
    visibility: Settings.windowMaximized ? Window.Maximized : Window.Windowed
    color: KP.windowBg

    property bool allowCloseWithArtwork: false
    function requestNewDocument() {
        newProjectDialog.open()
    }
    // Converts a plain local path (Unix, Windows drive, UNC) into a file:// URL
    // suitable for FileDialog. Qt.resolvedUrl would treat "C:/..." as scheme "c".
    function fileUrlForPath(path) {
        if (!path || path.length === 0) return "";
        if (path.startsWith("file:")) return path;
        if (path.startsWith("/")) return "file://" + path;
        if (/^[A-Za-z]:[\\/]/.test(path)) return "file:///" + path.replace(/\\/g, "/");
        if (path.indexOf("\\\\") === 0) return "file:" + path.replace(/\\/g, "/");
        return path;
    }
    function requestOpenImage() {
        openImageDialog.open()
    }
    function saveImage() {
        if (Workspace.currentFile.length > 0) Workspace.saveToPath(Workspace.currentFile)
        else saveImageDialog.open()
    }
    property string closeAction: ""
    property int closeTabIndex: -1
    property var approvedCloseIds: []
    function requestTabClose(index) {
        if (!Workspace.activateTab(index)) return
        if (!Workspace.document.modified) { Workspace.closeTab(index); return }
        closeAction = "tab"
        closeTabIndex = index
        saveChangesDialog.open()
    }
    function continueApplicationClose() {
        const tabs = Workspace.projectTabs
        for (let i = 0; i < tabs.length; ++i) {
            if (tabs[i].modified && approvedCloseIds.indexOf(tabs[i].id) < 0) {
                if (!Workspace.activateTab(i)) { closeAction = ""; return }
                closeAction = "app"
                closeTabIndex = i
                saveChangesDialog.open()
                return
            }
        }
        if (Workspace.recoveryReady && !Workspace.persistWorkspace(approvedCloseIds)) {
            closeAction = ""
            approvedCloseIds = []
            return
        }
        closeAction = ""
        allowCloseWithArtwork = true
        root.close()
    }
    function closeDecisionComplete(discard) {
        if (closeAction === "tab") {
            Workspace.closeTab(closeTabIndex, discard)
            closeAction = ""
        } else if (closeAction === "app") {
            if (discard) approvedCloseIds = approvedCloseIds.concat([Workspace.projectTabs[closeTabIndex].id])
            continueApplicationClose()
        }
    }
    function saveForClose() {
        if (Workspace.currentFile.length === 0) { saveImageDialog.open(); return }
        if (Workspace.saveToPath(Workspace.currentFile)) closeDecisionComplete(false)
        else { closeAction = ""; approvedCloseIds = [] }
    }
    onClosing: function(event) {
        if (!allowCloseWithArtwork) {
            event.accepted = false
            approvedCloseIds = []
            continueApplicationClose()
        }
    }

    readonly property bool editingText: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    Shortcut { sequence: "Ctrl+Z"; enabled: !root.editingText && Workspace.history.canUndo; onActivated: Workspace.history.undo() }
    Shortcut { sequence: "Ctrl+Y"; enabled: !root.editingText && Workspace.history.canRedo; onActivated: Workspace.history.redo() }
    Shortcut { sequence: "Ctrl+O"; enabled: !root.editingText; onActivated: root.requestOpenImage() }
    Shortcut { sequence: "Ctrl+S"; enabled: !root.editingText; onActivated: root.saveImage() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !root.editingText; onActivated: saveImageDialog.open() }
    Connections {
        target: Workspace.history
        function onErrorOccurred(message) { historyErrorText.text = message; historyErrorDialog.open() }
    }
    Connections {
        target: Workspace
        function onFileErrorOccurred(message) { fileErrorText.text = message; fileErrorDialog.open() }
        function onFileAssociationCompleted(message, ok) { associationResultText.text = message; associationResultDialog.open() }
    }
    Dialog {
        id: historyErrorDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Artwork history")
        standardButtons: Dialog.Ok
        contentItem: Label { id: historyErrorText; color: KP.text; wrapMode: Text.WordWrap; width: 360 }
    }
    Dialog {
        id: fileErrorDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Illustration file")
        standardButtons: Dialog.Ok
        contentItem: Label { id: fileErrorText; color: KP.text; wrapMode: Text.WordWrap; width: 360 }
    }
    Dialog {
        id: associationResultDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("File association")
        standardButtons: Dialog.Ok
        contentItem: Label { id: associationResultText; color: KP.text; wrapMode: Text.WordWrap; width: 360 }
    }
    FileDialog {
        id: openImageDialog
        title: qsTr("Open illustration")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("KomaPaint illustrations (*.koma)"), qsTr("All files (*)")]
        onAccepted: Workspace.openFromPath(selectedFile)
    }
    FileDialog {
        id: saveImageDialog
        title: qsTr("Save illustration")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "koma"
        nameFilters: [qsTr("KomaPaint illustrations (*.koma)"), qsTr("All files (*)")]
        selectedFile: Workspace.currentFile.length > 0
                      ? root.fileUrlForPath(Workspace.currentFile)
                      : Qt.resolvedUrl(Workspace.document.title + ".koma")
        onAccepted: {
            if (Workspace.saveToPath(selectedFile.toString())) {
                if (root.closeAction !== "") root.closeDecisionComplete(false)
            } else { root.closeAction = ""; root.approvedCloseIds = [] }
        }
        onRejected: { root.closeAction = ""; root.approvedCloseIds = [] }
    }

    minimumWidth: 1024
    minimumHeight: 640

    // Persist window size when the user resizes / (un)maximizes.
    onWidthChanged: if (visibility === Window.Windowed)
        Settings.windowSize = Qt.size(width, height)
    onHeightChanged: if (visibility === Window.Windowed)
        Settings.windowSize = Qt.size(width, height)
    onVisibilityChanged: Settings.windowMaximized = (visibility === Window.Maximized)

    function beginRecovery() {
        if (Workspace.recoveryAvailable) recoveryChoiceDialog.open()
        else Workspace.setRecoveryInterval(Number(Settings.value("recovery/intervalMinutes", 10)))
    }
    Component.onCompleted: {
        if (Workspace.recoveryReady) {
            if (!Settings.value("recovery/configured", false)) recoverySettingsDialog.open()
            else beginRecovery()
        }
    }

    // ---- Top-level column: menu bar → toolbar → workspace → status bar ----
    ColumnLayout {
        id: rootLayout
        anchors.fill: parent
        spacing: 0

        // --------------------------------------------------------------- Menu
        MenuBar {
            id: menuBar
            Layout.fillWidth: true
            Layout.preferredHeight: KP.menubarHeight
            background: Rectangle {
                color: KP.menubarBg
                border.color: KP.border
                border.width: 1
            }
            delegate: MenuBarItem {
                id: mbi
                contentItem: Label {
                    text: mbi.text
                    color: mbi.highlighted ? "#ffffff" : KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideNone
                }
                background: Rectangle {
                    color: mbi.highlighted ? KP.selection : "transparent"
                    implicitHeight: KP.menubarHeight
                }
                padding: 0
                leftPadding: 10
                rightPadding: 10
            }

            Menu {
                title: qsTr("&File")
                MenuItem {
                    text: qsTr("&New...")
                    onTriggered: root.requestNewDocument()
                }
                MenuItem {
                    text: qsTr("&Open...\tCtrl+O")
                    onTriggered: root.requestOpenImage()
                }
                MenuItem {
                    text: qsTr("&Save\tCtrl+S")
                    onTriggered: root.saveImage()
                }
                MenuItem {
                    text: qsTr("Save &As...\tCtrl+Shift+S")
                    onTriggered: saveImageDialog.open()
                }
                MenuSeparator {
                }
                MenuItem {
                    text: qsTr("Export...")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Import...")
                    enabled: false
                }
                MenuSeparator {
                }
                MenuItem {
                    text: qsTr("Recovery settings...")
                    onTriggered: recoverySettingsDialog.open()
                }
                MenuItem {
                    text: qsTr("Associate &.koma Files...")
                    onTriggered: Workspace.associateWithKomaFiles()
                }
                MenuSeparator {
                }
                MenuItem {
                    text: qsTr("E&xit")
                    onTriggered: root.close()
                }
            }
            Menu {
                title: qsTr("&Edit")
                MenuItem {
                    text: qsTr("&Undo")
                    enabled: Workspace.history.canUndo
                    onTriggered: Workspace.history.undo()
                }
                MenuItem {
                    text: qsTr("&Redo")
                    enabled: Workspace.history.canRedo
                    onTriggered: Workspace.history.redo()
                }
                MenuSeparator { }
                MenuItem {
                    text: qsTr("Clear Artwork History...")
                    enabled: Workspace.history.canUndo || Workspace.history.canRedo
                    onTriggered: clearHistoryDialog.open()
                }
                MenuSeparator {
                }
                MenuItem {
                    text: qsTr("Cut")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Copy")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Paste")
                    enabled: false
                }
                MenuSeparator {
                }
                MenuItem {
                    text: qsTr("Preferences...")
                    enabled: false
                }
            }
            Menu {
                title: qsTr("&View")
                MenuItem {
                    action: Action {
                        text: qsTr("Zoom &In")
                        shortcut: "Ctrl++"
                        onTriggered: Workspace.zoomIn()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Zoom &Out")
                        shortcut: "Ctrl+-"
                        onTriggered: Workspace.zoomOut()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("&Actual Pixels")
                        shortcut: "Ctrl+1"
                        onTriggered: Workspace.resetZoom()
                    }
                }
                MenuItem {
                    action: Action {
                        text: qsTr("Fit to Window")
                        shortcut: "Ctrl+0"
                        onTriggered: canvasArea.fitCanvas()
                    }
                }
                MenuSeparator {
                }
                Menu {
                    title: qsTr("&Panels")
                    MenuItem {
                        text: qsTr("Layers")
                        checkable: true
                        checked: Panels.isVisible("layers")
                        onTriggered: Panels.toggle("layers")
                    }
                    MenuItem {
                        text: qsTr("Properties")
                        checkable: true
                        checked: Panels.isVisible("properties")
                        onTriggered: Panels.toggle("properties")
                    }
                    MenuItem {
                        text: qsTr("Color")
                        checkable: true
                        checked: Panels.isVisible("color")
                        onTriggered: Panels.toggle("color")
                    }
                    MenuItem {
                        text: qsTr("Timeline")
                        checkable: true
                        checked: Panels.isVisible("timeline")
                        onTriggered: Panels.toggle("timeline")
                    }
                    MenuItem {
                        text: qsTr("Assets")
                        checkable: true
                        checked: Panels.isVisible("assets")
                        onTriggered: Panels.toggle("assets")
                    }
                    MenuItem {
                        text: qsTr("History")
                        checkable: true
                        checked: Panels.isVisible("history")
                        onTriggered: Panels.toggle("history")
                    }
                    MenuSeparator {
                    }
                    MenuItem {
                        text: qsTr("Reset Layout")
                        onTriggered: Panels.resetLayout()
                    }
                }
                MenuItem {
                    text: qsTr("Full Screen")
                    enabled: false
                }
            }
            Menu {
                title: qsTr("&Image")
                MenuItem {
                    text: qsTr("Image Size...")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Canvas Size...")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Rotate Canvas...")
                    enabled: false
                }
            }
            Menu {
                title: qsTr("&Layer")
                MenuItem {
                    text: qsTr("New Layer")
                    onTriggered: Workspace.layers.addLayer()
                }
                MenuItem {
                    text: qsTr("Duplicate Layer")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Delete Layer")
                    onTriggered: Workspace.layers.removeLayer(Workspace.layers.activeLayerIndex)
                }
                MenuSeparator {
                }
                MenuItem {
                    text: qsTr("Layer Properties...")
                    enabled: false
                }
            }
            Menu {
                title: qsTr("&Animation")
                MenuItem {
                    text: qsTr("New Frame")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Onion Skin")
                    enabled: false
                }
                MenuItem {
                    text: qsTr("Playback...")
                    enabled: false
                }
            }
            Menu {
                title: qsTr("&Window")
                MenuItem {
                    text: qsTr("New Window")
                    enabled: false
                }
            }
            Menu {
                title: qsTr("&Help")
                MenuItem {
                    text: qsTr("About KomaPaint")
                    onTriggered: aboutDialog.visible = true
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            contentHeight: 38
            clip: true
            Row {
                spacing: 2
                Repeater {
                    model: Workspace.projectTabs
                    delegate: Row {
                        required property int index
                        required property var modelData
                        Button {
                            text: modelData.title + (modelData.modified ? " *" : "")
                            highlighted: index === Workspace.activeTab
                            onClicked: Workspace.activateTab(index)
                        }
                        ToolButton { text: "×"; Accessible.name: qsTr("Close project"); onClicked: root.requestTabClose(index) }
                    }
                }
            }
        }

        // ------------------------------------------------------------- Toolbar
        MainToolbar {
            id: toolbar
            Layout.fillWidth: true
            onNewDocumentRequested: root.requestNewDocument()
            onFitRequested: canvasArea.fitCanvas()
        }

        // ----------------------------------------------------- Center + panels
        Item {
            id: workspaceArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // ---- Left: tool palette
                ToolPalette {
                    id: toolPalette
                    Layout.preferredWidth: KP.paletteWidth
                    Layout.fillHeight: true
                }

                // ---- Middle: canvas + bottom timeline
                ColumnLayout {
                    id: centerCol
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    CanvasWorkspace {
                        id: canvasArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    TimelinePanel {
                        id: timelinePanel
                        Layout.fillWidth: true
                        Layout.preferredHeight: Panels.isVisible("timeline") ? 180 : 0
                        visible: Panels.isVisible("timeline")
                    }

                    StatusBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: KP.statusbarHeight
                    }
                }

                // ---- Right: dock panel column
                ColumnLayout {
                    id: rightColumn
                    Layout.preferredWidth: 260
                    // A nested layout reports no useful implicit width, and
                    // without an explicit cap the row layout stretches this
                    // dock across the whole window, collapsing the canvas.
                    Layout.maximumWidth: 260
                    Layout.fillHeight: true
                    spacing: 1

                    LayersPanel {
                        id: layersPanel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Panels.isVisible("layers")
                        onVisibleChanged: if (visible && !Panels.isVisible("layers"))
                            Panels.setVisible("layers", true)
                        Connections {
                            target: Panels
                            function onPanelVisibilityChanged(id, v) {
                                if (id === "layers")
                                    layersPanel.visible = v;
                            }
                        }
                    }
                    PropertiesPanel {
                        id: propertiesPanel
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        visible: Panels.isVisible("properties")
                        Connections {
                            target: Panels
                            function onPanelVisibilityChanged(id, v) {
                                if (id === "properties")
                                    propertiesPanel.visible = v;
                            }
                        }
                    }
                    ColorPanel {
                        id: colorPanel
                        Layout.fillWidth: true
                        Layout.preferredHeight: 200
                        visible: Panels.isVisible("color")
                        Connections {
                            target: Panels
                            function onPanelVisibilityChanged(id, v) {
                                if (id === "color")
                                    colorPanel.visible = v;
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: recoverySettingsDialog
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Automatic recovery")
        onAboutToShow: {
            const saved = Number(Settings.value("recovery/intervalMinutes", 10))
            const values = [5, 10, 15, 30, 60, 0]
            const found = values.indexOf(saved)
            recoveryInterval.currentIndex = found < 0 ? 1 : found
        }
        contentItem: ColumnLayout {
            width: 380
            Label { text: qsTr("Save separate recovery snapshots:"); color: KP.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ComboBox { id: recoveryInterval; Layout.fillWidth: true; model: [qsTr("Every 5 minutes"), qsTr("Every 10 minutes"), qsTr("Every 15 minutes"), qsTr("Every 30 minutes"), qsTr("Every 60 minutes"), qsTr("Never")] }
            Label { text: qsTr("Never disables periodic crash protection for unsaved work. Recovery snapshots do not overwrite your project files."); color: KP.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("OK"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: {
                const minutes = [5, 10, 15, 30, 60, 0][recoveryInterval.currentIndex]
                Settings.setValue("recovery/intervalMinutes", minutes)
                Settings.setValue("recovery/configured", true)
                recoverySettingsDialog.close()
                root.beginRecovery()
            } }
        }
    }
    Dialog {
        id: recoveryChoiceDialog
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Restore previous workspace?")
        contentItem: Label { text: qsTr("Restore the last recovery snapshots and project tabs? Original files will not be overwritten. Undo history starts empty."); color: KP.text; wrapMode: Text.WordWrap; width: 380 }
        footer: DialogButtonBox {
            Button { text: qsTr("Restore"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: {
                if (Workspace.restoreWorkspace()) { recoveryChoiceDialog.close(); root.beginRecovery() }
            } }
            Button { text: qsTr("Discard recovery"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: recoveryDiscardConfirmation.open() }
        }
    }
    Dialog {
        id: recoveryDiscardConfirmation
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard recovery?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { text: qsTr("Discard the previous workspace recovery information? Original project files will not be changed."); color: KP.text; wrapMode: Text.WordWrap; width: 380 }
        onAccepted: if (Workspace.discardRecovery()) { recoveryChoiceDialog.close(); root.beginRecovery() }
    }
    Dialog {
        id: saveChangesDialog
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Save changes?")
        contentItem: Label { text: qsTr("Save changes to %1?").arg(Workspace.document.title); color: KP.text; wrapMode: Text.WordWrap; width: 360 }
        footer: DialogButtonBox {
            Button { text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: { saveChangesDialog.close(); root.saveForClose() } }
            Button { text: qsTr("Don't Save"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: { saveChangesDialog.close(); root.closeDecisionComplete(true) } }
            Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { saveChangesDialog.close(); root.closeAction = ""; root.approvedCloseIds = [] } }
        }
    }
    Dialog {
        id: clearHistoryDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Clear artwork history?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { text: qsTr("This frees history memory but removes all artwork undo and redo steps. The artwork itself will not change."); color: KP.text; wrapMode: Text.WordWrap; width: 360 }
        onAccepted: Workspace.history.clear()
    }
    Dialog {
        id: discardForNew
        objectName: "discardForNewDialog"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard unsaved changes?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { text: qsTr("Your artwork has unsaved changes. Creating a new project will permanently discard them."); color: KP.text; wrapMode: Text.WordWrap; width: 360 }
        onAccepted: newProjectDialog.open()
    }
    Dialog {
        id: discardForOpen
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard unsaved changes?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { text: qsTr("Your artwork has unsaved changes. Opening another illustration will permanently discard them."); color: KP.text; wrapMode: Text.WordWrap; width: 360 }
        onAccepted: openImageDialog.open()
    }
    Dialog {
        id: discardForClose
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard unsaved changes?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { text: qsTr("Your artwork has unsaved changes. Closing KomaPaint will permanently discard them."); color: KP.text; wrapMode: Text.WordWrap; width: 360 }
        onAccepted: { root.allowCloseWithArtwork = true; root.close() }
    }

    NewProjectDialog {
        id: newProjectDialog
    }

    // ------------------------------------------------------------- About dialog
    Dialog {
        id: aboutDialog
        modal: true
        anchors.centerIn: parent
        width: 360
        padding: 0
        background: Rectangle {
            color: KP.paneBg
            border.color: KP.border
            radius: KP.radiusM
        }
        contentItem: ColumnLayout {
            spacing: KP.gapM
            Layout.fillWidth: true
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: KP.paneHeaderBg
                Label {
                    anchors.centerIn: parent
                    text: App.versionDisplay
                    color: KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXL
                    font.bold: true
                }
            }
            Label {
                Layout.leftMargin: KP.gapL
                Layout.rightMargin: KP.gapL
                text: "Professional 2D digital painting and animation.\n\nThis is an early Phase-1 build: the application shell is in place but painting and animation engines are not yet implemented."
                color: KP.text
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.leftMargin: KP.gapL
                text: "© " + App.copyrightYear + " KomaPaint contributors. All rights reserved."
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontXS
            }
            Item {
                Layout.fillHeight: true
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: KP.gapL
                Layout.rightMargin: KP.gapL
                Layout.bottomMargin: KP.gapL
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    text: "Close"
                    palette.button: KP.buttonBg
                    palette.windowText: KP.text
                    background: Rectangle {
                        color: parent.down ? KP.buttonBgPress : (parent.hovered ? KP.buttonBgHover : KP.buttonBg)
                        border.color: KP.border
                        radius: KP.radiusS
                    }
                    contentItem: Label {
                        text: parent.text
                        color: KP.text
                        font.pixelSize: KP.fontS
                        horizontalAlignment: Text.AlignHCenter
                        font.family: KP.fontUi
                    }
                    onClicked: aboutDialog.visible = false
                }
            }
        }
    }
}
