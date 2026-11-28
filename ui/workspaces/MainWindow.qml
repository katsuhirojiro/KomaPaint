import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import "../components"
import "../docking"
import "../panels"
import "../themes"

ApplicationWindow {
    id: root

    visible: true
    // Keep "KomaPaint" as the leading token (packaging smoke test relies on it).
    title: App.versionDisplay + " — " + (Workspace.document
          ? Workspace.document.title + (Workspace.document.modified ? " *" : "")
          : qsTr("Nothing here yet"))
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
    function openBrushStudio() {
        if (brushStudioLoader.item) brushStudioLoader.item.present()
        else brushStudioLoader.active = true
    }
    // ---- Create Brush Tip from Selection -----------------------------------
    property var pendingBrushTip: null
    property bool pendingBrushTipContinue: false
    property bool pendingBrushTipAdd: true
    property string pendingBrushTipReplace: ""
    property bool pendingBrushTipColour: false

    function openCreateBrushTip() {
        if (!Workspace.document) return
        const studio = brushStudioLoader.item
        brushTipDialog.info = Workspace.brushTipSourceInfo()
        brushTipDialog.hasPrevious = !!studio && studio.hasPreviousBrush
        brushTipDialog.previousRealistic = !!studio && studio.draftEngine === "realistic"
        brushTipDialog.previousTips = studio ? studio.tipChoices : []
        brushTipDialog.error = ""
        brushTipDialog.open()
    }

    function createBrushTipFromDialog(continuePrevious) {
        const data = Workspace.createBrushTipSource(brushTipDialog.selectedSource,
                                                    brushTipDialog.useSelection,
                                                    brushTipDialog.selectedCoverage)
        if (!data.ok) {
            brushTipDialog.error = data.error
            return
        }
        const colourTip = brushTipDialog.selectedCoverage === "colour"
        const addAsNew = brushTipDialog.addTipAsNew
        const replaceId = brushTipDialog.replaceTipId
        const studio = brushStudioLoader.item
        if (studio) {
            if (!studio.importBrushTip(data, continuePrevious, addAsNew, replaceId, colourTip)) {
                brushTipDialog.error = studio.draftError.length > 0
                    ? studio.draftError
                    : qsTr("Brush Studio could not use this tip.")
                return
            }
            brushTipDialog.accept()
            return
        }
        // The first use of Studio loads it lazily. Keep the decision until the
        // window and its model exist, then perform the same import.
        pendingBrushTip = data
        pendingBrushTipContinue = continuePrevious
        pendingBrushTipAdd = addAsNew
        pendingBrushTipReplace = replaceId
        pendingBrushTipColour = colourTip
        brushTipDialog.accept()
        brushStudioLoader.active = true
    }
    // Keep one Studio and its own model alive across closes. Loading by URL
    // delays their creation until the artist first uses the Edit menu action.
    Loader {
        id: brushStudioLoader
        active: false
        source: "BrushStudioWindow.qml"
        onLoaded: {
            item.transientParent = root
            item.brushLibrary = Workspace.subTools
            item.workspace = Workspace
            item.tabletSettings = Workspace.inputSettings
            item.previewInk = Qt.binding(function() { return Workspace.colors.foreground })
            if (root.pendingBrushTip) {
                const data = root.pendingBrushTip
                const keep = root.pendingBrushTipContinue
                const add = root.pendingBrushTipAdd
                const replace = root.pendingBrushTipReplace
                const colour = root.pendingBrushTipColour
                root.pendingBrushTip = null
                item.importBrushTip(data, keep, add, replace, colour)
            }
            item.present()
        }
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
        // A canceled artwork close leaves Studio alone. On accepted exit,
        // close the native window and let the Loader release it and its draft.
        if (event.accepted && brushStudioLoader.item) {
            brushStudioLoader.item.close()
            brushStudioLoader.active = false
        }
    }

    // The brush size ladder is a quick set of choices, not a size limit.
    readonly property var brushSizeSteps: [1, 2, 3, 4, 5, 6, 7,
                                           8, 10, 12, 15, 17, 20, 25,
                                           30, 40, 50, 60, 70, 80, 100,
                                           120, 150, 170, 200, 256]
    readonly property bool editingText: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    readonly property bool shortcutsEnabled: !root.editingText && !komaSettingsDialog.capturingShortcut
    function shortcutSeq(actionId) {
        if (!Workspace.shortcuts || !Workspace.shortcuts.bindings) return ""
        const s = Workspace.shortcuts.bindings[actionId]
        return s ? String(s) : ""
    }
    function menuShortcutSuffix(actionId) {
        const s = shortcutSeq(actionId)
        return s.length > 0 ? "\t" + s : ""
    }
    // Repeated presses of a tool key walk its family (B: Brush -> Airbrush),
    // matching the way Clip Studio Paint cycles tool groups. A key with no
    // family simply selects its tool.
    function activateTool(actionId) {
        if (!Workspace.cycleToolGroup(actionId)) Workspace.activeTool = String(actionId).replace("tool.", "")
    }
    function toggleSidePanels() {
        const collapse = !Dock.isAreaCollapsed("left") || !Dock.isAreaCollapsed("right")
        Dock.setAreaCollapsed("left", collapse)
        Dock.setAreaCollapsed("right", collapse)
    }

    Shortcut { sequence: root.shortcutSeq("edit.undo"); enabled: root.shortcutsEnabled && Workspace.history && Workspace.history.canUndo; onActivated: Workspace.history.undo() }
    Shortcut { sequence: root.shortcutSeq("edit.redo"); enabled: root.shortcutsEnabled && Workspace.history && Workspace.history.canRedo; onActivated: Workspace.history.redo() }
    Shortcut { sequence: root.shortcutSeq("edit.redoAlt"); enabled: root.shortcutsEnabled && Workspace.history && Workspace.history.canRedo; onActivated: Workspace.history.redo() }
    Shortcut { sequence: root.shortcutSeq("file.new"); enabled: root.shortcutsEnabled; onActivated: root.requestNewDocument() }
    Shortcut { sequence: root.shortcutSeq("file.open"); enabled: root.shortcutsEnabled; onActivated: root.requestOpenImage() }
    Shortcut { sequence: root.shortcutSeq("file.save"); enabled: root.shortcutsEnabled && Workspace.document; onActivated: root.saveImage() }
    Shortcut { sequence: root.shortcutSeq("file.saveAs"); enabled: root.shortcutsEnabled && Workspace.document; onActivated: saveImageDialog.open() }
    // Tool shortcuts driven centrally by ShortcutManager.
    Shortcut { sequence: root.shortcutSeq("tool.zoom"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "zoom" }
    Shortcut { sequence: root.shortcutSeq("tool.hand"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "hand" }
    Shortcut { sequence: root.shortcutSeq("tool.eyedropper"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "eyedropper" }
    Shortcut { sequence: root.shortcutSeq("tool.pen"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.pen") }
    Shortcut { sequence: root.shortcutSeq("tool.pencil"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.pencil") }
    Shortcut { sequence: root.shortcutSeq("tool.brush"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.brush") }
    Shortcut { sequence: root.shortcutSeq("tool.airbrush"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.airbrush") }
    Shortcut { sequence: root.shortcutSeq("tool.eraser"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "eraser" }
    Shortcut { sequence: root.shortcutSeq("tool.smudge"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "smudge" }
    Shortcut { sequence: root.shortcutSeq("tool.fill"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "fill" }
    Shortcut { sequence: root.shortcutSeq("tool.gradient"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "gradient" }
    Shortcut { sequence: root.shortcutSeq("tool.wand"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.wand") }
    Shortcut { sequence: root.shortcutSeq("tool.lasso"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.lasso") }
    Shortcut { sequence: root.shortcutSeq("tool.selection"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.selection") }
    Shortcut { sequence: root.shortcutSeq("tool.move"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "move" }
    Shortcut { sequence: root.shortcutSeq("tool.transform"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "transform" }
    Shortcut { sequence: root.shortcutSeq("tool.line"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "line" }
    Shortcut { sequence: root.shortcutSeq("tool.curveline"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "curveline" }
    Shortcut { sequence: root.shortcutSeq("tool.rectangle"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.rectangle") }
    Shortcut { sequence: root.shortcutSeq("tool.circle"); enabled: root.shortcutsEnabled; onActivated: root.activateTool("tool.circle") }
    Shortcut { sequence: root.shortcutSeq("tool.text"); enabled: root.shortcutsEnabled; onActivated: Workspace.activeTool = "text" }
    Shortcut { sequence: root.shortcutSeq("select.all"); enabled: root.shortcutsEnabled && Workspace.document !== null; onActivated: Workspace.selectAll() }
    Shortcut { sequence: root.shortcutSeq("select.deselect"); enabled: root.shortcutsEnabled && Workspace.hasSelection; onActivated: Workspace.deselect() }
    Shortcut { sequence: root.shortcutSeq("select.invert"); enabled: root.shortcutsEnabled && Workspace.document !== null; onActivated: Workspace.invertSelection() }
    Shortcut { sequence: root.shortcutSeq("view.resetRotation"); enabled: root.shortcutsEnabled && dock.canvasItem !== null; onActivated: if (dock.canvasItem && typeof dock.canvasItem.resetRotation === "function") dock.canvasItem.resetRotation() }
    Shortcut { sequence: root.shortcutSeq("view.togglePanels"); enabled: root.shortcutsEnabled && Workspace.document !== null; onActivated: root.toggleSidePanels() }
    Shortcut { sequence: root.shortcutSeq("color.swap"); enabled: root.shortcutsEnabled; onActivated: Workspace.colors.swap() }
    Shortcut { sequence: root.shortcutSeq("color.reset"); enabled: root.shortcutsEnabled; onActivated: Workspace.colors.reset() }
    Shortcut { sequence: root.shortcutSeq("layer.newRaster"); enabled: root.shortcutsEnabled && Workspace.document !== null; onActivated: Workspace.layers.addLayer() }
    // [ and ] step to the next smaller / larger size of the Brush Size panel.
    function stepBrushSize(direction) {
        if (!Workspace.subTools.paintTool) return
        const sizes = root.brushSizeSteps, now = Workspace.brush.size
        const last = sizes[sizes.length - 1]
        if (now > last || (now === last && direction > 0)) {
            // Continue beyond the ladder at a proportional step, without
            // jumping a large typed diameter back to the smallest presets.
            const step = Math.max(1, Math.round(now / 8))
            Workspace.brush.size = Math.max(last, Math.min(Workspace.brush.maximumSize, now + direction * step))
        } else if (direction > 0) {
            for (let i = 0; i < sizes.length; ++i) if (sizes[i] > now) { Workspace.brush.size = sizes[i]; return }
        } else {
            for (let i = sizes.length - 1; i >= 0; --i) if (sizes[i] < now) { Workspace.brush.size = sizes[i]; return }
        }
    }
    Shortcut { sequence: root.shortcutSeq("brush.sizeDown"); enabled: root.shortcutsEnabled && !Workspace.brush.nativeBrush; onActivated: root.stepBrushSize(-1) }
    Shortcut { sequence: root.shortcutSeq("brush.sizeUp"); enabled: root.shortcutsEnabled && !Workspace.brush.nativeBrush; onActivated: root.stepBrushSize(1) }
    Connections {
        target: Workspace.history
        function onErrorOccurred(message) { historyErrorText.text = message; historyErrorDialog.open() }
    }
    Connections {
        target: Workspace
        function onFileErrorOccurred(message) { fileErrorText.text = message; fileErrorDialog.open() }
        function onFileAssociationCompleted(message, ok) { associationResultText.text = message; associationResultDialog.open() }
    }
    // Message dialogs: one sentence, one way out. The frame is the shared
    // KpDialog, so an error looks like the application rather than a system
    // alert.
    KpDialog {
        id: historyErrorDialog
        heading: qsTr("Artwork history")
        body: [
            Text {
                id: historyErrorText
                Layout.fillWidth: true
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("OK"); variant: "primary"; onClicked: historyErrorDialog.accept() }
        ]
    }
    KpDialog {
        id: fileErrorDialog
        heading: qsTr("Illustration file")
        body: [
            Text {
                id: fileErrorText
                Layout.fillWidth: true
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("OK"); variant: "primary"; onClicked: fileErrorDialog.accept() }
        ]
    }
    KpDialog {
        id: associationResultDialog
        heading: qsTr("File association")
        body: [
            Text {
                id: associationResultText
                Layout.fillWidth: true
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("OK"); variant: "primary"; onClicked: associationResultDialog.accept() }
        ]
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
                      : Qt.resolvedUrl((Workspace.document ? Workspace.document.title : qsTr("Untitled")) + ".koma")
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
    // Read the properties through `root` rather than through the handler's
    // implicit parameters, which Qt 6 deprecates.
    onWidthChanged: if (root.visibility === Window.Windowed)
        Settings.windowSize = Qt.size(root.width, root.height)
    onHeightChanged: if (root.visibility === Window.Windowed)
        Settings.windowSize = Qt.size(root.width, root.height)
    onVisibilityChanged: Settings.windowMaximized = (root.visibility === Window.Maximized)

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
    // Re-applied whenever a stored ui/ preference changes, so a value written
    // elsewhere (Preferences, a second path through the app) is picked up
    // without a restart.
    Connections {
        target: Settings
        function onValueChanged(key) {
            if (String(key).indexOf("ui/") === 0) root.loadThemePreferences()
        }
    }
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
        // The panel layout is a preference, not part of the session: DockModel
        // has already restored it from Settings when it was created.
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
        // Panel visibility is stored with the layout (workspace/layout).
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

        // ------------------------------------------------------- Application menu
        // Menus have a dedicated band above the command bar.
        // Qt Quick menus cannot render `&` mnemonics, so the labels
        // are shown as written ('File', not '&File') instead of leaking the raw
        // ampersand into the interface.
        MenuBar {
            id: menuBar
            Layout.fillWidth: true
            Layout.preferredHeight: KP.menubarHeight
            padding: 0
            leftPadding: KP.gapS
            rightPadding: KP.gapM

            background: Rectangle {
                color: KP.menubarBg

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: KP.hairline
                }


            }

            delegate: MenuBarItem {
                id: mbi
                contentItem: Text {
                    text: mbi.text.replace(/&/g, "")
                    color: mbi.highlighted ? KP.text : KP.textMuted
                    font.pixelSize: KP.fontM
                    font.family: KP.fontUi
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideNone
                }
                background: Rectangle {
                    color: "transparent"
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 5
                        anchors.bottomMargin: 5
                        radius: KP.radiusXS
                        color: mbi.highlighted ? KP.hoverOverlayStrong : "transparent"
                    }
                }
                padding: 0
                leftPadding: 9
                rightPadding: 9
            }

            KpMenu {
                title: qsTr("&File")
                KpMenuItem {
                    text: qsTr("&New...") + root.menuShortcutSuffix("file.new")
                    onTriggered: root.requestNewDocument()
                }
                KpMenuItem {
                    text: qsTr("&Open...") + root.menuShortcutSuffix("file.open")
                    onTriggered: root.requestOpenImage()
                }
                KpMenuItem {
                    text: qsTr("&Save") + root.menuShortcutSuffix("file.save")
                    enabled: Workspace.document !== null
                    onTriggered: if (Workspace.document) root.saveImage()
                }
                KpMenuItem {
                    text: qsTr("Save &As...") + root.menuShortcutSuffix("file.saveAs")
                    enabled: Workspace.document !== null
                    onTriggered: if (Workspace.document) saveImageDialog.open()
                }
                KpMenuSeparator {
                }
                KpMenuItem {
                    text: qsTr("Export...")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Import...")
                    enabled: false
                }
                KpMenuSeparator {
                }
                KpMenuItem {
                    objectName: "recoverUnsavedMenuItem"
                    text: qsTr("Recover Unsaved Work...")
                    enabled: Workspace.recoveryAvailable
                    onTriggered: recoveryChoiceDialog.open()
                }
                KpMenuItem {
                    text: qsTr("Recovery settings...")
                    onTriggered: recoverySettingsDialog.open()
                }
                KpMenuItem {
                    objectName: "komaSettingsMenuItem"
                    text: qsTr("Koma Settings...")
                    onTriggered: komaSettingsDialog.open()
                }
                KpMenuItem {
                    text: qsTr("Associate &.koma Files...")
                    onTriggered: Workspace.associateWithKomaFiles()
                }
                KpMenuSeparator {
                }
                KpMenuItem {
                    text: qsTr("E&xit")
                    onTriggered: root.close()
                }
            }
            KpMenu {
                objectName: "editMenu"
                title: qsTr("&Edit")
                KpMenuItem {
                    text: qsTr("&Undo") + root.menuShortcutSuffix("edit.undo")
                    enabled: Workspace.history !== null && Workspace.history.canUndo
                    onTriggered: if (Workspace.history) Workspace.history.undo()
                }
                KpMenuItem {
                    text: qsTr("&Redo") + root.menuShortcutSuffix("edit.redo")
                    enabled: Workspace.history !== null && Workspace.history.canRedo
                    onTriggered: if (Workspace.history) Workspace.history.redo()
                }
                KpMenuSeparator { }
                KpMenuItem {
                    text: qsTr("Clear Artwork History...")
                    enabled: Workspace.history !== null && (Workspace.history.canUndo || Workspace.history.canRedo)
                    onTriggered: clearHistoryDialog.open()
                }
                KpMenuSeparator {
                }
                KpMenuItem {
                    text: qsTr("Cut")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Copy")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Paste")
                    enabled: false
                }
                KpMenuSeparator {
                }
                KpMenuItem {
                    objectName: "brushStudioMenuItem"
                    text: qsTr("Brush Studio")
                    onTriggered: root.openBrushStudio()
                }
                KpMenuItem {
                    objectName: "createBrushTipFromSelectionMenuItem"
                    text: qsTr("Create Brush Tip from Selection...")
                    enabled: Workspace.document !== null
                    onTriggered: root.openCreateBrushTip()
                }
                KpMenuSeparator { }
                KpMenuItem {
                    objectName: "keyboardShortcutsMenuItem"
                    text: qsTr("Keyboard Shortcuts...")
                    onTriggered: komaSettingsDialog.openCategory("shortcuts")
                }
                KpMenuItem {
                    objectName: "preferencesMenuItem"
                    text: qsTr("Preferences...")
                    onTriggered: preferencesDialog.open()
                }
            }
            KpMenu {
                title: qsTr("&Select")
                KpMenuItem {
                    text: qsTr("Select &All") + root.menuShortcutSuffix("select.all")
                    enabled: Workspace.document !== null
                    onTriggered: Workspace.selectAll()
                }
                KpMenuItem {
                    text: qsTr("&Deselect") + root.menuShortcutSuffix("select.deselect")
                    enabled: Workspace.hasSelection
                    onTriggered: Workspace.deselect()
                }
                KpMenuItem {
                    text: qsTr("&Invert Selection") + root.menuShortcutSuffix("select.invert")
                    enabled: Workspace.document !== null
                    onTriggered: Workspace.invertSelection()
                }
                KpMenuSeparator {}
                KpMenuItem {
                    text: qsTr("Expand 2 px")
                    enabled: Workspace.hasSelection
                    onTriggered: Workspace.expandSelection(2)
                }
                KpMenuItem {
                    text: qsTr("Contract 2 px")
                    enabled: Workspace.hasSelection
                    onTriggered: Workspace.contractSelection(2)
                }
                KpMenuItem {
                    text: qsTr("Feather 3 px")
                    enabled: Workspace.hasSelection
                    onTriggered: Workspace.featherSelection(3)
                }
            }
            KpMenu {
                title: qsTr("&View")
                KpMenuItem {
                    action: Action {
                        text: qsTr("Zoom &In")
                        shortcut: root.shortcutSeq("view.zoomIn")
                        enabled: root.shortcutsEnabled
                        onTriggered: Workspace.zoomIn()
                    }
                }
                KpMenuItem {
                    action: Action {
                        text: qsTr("Zoom &Out")
                        shortcut: root.shortcutSeq("view.zoomOut")
                        enabled: root.shortcutsEnabled
                        onTriggered: Workspace.zoomOut()
                    }
                }
                KpMenuItem {
                    action: Action {
                        text: qsTr("&Actual Pixels")
                        shortcut: root.shortcutSeq("view.actualPixels")
                        enabled: root.shortcutsEnabled
                        onTriggered: Workspace.resetZoom()
                    }
                }
                KpMenuItem {
                    action: Action {
                        text: qsTr("Fit to Window")
                        shortcut: root.shortcutSeq("view.fitToWindow")
                        enabled: root.shortcutsEnabled
                        onTriggered: dock.fitCanvas()
                    }
                }
                KpMenuSeparator {
                }
                KpMenu {
                    title: qsTr("Color")
                    KpMenuItem {
                        text: qsTr("Palette")
                        checkable: true
                        checked: root.dockRevision >= 0 && Dock.isVisible("palette")
                        onTriggered: Dock.togglePanel("palette")
                    }
                    KpMenuItem {
                        text: qsTr("Color Mixer")
                        checkable: true
                        checked: root.dockRevision >= 0 && Dock.isVisible("colormixer")
                        onTriggered: Dock.togglePanel("colormixer")
                    }
                }
                KpMenu {
                    id: panelsMenu
                    title: qsTr("&Panels")
                    // Driven by the dock model, so every panel the application
                    // implements appears here with its current state.
                    Repeater {
                        model: root.panelMenuIds
                        KpMenuItem {
                            required property string modelData
                            text: Dock.title(modelData)
                            checkable: true
                            checked: root.dockRevision >= 0 && Dock.isVisible(modelData)
                            onTriggered: Dock.togglePanel(modelData)
                        }
                    }
                    KpMenuSeparator {}
                    // Collapsing a whole dock area, for people who work with the
                    // menus and for keyboard access to the same state the edge
                    // controls and the collapsed strips show.
                    Repeater {
                        model: root.dockAreaMenu
                        KpMenuItem {
                            required property var modelData
                            text: modelData.collapsed
                                  ? qsTr("Show %1 Panels").arg(root.areaTitle(modelData.side))
                                  : qsTr("Collapse %1 Panels").arg(root.areaTitle(modelData.side))
                            onTriggered: Dock.toggleArea(modelData.side)
                        }
                    }
                    KpMenuSeparator {}
                    KpMenuItem {
                        text: qsTr("Dock All Floating Panels")
                        enabled: Dock.floating.length > 0
                        onTriggered: {
                            const ids = []
                            for (let i = 0; i < Dock.floating.length; ++i) ids.push(Dock.floating[i].id)
                            for (let i = 0; i < ids.length; ++i) Dock.dockBack(ids[i])
                        }
                    }
                    KpMenuItem {
                        text: qsTr("Reset Layout")
                        onTriggered: Dock.resetToDefault()
                    }
                }
                KpMenu {
                    id: workspaceLayoutMenu
                    title: qsTr("&Workspace")
                    Repeater {
                        model: root.dockRevision >= 0 ? Dock.presetNames() : []
                        KpMenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: Dock.applyPreset(modelData)
                        }
                    }
                    KpMenuSeparator { visible: Dock.presetNames().length > 0 }
                    KpMenuItem {
                        text: qsTr("Save Layout as Preset…")
                        onTriggered: saveWorkspaceDialog.open()
                    }
                    KpMenuItem {
                        text: qsTr("Reset to Default Layout")
                        onTriggered: Dock.resetToDefault()
                    }
                }
                KpMenuItem {
                    text: qsTr("Full Screen")
                    enabled: false
                }
            }
            KpMenu {
                title: qsTr("&Image")
                KpMenuItem {
                    text: qsTr("Image Size...")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Canvas Size...")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Rotate Canvas...")
                    enabled: false
                }
            }
            KpMenu {
                title: qsTr("&Layer")
                KpMenuItem {
                    text: qsTr("New Layer")
                    enabled: Workspace.document !== null
                    onTriggered: if (Workspace.document) Workspace.layers.addLayer()
                }
                KpMenuItem {
                    text: qsTr("Duplicate Layer")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Delete Layer")
                    enabled: Workspace.document !== null
                    onTriggered: if (Workspace.document) Workspace.layers.removeLayer(Workspace.layers.activeLayerIndex)
                }
                KpMenuSeparator {
                }
                KpMenuItem {
                    text: qsTr("Layer Properties...")
                    enabled: false
                }
            }
            KpMenu {
                title: qsTr("&Animation")
                KpMenuItem {
                    text: qsTr("New Frame")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Onion Skin")
                    enabled: false
                }
                KpMenuItem {
                    text: qsTr("Playback...")
                    enabled: false
                }
            }
            KpMenu {
                title: qsTr("&Window")
                KpMenuItem {
                    text: qsTr("New Window")
                    enabled: false
                }
            }
            KpMenu {
                title: qsTr("&Help")
                KpMenuItem {
                    text: qsTr("About KomaPaint")
                    onTriggered: aboutDialog.visible = true
                }
            }
        }


        // ---------------------------------------------------------- Command bar
        MainToolbar {
            id: toolbar
            Layout.fillWidth: true
            onNewDocumentRequested: root.requestNewDocument()
            onOpenRequested: root.requestOpenImage()
            onSaveRequested: root.saveImage()
        }

        // ---------------------------------------------------------- Documents
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: KP.paletteWidth
                Layout.preferredHeight: KP.tabStripHeight
                color: KP.toolRailBg
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: KP.hairline }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: KP.hairline }
            }
            DocumentTabs {
                Layout.fillWidth: true
                onActivateRequested: function(index) { Workspace.activateTab(index) }
                onCloseRequested: function(index) { root.requestTabClose(index) }
                onNewRequested: root.requestNewDocument()
            }
        }

        // ------------------------------------------- Workspace: tools + docking
        // The tool palette is the fixed strip; everything to its right is the
        // shared docking workspace, which lays out the canvas, the drawing
        // panels and the timeline from the layout model (see DockModel).
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ToolPalette {
                id: toolPalette
                objectName: "toolPalette"
                Layout.preferredWidth: KP.paletteWidth
                Layout.fillHeight: true
                visible: Workspace.document !== null
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                visible: Workspace.document !== null

                DockWorkspace {
                    id: dock
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Dock
                    panelComponents: root.panelComponents
                    canvasComponent: root.canvasComponent
                }
            }

            Item {
                objectName: "emptyWorkspace"
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: Workspace.document === null
                Rectangle { anchors.fill: parent; color: KP.canvasBg }
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * KP.gapXL, 460)
                    spacing: KP.gapM
                    KpIcon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "project-types/illustration"
                        size: 42
                        color: KP.iconOff
                    }
                    Text {
                        objectName: "emptyWorkspaceInvitation"
                        Layout.fillWidth: true
                        text: qsTr("Nothing here yet. Create a new project to get started.")
                        color: KP.text
                        font.pixelSize: KP.fontM
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    KpButton {
                        objectName: "emptyWorkspaceNewProjectButton"
                        Layout.alignment: Qt.AlignHCenter
                        label: qsTr("New Project")
                        variant: "primary"
                        onClicked: root.requestNewDocument()
                    }
                }
            }
        }

        // ------------------------------------------------------------ Status rail
        // One band across the bottom of the whole shell, under the tool rail and
        // the workspace alike.
        StatusBar {
            Layout.fillWidth: true
            Layout.preferredHeight: KP.statusbarHeight
            toolLabel: toolPalette.labelFor(Workspace.activeTool)
        }
    }

    // ------------------------------------------------------------ dock workspace
    // Panel content. The docking frame owns the header, tabs, close button,
    // collapse and drag behaviour, so a panel here is only its content. A new
    // panel needs one entry in this map, one in panelMenuIds and one row in
    // DockModel::panelTable().
    readonly property var panelComponents: ({
        "subtool": subToolComponent,
        "toolproperty": toolPropertyComponent,
        "brushsize": brushSizeComponent,
        "layers": layersComponent,
        "properties": propertiesComponent,
        "color": colorComponent,
        "palette": paletteComponent,
        "colormixer": colorMixerComponent,
        "assets": assetsComponent,
        "timeline": timelineComponent
    })
    // The dock areas the layout currently has, for the Panels menu.
    readonly property var dockAreaMenu: root.dockRevision >= 0 ? Dock.areas() : []
    function areaTitle(side) {
        if (side === "left") return qsTr("Left")
        if (side === "right") return qsTr("Right")
        if (side === "top") return qsTr("Top")
        return qsTr("Bottom")
    }
    readonly property var panelMenuIds: ["subtool", "toolproperty", "brushsize",
                                         "layers", "properties", "color", "palette", "colormixer", "assets", "timeline"]
    property int dockRevision: 0
    Connections {
        target: Dock
        function onLayoutChanged() { root.dockRevision += 1 }
    }
    // Sizes of the panels the docking workspace does not control itself.
    Component {
        id: subToolComponent
        SubToolPanel {
            objectName: "subToolPanel"
            toolLabel: toolPalette.labelFor(Workspace.activeTool)
        }
    }
    Component { id: toolPropertyComponent; ToolPropertyPanel { objectName: "toolPropertyPanel" } }
    Component {
        id: brushSizeComponent
        BrushSizePanel {
            objectName: "brushSizePanel"
            sizes: root.brushSizeSteps
        }
    }
    Component { id: layersComponent; LayersPanel { objectName: "layersPanel" } }
    Component { id: propertiesComponent; PropertiesPanel { objectName: "propertiesPanel" } }
    Component { id: colorComponent; ColorPanel { objectName: "colorPanel" } }
    Component { id: paletteComponent; PalettePanel { objectName: "palettePanel" } }
    Component { id: colorMixerComponent; ColorMixerPanel { objectName: "colorMixerPanel" } }
    Component { id: assetsComponent; AssetsPanel { objectName: "assetsPanel" } }
    Component { id: timelineComponent; TimelinePanel { objectName: "timelinePanel" } }
    Component {
        id: canvasComponentItem
        CanvasWorkspace {
            id: canvasArea
            objectName: "canvasArea"
        }
    }
    property Component canvasComponent: canvasComponentItem

    KpDialog {
        id: saveWorkspaceDialog
        objectName: "saveWorkspaceDialog"
        heading: qsTr("Save workspace layout")
        bodyWidth: 420
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("Panels, splits, tab order and floating windows are stored under this name and can be restored from View > Workspace.")
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            },
            KpTextField {
                id: workspaceNameField
                objectName: "workspaceNameField"
                Layout.fillWidth: true
                placeholder: qsTr("Layout name")
            }
        ]
        actions: [
            KpButton { label: qsTr("Cancel"); onClicked: saveWorkspaceDialog.reject() },
            KpButton {
                label: qsTr("Save")
                variant: "primary"
                enabled: workspaceNameField.text.trim().length > 0
                onClicked: {
                    if (Dock.savePreset(workspaceNameField.text)) {
                        workspaceNameField.text = ""
                        saveWorkspaceDialog.accept()
                    }
                }
            }
        ]
    }
    KpDialog {
        id: recoverySettingsDialog
        objectName: "recoverySettingsDialog"
        heading: qsTr("Automatic recovery")
        bodyWidth: 420
        closePolicy: Popup.NoAutoClose
        note: qsTr("Never disables periodic crash protection for unsaved work. Recovery snapshots do not overwrite your project files.")
        onAboutToShow: {
            const saved = Number(Settings.value("recovery/intervalMinutes", 10))
            const values = [5, 10, 15, 30, 60, 0]
            const found = values.indexOf(saved)
            recoveryInterval.currentIndex = found < 0 ? 1 : found
        }
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("Save separate recovery snapshots:")
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            },
            ComboBox {
                id: recoveryInterval
                objectName: "recoveryIntervalCombo"
                Layout.fillWidth: true
                implicitHeight: KP.controlHeight + 4
                font.pixelSize: KP.fontS
                model: [qsTr("Every 5 minutes"), qsTr("Every 10 minutes"), qsTr("Every 15 minutes"), qsTr("Every 30 minutes"), qsTr("Every 60 minutes"), qsTr("Never")]
                background: Rectangle {
                    color: KP.inputBg
                    border.width: 1
                    border.color: recoveryInterval.hovered || recoveryInterval.popup.visible ? KP.borderLight : KP.border
                    radius: KP.radiusS
                }
                contentItem: Text {
                    leftPadding: KP.gapM
                    text: recoveryInterval.displayText
                    color: KP.text
                    font.pixelSize: KP.fontS
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                indicator: KpGlyph {
                    x: recoveryInterval.width - width - KP.gapM
                    y: (recoveryInterval.height - height) / 2
                    name: "chevron-down"
                    size: 12
                    color: KP.iconNormal
                }
            }
        ]
        actions: [
            KpButton {
                label: qsTr("OK")
                variant: "primary"
                onClicked: {
                    const minutes = [5, 10, 15, 30, 60, 0][recoveryInterval.currentIndex]
                    Settings.setValue("recovery/intervalMinutes", minutes)
                    Settings.setValue("recovery/configured", true)
                    if (!Settings.sync()) {
                        fileErrorText.text = qsTr("Could not save the recovery preference. Please check your settings storage and try again.")
                        fileErrorDialog.open()
                        return
                    }
                    recoverySettingsDialog.accept()
                    root.beginRecovery()
                }
            }
        ]
    }
    // Crash recovery: one row per recoverable project, each with its own
    // Recover and Discard. Nothing here ever writes to a saved project file.
    KpDialog {
        id: recoveryChoiceDialog
        objectName: "recoveryChoiceDialog"
        heading: qsTr("Recover unsaved work")
        bodyWidth: Math.min(root.width - 80, 640)
        closePolicy: Popup.NoAutoClose
        note: qsTr("Recovering opens the copy in its own tab. Your saved files are not changed unless you then choose Save. Undo history starts empty.")
        Connections {
            target: Workspace
            function onRecoveryAvailableChanged() { if (!Workspace.recoveryAvailable) recoveryChoiceDialog.close() }
        }
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("KomaPaint has recovery copies of the projects below.")
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            },
            ListView {
                id: recoveryList
                objectName: "recoveryEntryList"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 320)
                clip: true
                spacing: KP.gapS
                model: Workspace.recoveryEntries
                ScrollBar.vertical: KpScrollBar { }
                delegate: Rectangle {
                    id: recoveryRow
                    required property var modelData
                    required property int index
                    objectName: "recoveryEntry" + index
                    width: ListView.view.width
                    height: recoveryRowLayout.implicitHeight + 16
                    color: KP.paneBgAlt
                    border.width: 1
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
                            Text {
                                objectName: "recoveryEntryTitle" + recoveryRow.index
                                text: recoveryRow.modelData.title
                                color: KP.text
                                font.pixelSize: KP.fontS + 1
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                objectName: "recoveryEntryKind" + recoveryRow.index
                                text: recoveryRow.modelData.saved
                                      ? qsTr("Saved file: %1").arg(recoveryRow.modelData.fileName)
                                      : qsTr("Never saved")
                                color: KP.textMuted
                                font.pixelSize: KP.fontS
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: recoveryRow.modelData.saved
                                text: recoveryRow.modelData.originalPath
                                color: KP.textSubtle
                                font.pixelSize: KP.fontXS
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Text {
                                text: root.recoveryNote(recoveryRow.modelData)
                                visible: text.length > 0
                                color: recoveryRow.modelData.status === "damaged" ? KP.error : KP.textMuted
                                font.pixelSize: KP.fontXS
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        KpButton {
                            objectName: "recoverEntryButton" + recoveryRow.index
                            label: qsTr("Recover")
                            small: true
                            Layout.alignment: Qt.AlignVCenter
                            onClicked: Workspace.recoverEntry(recoveryRow.modelData.key)
                        }
                        KpButton {
                            objectName: "discardEntryButton" + recoveryRow.index
                            label: qsTr("Discard")
                            small: true
                            Layout.alignment: Qt.AlignVCenter
                            onClicked: {
                                recoveryDiscardConfirmation.entryKey = recoveryRow.modelData.key
                                recoveryDiscardConfirmation.entryTitle = recoveryRow.modelData.title
                                recoveryDiscardConfirmation.open()
                            }
                        }
                    }
                }
            }
        ]
        actions: [
            KpButton {
                objectName: "notNowRecoveryButton"
                label: qsTr("Decide later")
                onClicked: recoveryChoiceDialog.reject()
            },
            KpButton {
                objectName: "restoreRecoveryButton"
                label: qsTr("Recover all")
                variant: "primary"
                onClicked: Workspace.restoreWorkspace()
            }
        ]
    }
    KpDialog {
        id: recoveryDiscardConfirmation
        objectName: "recoveryDiscardConfirmation"
        heading: qsTr("Discard recovery copy?")
        bodyWidth: 400
        property string entryKey: ""
        property string entryTitle: ""
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("Permanently discard the recovery copy of \"%1\"? This cannot be undone. Saved project files will not be changed.").arg(recoveryDiscardConfirmation.entryTitle)
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("Cancel"); onClicked: recoveryDiscardConfirmation.reject() },
            KpButton { label: qsTr("Discard"); variant: "danger"; onClicked: recoveryDiscardConfirmation.accept() }
        ]
        onAccepted: Workspace.discardEntry(entryKey)
    }
    KpDialog {
        id: saveChangesDialog
        objectName: "saveChangesDialog"
        heading: qsTr("Save changes?")
        bodyWidth: 400
        closePolicy: Popup.NoAutoClose
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("Save changes to %1?").arg(Workspace.document ? Workspace.document.title : qsTr("Untitled"))
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        // The answer is acted on one event-loop turn after the dialog has
        // closed. Asking about the next project means opening this same
        // dialog again, and re-opening it from inside its own button handler
        // left it closed: the second question never appeared, so the
        // application could not be closed with two unsaved projects.
        property string decision: ""
        actions: [
            KpButton {
                objectName: "saveChangesCancelButton"
                label: qsTr("Cancel")
                onClicked: { saveChangesDialog.decision = "cancel"; saveChangesDialog.close() }
            },
            KpButton {
                objectName: "saveChangesDiscardButton"
                label: qsTr("Don't Save")
                onClicked: { saveChangesDialog.decision = "discard"; saveChangesDialog.close() }
            },
            KpButton {
                objectName: "saveChangesSaveButton"
                label: qsTr("Save")
                variant: "primary"
                onClicked: { saveChangesDialog.decision = "save"; saveChangesDialog.close() }
            }
        ]
        onClosed: Qt.callLater(root.applyCloseAnswer)
    }
    KpDialog {
        id: clearHistoryDialog
        heading: qsTr("Clear artwork history?")
        bodyWidth: 400
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("This frees history memory but removes all artwork undo and redo steps. The artwork itself will not change.")
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("Cancel"); onClicked: clearHistoryDialog.reject() },
            KpButton {
                label: qsTr("Clear history")
                variant: "danger"
                onClicked: { Workspace.history.clear(); clearHistoryDialog.accept() }
            }
        ]
    }
    KpDialog {
        id: discardForNew
        objectName: "discardForNewDialog"
        heading: qsTr("Discard unsaved changes?")
        bodyWidth: 400
        body: [
            Text {
                Layout.fillWidth: true
                text: qsTr("Your artwork has unsaved changes. Creating a new project will permanently discard them.")
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("Cancel"); onClicked: discardForNew.reject() },
            KpButton {
                label: qsTr("Discard and continue")
                variant: "danger"
                onClicked: { discardForNew.accept(); newProjectDialog.open() }
            }
        ]
    }
    CreateBrushTipDialog {
        id: brushTipDialog
        onCreateRequested: function(continuePrevious) { root.createBrushTipFromDialog(continuePrevious) }
    }
    NewProjectDialog {
        id: newProjectDialog
    }
    KomaSettingsDialog {
        id: komaSettingsDialog
    }
    Connections {
        target: Workspace
        function onOpenBrushEngineSettingsRequested() {
            komaSettingsDialog.openCategory("brushEngine")
        }
    }

    PreferencesDialog {
        id: preferencesDialog
    }

    // --------------------------------------------- Undo history under pressure
    // When the machine runs low on memory in the middle of a stroke, the oldest
    // undo steps are dropped so the artwork itself keeps being recorded. That is
    // reported here, once, as a quiet line at the bottom of the window: a modal
    // dialog would interrupt the line the artist is drawing.
    Item {
        id: historyTrimToast
        objectName: "historyTrimToast"
        anchors.fill: parent
        z: 1000
        opacity: 0
        visible: opacity > 0
        property string message: ""

        function show(text) {
            historyTrimToast.message = text
            historyTrimToast.opacity = 1
            trimHideTimer.restart()
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: KP.gapXL
            width: Math.min(historyTrimToast.width - 2 * KP.gapXL, 560)
            height: trimText.implicitHeight + 2 * KP.gapS
            radius: KP.radiusS
            color: KP.paneHeaderBg
            border.width: 1
            border.color: KP.hairline

            Text {
                id: trimText
                anchors.centerIn: parent
                width: parent.width - 2 * KP.gapM
                text: historyTrimToast.message
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Behavior on opacity { NumberAnimation { duration: 180 } }
        Timer {
            id: trimHideTimer
            interval: 4500
            onTriggered: historyTrimToast.opacity = 0
        }
    }
    Connections {
        target: Workspace.history
        function onHistoryTrimmed(discarded) {
            historyTrimToast.show(qsTr("%1 older undo step(s) were dropped to free memory. Your artwork is safe; save the file if you still need those steps.")
                                  .arg(discarded))
        }
    }

    // ------------------------------------------------------------- About dialog
    KpDialog {
        id: aboutDialog
        objectName: "aboutDialog"
        heading: qsTr("About KomaPaint")
        bodyWidth: 420
        body: [
            RowLayout {
                Layout.fillWidth: true
                spacing: KP.gapM
                KpGlyph { name: "brush"; size: 20; color: KP.textAccent }
                Text {
                    Layout.fillWidth: true
                    text: App.versionDisplay
                    color: KP.text
                    font.pixelSize: KP.fontXL
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            },
            Text {
                Layout.fillWidth: true
                text: "Professional 2D digital painting and animation.\n\nThis is an early alpha build. Brush painting, layers, project tabs, saving and automatic recovery work. Selection, transform, fill, text, shape and animation tools are not implemented yet."
                color: KP.text
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            },
            Text {
                Layout.fillWidth: true
                text: "© " + App.copyrightYear + " KomaPaint contributors. All rights reserved."
                color: KP.textSubtle
                font.pixelSize: KP.fontXS
                wrapMode: Text.WordWrap
            }
        ]
        actions: [
            KpButton { label: qsTr("Close"); variant: "primary"; onClicked: aboutDialog.accept() }
        ]
    }
}
