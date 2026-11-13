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
    // Qt Quick Controls (text fields, spin boxes, combo boxes, buttons and the
    // dialogs that host them) read the window palette. Binding it to the theme
    // keeps them in step with the panels in both themes instead of always
    // looking like the system's light style.
    palette.window: KP.paneBg
    palette.windowText: KP.text
    palette.base: KP.inputBg
    palette.alternateBase: KP.paneBgAlt
    palette.text: KP.text
    palette.button: KP.buttonBg
    palette.buttonText: KP.text
    palette.brightText: KP.textOnAccent
    palette.highlight: KP.selection
    palette.highlightedText: KP.textOnAccent
    palette.toolTipBase: KP.paneHeaderBg
    palette.toolTipText: KP.text
    palette.placeholderText: KP.textSubtle
    palette.mid: KP.border
    palette.midlight: KP.borderLight
    palette.dark: KP.border
    palette.light: KP.borderLight

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
    property int activeTabAtClose: -1
    function requestTabClose(index) {
        if (!Workspace.activateTab(index)) return
        if (!Workspace.document.modified) { finishTabClose(index, false); return }
        closeAction = "tab"
        closeTabIndex = index
        saveChangesDialog.open()
    }
    // Closing one tab touches only that tab: its document, its history and
    // (because the user saved or explicitly chose Don't Save) its recovery copy.
    function finishTabClose(index, discard) {
        Workspace.closeTab(index, discard)
    }
    // Cancel: nothing is closed, nothing is discarded, and the project the
    // user was working on is shown again.
    function cancelClose() {
        const wasApp = closeAction === "app"
        closeAction = ""
        approvedCloseIds = []
        if (wasApp && activeTabAtClose >= 0 && activeTabAtClose < Workspace.projectTabs.length)
            Workspace.activateTab(activeTabAtClose)
    }
    // Returns true when every modified project has a decision and the
    // window may close; false when a question is now on screen (or a stroke
    // in progress blocks the close).
    function prepareApplicationClose() {
        const tabs = Workspace.projectTabs
        for (let i = 0; i < tabs.length; ++i) {
            if (tabs[i].modified && approvedCloseIds.indexOf(tabs[i].id) < 0) {
                if (!Workspace.activateTab(i)) { closeAction = ""; return false }
                closeAction = "app"
                closeTabIndex = i
                saveChangesDialog.open()
                return false
            }
        }
        // Every modified project now has a decision and nothing has been
        // closed yet. Write the session (tabs, active project, layout) and
        // drop recovery copies only for projects that were saved or that the
        // user chose not to save. A storage problem here never traps the
        // user in the application.
        if (activeTabAtClose >= 0 && activeTabAtClose < Workspace.projectTabs.length)
            Workspace.activateTab(activeTabAtClose)
        Workspace.finishShutdown(approvedCloseIds)
        closeAction = ""
        allowCloseWithArtwork = true
        return true
    }
    // Called after an answer in the Save changes dialog.
    function continueApplicationClose() {
        if (prepareApplicationClose()) root.close()
    }
    function applyCloseAnswer() {
        const answer = saveChangesDialog.decision
        saveChangesDialog.decision = ""
        if (answer === "save") saveForClose()
        else if (answer === "discard") closeDecisionComplete(true)
        else if (!saveChangesDialog.visible) cancelClose()  // Cancel, or closed any other way: keep everything
    }
    function closeDecisionComplete(discard) {
        if (closeAction === "tab") {
            finishTabClose(closeTabIndex, discard)
            closeAction = ""
        } else if (closeAction === "app") {
            if (discard) approvedCloseIds = approvedCloseIds.concat([Workspace.projectTabs[closeTabIndex].id])
            continueApplicationClose()
        }
    }
    function saveForClose() {
        if (Workspace.currentFile.length === 0) { saveImageDialog.open(); return }
        if (Workspace.saveToPath(Workspace.currentFile)) closeDecisionComplete(false)
        else cancelClose()
    }
    onClosing: function(event) {
        if (!allowCloseWithArtwork) {
            if (closeAction === "") activeTabAtClose = Workspace.activeTab
            approvedCloseIds = []
            // Accept this very close request when nothing needs asking. (A
            // second close() from inside this handler is ignored by Qt, which
            // used to make File > Exit do nothing the first time.)
            event.accepted = prepareApplicationClose()
        }
    }

    readonly property bool editingText: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    Shortcut { sequence: "Ctrl+Z"; enabled: !root.editingText && Workspace.history.canUndo; onActivated: Workspace.history.undo() }
    Shortcut { sequence: "Ctrl+Y"; enabled: !root.editingText && Workspace.history.canRedo; onActivated: Workspace.history.redo() }
    Shortcut { sequence: "Ctrl+O"; enabled: !root.editingText; onActivated: root.requestOpenImage() }
    Shortcut { sequence: "Ctrl+S"; enabled: !root.editingText; onActivated: root.saveImage() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !root.editingText; onActivated: saveImageDialog.open() }
    // Tool keys (shown in the tool palette tooltips).
    Shortcut { sequence: "Z"; enabled: !root.editingText; onActivated: Workspace.activeTool = "zoom" }
    Shortcut { sequence: "H"; enabled: !root.editingText; onActivated: Workspace.activeTool = "hand" }
    Shortcut { sequence: "I"; enabled: !root.editingText; onActivated: Workspace.activeTool = "eyedropper" }
    Shortcut { sequence: "P"; enabled: !root.editingText; onActivated: Workspace.activeTool = "pen" }
    Shortcut { sequence: "N"; enabled: !root.editingText; onActivated: Workspace.activeTool = "pencil" }
    Shortcut { sequence: "B"; enabled: !root.editingText; onActivated: Workspace.activeTool = "brush" }
    Shortcut { sequence: "A"; enabled: !root.editingText; onActivated: Workspace.activeTool = "airbrush" }
    Shortcut { sequence: "E"; enabled: !root.editingText; onActivated: Workspace.activeTool = "eraser" }
    // [ and ] step to the next smaller / larger size of the Brush Size panel.
    function stepBrushSize(direction) {
        if (!Workspace.subTools.paintTool) return
        const sizes = brushSizePanel.sizes, now = Workspace.brush.size
        if (direction > 0) { for (let i = 0; i < sizes.length; ++i) if (sizes[i] > now) { Workspace.brush.size = sizes[i]; return } }
        else { for (let i = sizes.length - 1; i >= 0; --i) if (sizes[i] < now) { Workspace.brush.size = sizes[i]; return } }
    }
    Shortcut { sequence: "["; enabled: !root.editingText; onActivated: root.stepBrushSize(-1) }
    Shortcut { sequence: "]"; enabled: !root.editingText; onActivated: root.stepBrushSize(1) }
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
            } else root.cancelClose()
        }
        onRejected: root.cancelClose()
    }

    minimumWidth: 1024
    minimumHeight: 640

    // Persist window size when the user resizes / (un)maximizes.
    onWidthChanged: if (visibility === Window.Windowed)
        Settings.windowSize = Qt.size(width, height)
    onHeightChanged: if (visibility === Window.Windowed)
        Settings.windowSize = Qt.size(width, height)
    onVisibilityChanged: Settings.windowMaximized = (visibility === Window.Maximized)

    // Width of the right dock; the user drags the handle next to it. Kept in
    // the session together with which panels are shown.
    property real rightDockWidth: 260
    property bool recoveryPrompted: false
    // Stored settings come back as text on some platforms ("false" is not
    // false in JavaScript), so compare explicitly.
    function settingIsTrue(key, fallback) {
        const v = Settings.value(key, fallback)
        return v === true || v === "true" || v === 1 || v === "1"
    }
    // ---- Theme -------------------------------------------------------------
    // KomaTheme.qml owns the colours and the icon tints; the chosen theme and
    // any icon colour overrides are user preferences, stored next to the
    // window geometry. Loading happens before the first frame, so the window
    // never flashes the wrong theme.
    function loadThemePreferences() {
        KP.mode = (String(Settings.value("ui/theme", "dark")) === "light") ? "light" : "dark"
        KP.iconNormalPref = String(Settings.value("ui/icons/normal", ""))
        KP.iconHoverPref = String(Settings.value("ui/icons/hover", ""))
        KP.iconSelectedPref = String(Settings.value("ui/icons/selected", ""))
        KP.iconDisabledPref = String(Settings.value("ui/icons/disabled", ""))
    }
    // Written immediately (and flushed) rather than at exit: a theme the user
    // picked is expected to survive a crash as well as a clean close.
    function storeThemeValue(key, value) {
        Settings.setValue(key, value)
        Settings.sync()
    }
    Connections {
        target: KP
        function onModeChanged() { root.storeThemeValue("ui/theme", KP.mode) }
        function onIconNormalPrefChanged() { root.storeThemeValue("ui/icons/normal", KP.iconNormalPref) }
        function onIconHoverPrefChanged() { root.storeThemeValue("ui/icons/hover", KP.iconHoverPref) }
        function onIconSelectedPrefChanged() { root.storeThemeValue("ui/icons/selected", KP.iconSelectedPref) }
        function onIconDisabledPrefChanged() { root.storeThemeValue("ui/icons/disabled", KP.iconDisabledPref) }
    }

    // Session: reopen last time's saved projects (each with its own tool,
    // zoom and position) and put the window layout back. This is separate
    // from recovery and never asks a question.
    function restoreSessionState() {
        Workspace.restoreSession()
        const ids = ["subtool", "toolproperty", "brushsize", "layers", "properties", "color", "timeline"]
        for (let i = 0; i < ids.length; ++i) {
            const v = Workspace.sessionValue("panel/" + ids[i])
            if (v !== undefined && v !== null) Panels.setVisible(ids[i], v === true || v === "true")
        }
        const dock = Number(Workspace.sessionValue("layout/rightDockWidth", 0))
        if (dock > 0) rightDockWidth = dock
    }
    // Recovery: periodic snapshots always run at the chosen interval, also
    // while older recovery entries are still waiting for a decision.
    function beginRecovery() {
        Workspace.setRecoveryInterval(Number(Settings.value("recovery/intervalMinutes", 10)))
        if (Workspace.recoveryAvailable && !recoveryPrompted) {
            recoveryPrompted = true
            recoveryChoiceDialog.open()
        }
    }
    function beginRecoverySetup() {
        restoreSessionState()
        if (!Workspace.recoveryReady) return
        if (!settingIsTrue("recovery/configured", false)) recoverySettingsDialog.open()
        else beginRecovery()
    }
    function recoveryNote(entry) {
        const parts = []
        if (entry.snapshotTime.length > 0) parts.push(qsTr("Recovery copy from %1.").arg(entry.snapshotTime))
        if (entry.originalState === "changed")
            parts.push(qsTr("The saved file has changed since then, so this opens as a separate project that you keep with Save As."))
        else if (entry.originalState === "missing")
            parts.push(qsTr("The saved file is missing, so this opens as a separate project that you keep with Save As."))
        if (entry.message.length > 0) parts.push(entry.message)
        return parts.join(" ")
    }
    Connections {
        target: Panels
        function onPanelVisibilityChanged(id, v) { Workspace.setSessionValue("panel/" + id, v) }
    }
    // Defer opening dialogs until the window has completed and its overlay is
    // available; opening a popup directly from Component.onCompleted can be
    // lost on first show.
    Component.onCompleted: {
        root.loadThemePreferences()
        Qt.callLater(function() { root.beginRecoverySetup() })
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
                    color: mbi.highlighted ? KP.textOnAccent : KP.text
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
                    objectName: "recoverUnsavedMenuItem"
                    text: qsTr("Recover Unsaved Work...")
                    enabled: Workspace.recoveryAvailable
                    onTriggered: recoveryChoiceDialog.open()
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
                    objectName: "preferencesMenuItem"
                    text: qsTr("Preferences...")
                    onTriggered: preferencesDialog.open()
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
                        text: qsTr("Sub Tool")
                        checkable: true
                        checked: Panels.isVisible("subtool")
                        onTriggered: Panels.toggle("subtool")
                    }
                    MenuItem {
                        text: qsTr("Tool Property")
                        checkable: true
                        checked: Panels.isVisible("toolproperty")
                        onTriggered: Panels.toggle("toolproperty")
                    }
                    MenuItem {
                        text: qsTr("Brush Size")
                        checkable: true
                        checked: Panels.isVisible("brushsize")
                        onTriggered: Panels.toggle("brushsize")
                    }
                    MenuSeparator {
                    }
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
                    // These panels do not exist yet; they are shown disabled
                    // rather than pretending to toggle something.
                    MenuItem {
                        text: qsTr("Assets")
                        enabled: false
                    }
                    MenuItem {
                        text: qsTr("History")
                        enabled: false
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
                            objectName: "projectTabButton" + index
                            text: modelData.title + (modelData.modified ? " *" : "")
                            highlighted: index === Workspace.activeTab
                            ToolTip.visible: hovered && modelData.path.length > 0
                            ToolTip.text: modelData.path
                            onClicked: Workspace.activateTab(index)
                        }
                        ToolButton { objectName: "projectTabClose" + index; text: "×"; Accessible.name: qsTr("Close project"); onClicked: root.requestTabClose(index) }
                    }
                }
            }
        }

        // ------------------------------------------------------------- Toolbar
        MainToolbar {
            id: toolbar
            Layout.fillWidth: true
            onNewDocumentRequested: root.requestNewDocument()
            onOpenRequested: root.requestOpenImage()
            onSaveRequested: root.saveImage()
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

                // ---- Left dock: Sub Tool / Tool Property / Brush Size
                // The brush is set up here, next to the tools, not in the
                // top toolbar. The column disappears when all three panels
                // are closed (View > Panels brings them back).
                ColumnLayout {
                    id: leftDock
                    objectName: "leftDock"
                    // Panel visibility as bindable values. Panels.isVisible()
                    // is a function call, so it is re-read when the panel
                    // manager reports a change.
                    property int revision: 0
                    function shown(id) { return revision >= 0 && Panels.isVisible(id) }
                    readonly property bool showSubTool: shown("subtool")
                    readonly property bool showToolProperty: shown("toolproperty")
                    readonly property bool showBrushSize: shown("brushsize")
                    Connections {
                        target: Panels
                        function onVisiblePanelsChanged() { leftDock.revision += 1 }
                    }
                    visible: showSubTool || showToolProperty || showBrushSize
                    Layout.preferredWidth: KP.leftDockWidth
                    Layout.maximumWidth: KP.leftDockWidth
                    Layout.fillHeight: true
                    spacing: 1

                    SubToolPanel {
                        id: subToolPanel
                        objectName: "subToolPanel"
                        visible: leftDock.showSubTool
                        toolLabel: toolPalette.labelFor(Workspace.activeTool)
                        Layout.fillWidth: true
                        // Takes the spare height only when Tool Property is closed.
                        Layout.fillHeight: !leftDock.showToolProperty
                        Layout.preferredHeight: 166
                        Layout.minimumHeight: 100
                    }
                    ToolPropertyPanel {
                        id: toolPropertyPanel
                        objectName: "toolPropertyPanel"
                        visible: leftDock.showToolProperty
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 140
                    }
                    BrushSizePanel {
                        id: brushSizePanel
                        objectName: "brushSizePanel"
                        visible: leftDock.showBrushSize
                        Layout.fillWidth: true
                        Layout.fillHeight: !leftDock.showSubTool && !leftDock.showToolProperty
                        Layout.preferredHeight: preferredHeight
                        Layout.minimumHeight: KP.panelHeaderH + cellHeight
                    }
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

                // ---- Drag handle: resizes the right dock
                Rectangle {
                    id: rightDockHandle
                    objectName: "rightDockHandle"
                    Layout.preferredWidth: 5
                    Layout.fillHeight: true
                    color: dockDrag.pressed || dockDrag.containsMouse ? KP.selection : KP.border
                    MouseArea {
                        id: dockDrag
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SplitHCursor
                        property real startX: 0
                        property real startWidth: 0
                        onPressed: function(mouse) {
                            startX = mapToItem(workspaceArea, mouse.x, 0).x
                            startWidth = root.rightDockWidth
                        }
                        onPositionChanged: function(mouse) {
                            if (!pressed) return
                            const moved = mapToItem(workspaceArea, mouse.x, 0).x - startX
                            root.rightDockWidth = Math.max(200, Math.min(workspaceArea.width * 0.6, startWidth - moved))
                        }
                        onReleased: Workspace.setSessionValue("layout/rightDockWidth", Math.round(root.rightDockWidth))
                    }
                }

                // ---- Right: dock panel column
                ColumnLayout {
                    id: rightColumn
                    Layout.preferredWidth: root.rightDockWidth
                    // A nested layout reports no useful implicit width, and
                    // without an explicit cap the row layout stretches this
                    // dock across the whole window, collapsing the canvas.
                    Layout.maximumWidth: root.rightDockWidth
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
        objectName: "recoverySettingsDialog"
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
            ComboBox { id: recoveryInterval; objectName: "recoveryIntervalCombo"; Layout.fillWidth: true; model: [qsTr("Every 5 minutes"), qsTr("Every 10 minutes"), qsTr("Every 15 minutes"), qsTr("Every 30 minutes"), qsTr("Every 60 minutes"), qsTr("Never")] }
            Label { text: qsTr("Never disables periodic crash protection for unsaved work. Recovery snapshots do not overwrite your project files."); color: KP.textMuted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("OK"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: {
                const minutes = [5, 10, 15, 30, 60, 0][recoveryInterval.currentIndex]
                Settings.setValue("recovery/intervalMinutes", minutes)
                Settings.setValue("recovery/configured", true)
                if (!Settings.sync()) {
                    fileErrorText.text = qsTr("Could not save the recovery preference. Please check your settings storage and try again.")
                    fileErrorDialog.open()
                    return
                }
                recoverySettingsDialog.close()
                root.beginRecovery()
            } }
        }
    }
    // Crash recovery: one row per recoverable project, each with its own
    // Recover and Discard. Nothing here ever writes to a saved project file.
    Dialog {
        id: recoveryChoiceDialog
        objectName: "recoveryChoiceDialog"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(root.width - 80, 640)
        title: qsTr("Recover unsaved work")
        Connections {
            target: Workspace
            function onRecoveryAvailableChanged() { if (!Workspace.recoveryAvailable) recoveryChoiceDialog.close() }
        }
        contentItem: ColumnLayout {
            spacing: KP.gapM
            Label {
                text: qsTr("KomaPaint has recovery copies of the projects below. Recovering opens the copy in its own tab. Your saved files are not changed unless you then choose Save. Undo history starts empty.")
                color: KP.text
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ListView {
                id: recoveryList
                objectName: "recoveryEntryList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 320)
                clip: true
                spacing: 6
                model: Workspace.recoveryEntries
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    id: recoveryRow
                    required property var modelData
                    required property int index
                    objectName: "recoveryEntry" + index
                    width: ListView.view.width
                    height: recoveryRowLayout.implicitHeight + 16
                    color: KP.paneBgAlt
                    border.color: modelData.status === "damaged" ? KP.error : KP.border
                    radius: KP.radiusS
                    RowLayout {
                        id: recoveryRowLayout
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: KP.gapM
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                objectName: "recoveryEntryTitle" + recoveryRow.index
                                text: recoveryRow.modelData.title
                                color: KP.text
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                objectName: "recoveryEntryKind" + recoveryRow.index
                                text: recoveryRow.modelData.saved
                                      ? qsTr("Saved file: %1").arg(recoveryRow.modelData.fileName)
                                      : qsTr("Never saved")
                                color: KP.textMuted
                                font.pixelSize: KP.fontS
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: recoveryRow.modelData.saved
                                text: recoveryRow.modelData.originalPath
                                color: KP.textSubtle
                                font.pixelSize: KP.fontXS
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                text: root.recoveryNote(recoveryRow.modelData)
                                visible: text.length > 0
                                color: recoveryRow.modelData.status === "damaged" ? KP.error : KP.textMuted
                                font.pixelSize: KP.fontXS
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        Button {
                            objectName: "recoverEntryButton" + recoveryRow.index
                            text: qsTr("Recover")
                            onClicked: Workspace.recoverEntry(recoveryRow.modelData.key)
                        }
                        Button {
                            objectName: "discardEntryButton" + recoveryRow.index
                            text: qsTr("Discard")
                            onClicked: {
                                recoveryDiscardConfirmation.entryKey = recoveryRow.modelData.key
                                recoveryDiscardConfirmation.entryTitle = recoveryRow.modelData.title
                                recoveryDiscardConfirmation.open()
                            }
                        }
                    }
                }
            }
        }
        footer: DialogButtonBox {
            Button { objectName: "restoreRecoveryButton"; text: qsTr("Recover all"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: Workspace.restoreWorkspace() }
            Button { objectName: "notNowRecoveryButton"; text: qsTr("Decide later"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: recoveryChoiceDialog.close() }
        }
    }
    Dialog {
        id: recoveryDiscardConfirmation
        objectName: "recoveryDiscardConfirmation"
        anchors.centerIn: parent
        modal: true
        title: qsTr("Discard recovery copy?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        property string entryKey: ""
        property string entryTitle: ""
        contentItem: Label { text: qsTr("Permanently discard the recovery copy of \"%1\"? This cannot be undone. Saved project files will not be changed.").arg(recoveryDiscardConfirmation.entryTitle); color: KP.text; wrapMode: Text.WordWrap; width: 380 }
        onAccepted: Workspace.discardEntry(entryKey)
    }
    Dialog {
        id: saveChangesDialog
        objectName: "saveChangesDialog"
        anchors.centerIn: parent
        modal: true
        closePolicy: Popup.NoAutoClose
        title: qsTr("Save changes?")
        contentItem: Label { text: qsTr("Save changes to %1?").arg(Workspace.document.title); color: KP.text; wrapMode: Text.WordWrap; width: 360 }
        // The answer is acted on one event-loop turn after the dialog has
        // closed. Asking about the next project means opening this same
        // dialog again, and re-opening it from inside its own button handler
        // left it closed: the second question never appeared, so the
        // application could not be closed with two unsaved projects.
        property string decision: ""
        footer: DialogButtonBox {
            Button { objectName: "saveChangesSaveButton"; text: qsTr("Save"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: { saveChangesDialog.decision = "save"; saveChangesDialog.close() } }
            Button { objectName: "saveChangesDiscardButton"; text: qsTr("Don't Save"); DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: { saveChangesDialog.decision = "discard"; saveChangesDialog.close() } }
            Button { objectName: "saveChangesCancelButton"; text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { saveChangesDialog.decision = "cancel"; saveChangesDialog.close() } }
        }
        onClosed: Qt.callLater(root.applyCloseAnswer)
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
    NewProjectDialog {
        id: newProjectDialog
    }
    PreferencesDialog {
        id: preferencesDialog
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
                text: "Professional 2D digital painting and animation.\n\nThis is an early alpha build. Brush painting, layers, project tabs, saving and automatic recovery work. Selection, transform, fill, text, shape and animation tools are not implemented yet."
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
