import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

KpDialog {
    id: dialog
    objectName: "komaSettingsDialog"
    heading: qsTr("Koma Settings")
    bodyWidth: 820
    note: qsTr("Changes are staged until Apply. The default tool applies to new projects; it does not change the active project.")

    readonly property var categories: [
        { id: "general", label: qsTr("General") },
        { id: "interface", label: qsTr("Interface") },
        { id: "canvas", label: qsTr("Canvas") },
        { id: "brushEngine", label: qsTr("Brush Engine") },
        { id: "tabletInput", label: qsTr("Tablet & Stylus") },
        { id: "shortcuts", label: qsTr("Keyboard Shortcuts") },
        { id: "performance", label: qsTr("Performance & Storage") }
    ]
    readonly property var toolIds: ["brush", "pen", "pencil", "airbrush", "eraser", "zoom", "hand", "eyedropper"]
    readonly property var toolLabels: [qsTr("Brush"), qsTr("Pen"), qsTr("Pencil"), qsTr("Airbrush"),
                                       qsTr("Eraser"), qsTr("Zoom"), qsTr("Hand"), qsTr("Eyedropper")]
    readonly property var recoveryIntervals: [5, 10, 15, 30, 60, 0]
    readonly property var recoveryLabels: [qsTr("Every 5 minutes"), qsTr("Every 10 minutes"),
                                           qsTr("Every 15 minutes"), qsTr("Every 30 minutes"),
                                           qsTr("Every 60 minutes"), qsTr("Never")]
    readonly property var windowsProtocolIds: ["auto", "windows_ink", "wintab"]
    readonly property var windowsProtocolLabels: [
        qsTr("Automatic (Windows Ink + WinTab fallback)"),
        qsTr("Windows Ink (WM_POINTER)"),
        qsTr("WinTab (wintab32.dll)")
    ]
    readonly property var touchModeIds: ["stylus_nav_only", "allow_all", "disable_touch"]
    readonly property var touchModeLabels: [
        qsTr("Stylus paints · Fingers pan/zoom/rotate"),
        qsTr("Allow finger & stylus painting"),
        qsTr("Disable touch on canvas")
    ]
    readonly property var barrelActionIds: ["right_click", "pan", "eyedropper", "eraser", "undo", "none"]
    readonly property var barrelActionLabels: [
        qsTr("Context / Right-Click"),
        qsTr("Pan View (Hold)"),
        qsTr("Eyedropper (Pick Colour)"),
        qsTr("Eraser (Hold)"),
        qsTr("Undo Stroke"),
        qsTr("Disabled")
    ]
    readonly property var shortcutCategories: [
        qsTr("All"), qsTr("File"), qsTr("Edit"), qsTr("Select"),
        qsTr("View"), qsTr("Tools"), qsTr("Brush"), qsTr("Layers")
    ]

    property int categoryIndex: 0
    property int pendingCategoryIndex: -1
    property string shortcutSearch: ""
    property string shortcutCategoryFilter: "All"
    property string capturingActionId: ""
    readonly property bool capturingShortcut: capturingActionId.length > 0
    property string pendingConflictActionId: ""
    property string pendingConflictSeq: ""
    property string pendingConflictOwnerLabel: ""

    property var draft: ({
        theme: "dark",
        defaultTool: "brush",
        brushEngine: "digital",
        realisticEngineEnabled: false,
        recoveryInterval: 10,
        input: ({}),
        performance: ({}),
        shortcuts: ({})
    })
    property string validationMessage: ""
    property real testPadPressure: 0.0
    property real testPadTiltX: 0.0
    property real testPadTiltY: 0.0
    property real testPadRotation: 0.0
    property bool testPadEraser: false
    property string testPadDevice: qsTr("Press or hover here with stylus/mouse to test")

    function openCategory(categoryId) {
        let target = -1
        for (let i = 0; i < categories.length; ++i) {
            if (categories[i].id === categoryId) { target = i; break }
        }
        pendingCategoryIndex = target
        open()
    }

    function defaultInputDraft() {
        return {
            windowsTabletMode: "auto",
            windowsDisableWmPointer: false,
            pressureThreshold: 0.02,
            pressureMax: 1.0,
            pressureGamma: 1.0,
            pressureCurveLow: 0.25,
            pressureCurveMid: 0.50,
            pressureCurveHigh: 0.75,
            stabilizationDefault: 0,
            touchMode: "stylus_nav_only",
            touchPinchZoom: true,
            touchRotate: true,
            palmRejection: true,
            barrelButton1Action: "right_click",
            barrelButton2Action: "pan",
            eraserEndEnabled: true,
            hoverOutlineEnabled: true,
            tiltCursorEnabled: true
        }
    }

    function defaultPerformanceDraft() {
        return {
            tileCacheLimitMB: 256,
            // 0 = unlimited. History grows with available memory, not a quota.
            undoHistoryLimit: 0,
            wetSimEnabled: true,
            wetSimIntervalMs: 200
        }
    }

    function loadInputDraft() {
        const d = defaultInputDraft()
        const rawMode = String(Settings.value("input/windowsTabletMode", d.windowsTabletMode))
        const rawTouch = String(Settings.value("input/touchMode", d.touchMode))
        const rawB1 = String(Settings.value("input/barrelButton1Action", d.barrelButton1Action))
        const rawB2 = String(Settings.value("input/barrelButton2Action", d.barrelButton2Action))
        const asBool = function(k, defVal) {
            const v = Settings.value(k, defVal)
            return v === true || String(v).toLowerCase() === "true"
        }
        return {
            windowsTabletMode: windowsProtocolIds.indexOf(rawMode) >= 0 ? rawMode : d.windowsTabletMode,
            windowsDisableWmPointer: asBool("input/windowsDisableWmPointer", d.windowsDisableWmPointer),
            pressureThreshold: Number(Settings.value("input/pressureThreshold", d.pressureThreshold)),
            pressureMax: Number(Settings.value("input/pressureMax", d.pressureMax)),
            pressureGamma: Number(Settings.value("input/pressureGamma", d.pressureGamma)),
            pressureCurveLow: Number(Settings.value("input/pressureCurveLow", d.pressureCurveLow)),
            pressureCurveMid: Number(Settings.value("input/pressureCurveMid", d.pressureCurveMid)),
            pressureCurveHigh: Number(Settings.value("input/pressureCurveHigh", d.pressureCurveHigh)),
            stabilizationDefault: Number(Settings.value("input/stabilizationDefault", d.stabilizationDefault)),
            touchMode: touchModeIds.indexOf(rawTouch) >= 0 ? rawTouch : d.touchMode,
            touchPinchZoom: asBool("input/touchPinchZoom", d.touchPinchZoom),
            touchRotate: asBool("input/touchRotate", d.touchRotate),
            palmRejection: asBool("input/palmRejection", d.palmRejection),
            barrelButton1Action: barrelActionIds.indexOf(rawB1) >= 0 ? rawB1 : d.barrelButton1Action,
            barrelButton2Action: barrelActionIds.indexOf(rawB2) >= 0 ? rawB2 : d.barrelButton2Action,
            eraserEndEnabled: asBool("input/eraserEndEnabled", d.eraserEndEnabled),
            hoverOutlineEnabled: asBool("input/hoverOutlineEnabled", d.hoverOutlineEnabled),
            tiltCursorEnabled: asBool("input/tiltCursorEnabled", d.tiltCursorEnabled)
        }
    }

    function loadPerformanceDraft() {
        const d = defaultPerformanceDraft()
        const asBool = function(k, defVal) {
            const v = Settings.value(k, defVal)
            return v === true || String(v).toLowerCase() === "true"
        }
        const cache = Number(Settings.value("performance/tileCacheLimitMB", d.tileCacheLimitMB))
        const undo = Number(Settings.value("performance/undoHistoryLimit", d.undoHistoryLimit))
        const undoSteps = isNaN(undo) ? 0 : Math.max(0, Math.round(undo))
        const wetMs = Number(Settings.value("performance/wetSimIntervalMs", d.wetSimIntervalMs))
        return {
            tileCacheLimitMB: Math.max(64, Math.min(2048, isNaN(cache) ? d.tileCacheLimitMB : cache)),
            undoHistoryLimit: undoSteps,
            wetSimEnabled: asBool("performance/wetSimEnabled", d.wetSimEnabled),
            wetSimIntervalMs: Math.max(100, Math.min(1000, isNaN(wetMs) ? d.wetSimIntervalMs : wetMs))
        }
    }

    function loadShortcutsDraft() {
        if (!Workspace.shortcuts) return {}
        return Object.assign({}, Workspace.shortcuts.bindings || {})
    }

    function filteredShortcutActions() {
        if (!Workspace.shortcuts) return []
        const all = Workspace.shortcuts.actions || []
        const q = shortcutSearch.trim().toLowerCase()
        const cat = shortcutCategoryFilter
        const out = []
        for (let i = 0; i < all.length; ++i) {
            const a = all[i]
            if (cat !== "All" && a.category !== cat) continue
            if (q.length > 0) {
                const hay = (a.label + " " + a.category + " " + (draft.shortcuts[a.id] || "")).toLowerCase()
                if (hay.indexOf(q) < 0) continue
            }
            out.push(a)
        }
        return out
    }

    function loadDraft() {
        const savedTheme = String(Settings.value("ui/theme", "dark"))
        const savedTool = String(Settings.value("tools/defaultTool", "brush"))
        const rawEnabled = Settings.value("tools/realisticEngineEnabled", Workspace.realisticEngineEnabled)
        const savedRealisticEnabled = rawEnabled === true || String(rawEnabled).toLowerCase() === "true"
        const savedEngine = String(Settings.value("tools/brushEngine", Workspace.brushEngine)).toLowerCase()
        const savedInterval = Number(Settings.value("recovery/intervalMinutes", 10))
        draft = {
            theme: savedTheme === "light" ? "light" : "dark",
            defaultTool: toolIds.indexOf(savedTool) >= 0 ? savedTool : "brush",
            realisticEngineEnabled: savedRealisticEnabled,
            brushEngine: (savedEngine === "realistic" && savedRealisticEnabled) ? "realistic" : "digital",
            recoveryInterval: recoveryIntervals.indexOf(savedInterval) >= 0 ? savedInterval : 10,
            input: loadInputDraft(),
            performance: loadPerformanceDraft(),
            shortcuts: loadShortcutsDraft()
        }
        if (Workspace.inputSettings) Workspace.inputSettings.refreshDevices()
        categoryIndex = pendingCategoryIndex >= 0 ? pendingCategoryIndex : 0
        pendingCategoryIndex = -1
        capturingActionId = ""
        pendingConflictActionId = ""
        pendingConflictSeq = ""
        pendingConflictOwnerLabel = ""
        validationMessage = ""
    }

    function setDraft(key, value) {
        const next = Object.assign({}, draft)
        next[key] = value
        if (key === "realisticEngineEnabled" && !value && next.brushEngine === "realistic") {
            validationMessage = qsTr("Switch the active brush engine back to Digital before disabling Realistic.")
            return
        }
        if (key === "brushEngine" && value === "realistic" && !next.realisticEngineEnabled) {
            validationMessage = qsTr("Enable Realistic brushes above before selecting the Realistic engine.")
            return
        }
        draft = next
        validationMessage = ""
    }

    function setInputDraft(key, value) {
        const nextInput = Object.assign({}, draft.input || defaultInputDraft())
        nextInput[key] = value
        if (key === "pressureThreshold" && nextInput.pressureMax < value + 0.15) {
            nextInput.pressureMax = Math.min(1.0, value + 0.15)
        }
        if (key === "pressureMax" && value < nextInput.pressureThreshold + 0.15) {
            nextInput.pressureMax = Math.min(1.0, nextInput.pressureThreshold + 0.15)
        }
        if (key === "pressureCurveLow") {
            if (nextInput.pressureCurveMid < value) nextInput.pressureCurveMid = value
            if (nextInput.pressureCurveHigh < nextInput.pressureCurveMid) nextInput.pressureCurveHigh = nextInput.pressureCurveMid
        }
        if (key === "pressureCurveMid") {
            if (value < nextInput.pressureCurveLow) nextInput.pressureCurveMid = nextInput.pressureCurveLow
            if (nextInput.pressureCurveHigh < nextInput.pressureCurveMid) nextInput.pressureCurveHigh = nextInput.pressureCurveMid
        }
        if (key === "pressureCurveHigh" && value < nextInput.pressureCurveMid) {
            nextInput.pressureCurveHigh = nextInput.pressureCurveMid
        }
        setDraft("input", nextInput)
        if (pressureCurveCanvas) pressureCurveCanvas.requestPaint()
    }

    function setPerformanceDraft(key, value) {
        const nextPerf = Object.assign({}, draft.performance || defaultPerformanceDraft())
        nextPerf[key] = value
        setDraft("performance", nextPerf)
    }

    function setShortcutDraft(actionId, seq, replaceConflict) {
        const nextMap = Object.assign({}, draft.shortcuts || {})
        const norm = Workspace.shortcuts ? Workspace.shortcuts.normalizeSequence(seq) : seq
        if (seq.length > 0 && norm.length === 0) {
            validationMessage = qsTr("Unsupported key sequence.")
            return false
        }
        if (norm.length > 0 && Workspace.shortcuts) {
            const owner = Workspace.shortcuts.findConflict(actionId, norm, nextMap)
            if (owner.length > 0 && !replaceConflict) {
                pendingConflictActionId = actionId
                pendingConflictSeq = norm
                pendingConflictOwnerLabel = owner
                return false
            }
            if (owner.length > 0 && replaceConflict) {
                const keys = Object.keys(nextMap)
                for (let i = 0; i < keys.length; ++i) {
                    if (keys[i] !== actionId && Workspace.shortcuts.normalizeSequence(String(nextMap[keys[i]] || "")) === norm) {
                        nextMap[keys[i]] = ""
                    }
                }
            }
        }
        nextMap[actionId] = norm
        pendingConflictActionId = ""
        pendingConflictSeq = ""
        pendingConflictOwnerLabel = ""
        setDraft("shortcuts", nextMap)
        return true
    }

    function restoreDraftDefaults() {
        draft = {
            theme: "dark",
            defaultTool: "brush",
            brushEngine: "digital",
            realisticEngineEnabled: false,
            recoveryInterval: 10,
            input: defaultInputDraft(),
            performance: defaultPerformanceDraft(),
            shortcuts: Workspace.shortcuts ? Object.assign({}, Workspace.shortcuts.defaultBindingsMap()) : ({})
        }
        capturingActionId = ""
        pendingConflictActionId = ""
        pendingConflictSeq = ""
        pendingConflictOwnerLabel = ""
        validationMessage = ""
        if (pressureCurveCanvas) pressureCurveCanvas.requestPaint()
    }

    function applyDraft() {
        if (toolIds.indexOf(draft.defaultTool) < 0
            || (draft.brushEngine !== "digital" && draft.brushEngine !== "realistic")
            || (!draft.realisticEngineEnabled && draft.brushEngine === "realistic")
            || recoveryIntervals.indexOf(Number(draft.recoveryInterval)) < 0
            || (draft.theme !== "dark" && draft.theme !== "light")) {
            validationMessage = qsTr("One or more settings are invalid. Review the selected values and try again.")
            return false
        }
        const inputDraft = Object.assign(defaultInputDraft(), draft.input || {})
        const perfDraft = Object.assign(defaultPerformanceDraft(), draft.performance || {})
        const oldTheme = Settings.value("ui/theme", "dark")
        const oldTool = Settings.value("tools/defaultTool", "brush")
        const oldEnabled = Settings.value("tools/realisticEngineEnabled", false)
        const oldEngine = Settings.value("tools/brushEngine", "digital")
        const oldInterval = Settings.value("recovery/intervalMinutes", 10)
        const oldConfigured = Settings.value("recovery/configured", false)

        Settings.setValue("ui/theme", draft.theme)
        Settings.setValue("tools/defaultTool", draft.defaultTool)
        Settings.setValue("tools/realisticEngineEnabled", Boolean(draft.realisticEngineEnabled))
        Settings.setValue("tools/brushEngine", draft.brushEngine)
        Settings.setValue("recovery/intervalMinutes", Number(draft.recoveryInterval))
        Settings.setValue("recovery/configured", true)
        Settings.setValue("input/windowsTabletMode", inputDraft.windowsTabletMode)
        Settings.setValue("input/windowsDisableWmPointer", Boolean(inputDraft.windowsDisableWmPointer))
        Settings.setValue("input/pressureThreshold", Number(inputDraft.pressureThreshold))
        Settings.setValue("input/pressureMax", Number(inputDraft.pressureMax))
        Settings.setValue("input/pressureGamma", Number(inputDraft.pressureGamma))
        Settings.setValue("input/pressureCurveLow", Number(inputDraft.pressureCurveLow))
        Settings.setValue("input/pressureCurveMid", Number(inputDraft.pressureCurveMid))
        Settings.setValue("input/pressureCurveHigh", Number(inputDraft.pressureCurveHigh))
        Settings.setValue("input/stabilizationDefault", Number(inputDraft.stabilizationDefault))
        Settings.setValue("input/touchMode", inputDraft.touchMode)
        Settings.setValue("input/touchPinchZoom", Boolean(inputDraft.touchPinchZoom))
        Settings.setValue("input/touchRotate", Boolean(inputDraft.touchRotate))
        Settings.setValue("input/palmRejection", Boolean(inputDraft.palmRejection))
        Settings.setValue("input/barrelButton1Action", inputDraft.barrelButton1Action)
        Settings.setValue("input/barrelButton2Action", inputDraft.barrelButton2Action)
        Settings.setValue("input/eraserEndEnabled", Boolean(inputDraft.eraserEndEnabled))
        Settings.setValue("input/hoverOutlineEnabled", Boolean(inputDraft.hoverOutlineEnabled))
        Settings.setValue("input/tiltCursorEnabled", Boolean(inputDraft.tiltCursorEnabled))
        Settings.setValue("performance/tileCacheLimitMB", Number(perfDraft.tileCacheLimitMB))
        Settings.setValue("performance/undoHistoryLimit", Number(perfDraft.undoHistoryLimit))
        Settings.setValue("performance/wetSimEnabled", Boolean(perfDraft.wetSimEnabled))
        Settings.setValue("performance/wetSimIntervalMs", Number(perfDraft.wetSimIntervalMs))

        if (Workspace.shortcuts && !Workspace.shortcuts.applyBindings(draft.shortcuts || {})) {
            validationMessage = qsTr("Shortcut bindings contain duplicate keys or could not be saved.")
            return false
        }
        if (!Settings.sync()) {
            Settings.setValue("ui/theme", oldTheme)
            Settings.setValue("tools/defaultTool", oldTool)
            Settings.setValue("tools/realisticEngineEnabled", oldEnabled)
            Settings.setValue("tools/brushEngine", oldEngine)
            Settings.setValue("recovery/intervalMinutes", oldInterval)
            Settings.setValue("recovery/configured", oldConfigured)
            Settings.sync()
            validationMessage = qsTr("Settings could not be saved. Check the application settings location and try again.")
            return false
        }
        Workspace.defaultTool = draft.defaultTool
        if (draft.realisticEngineEnabled) {
            Workspace.realisticEngineEnabled = true
            Workspace.brushEngine = draft.brushEngine
        } else {
            Workspace.brushEngine = "digital"
            Workspace.realisticEngineEnabled = false
        }
        if (Workspace.inputSettings) Workspace.inputSettings.applyVariantMap(inputDraft)
        if (Workspace.performanceSettings) Workspace.performanceSettings.applyVariantMap(perfDraft)
        Workspace.setRecoveryInterval(Number(draft.recoveryInterval))
        KP.mode = draft.theme
        return true
    }

    onOpened: loadDraft()
    onClosed: { capturingActionId = ""; pendingConflictActionId = "" }

    body: [
        RowLayout {
            objectName: "komaSettingsContent"
            Layout.fillWidth: true
            Layout.preferredHeight: 440
            spacing: KP.gapM

            Rectangle {
                Layout.preferredWidth: 184
                Layout.fillHeight: true
                color: KP.paneBgAlt
                border.width: 1
                border.color: KP.hairline
                radius: KP.radiusS
                ListView {
                    id: categoryList
                    objectName: "komaSettingsCategories"
                    anchors.fill: parent
                    anchors.margins: KP.gapXS
                    clip: true
                    model: dialog.categories
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: AbstractButton {
                        id: categoryButton
                        required property int index
                        required property var modelData
                        objectName: "komaSettingsCategory_" + modelData.id
                        width: categoryList.width
                        height: 34
                        hoverEnabled: true
                        Accessible.name: modelData.label
                        onClicked: {
                            dialog.capturingActionId = ""
                            dialog.categoryIndex = index
                        }
                        background: Rectangle {
                            radius: KP.radiusXS
                            color: categoryButton.index === dialog.categoryIndex ? KP.rowSelected
                                 : categoryButton.hovered ? KP.hoverOverlay : "transparent"
                            Rectangle {
                                visible: categoryButton.index === dialog.categoryIndex
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 2
                                color: KP.ember
                            }
                        }
                        contentItem: Text {
                            leftPadding: KP.gapM
                            text: categoryButton.modelData.label
                            color: categoryButton.index === dialog.categoryIndex ? KP.textAccent : KP.text
                            font.pixelSize: KP.fontS
                            font.weight: categoryButton.index === dialog.categoryIndex ? Font.Medium : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
                    ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }
                }
            }

            StackLayout {
                id: pages
                objectName: "komaSettingsPages"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: dialog.categoryIndex

                // 1. General
                ColumnLayout {
                    objectName: "komaSettingsPage_general"
                    spacing: KP.gapM
                    Text { text: qsTr("General"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("KomaPaint opens to an empty workspace. Projects appear only when opened, created, or explicitly recovered.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Default tool"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 140 }
                        ComboBox {
                            id: defaultToolChoice
                            objectName: "komaSettingsDefaultTool"
                            Layout.preferredWidth: 200
                            model: dialog.toolLabels
                            currentIndex: Math.max(0, dialog.toolIds.indexOf(dialog.draft.defaultTool))
                            onActivated: function(index) { dialog.setDraft("defaultTool", dialog.toolIds[index]) }
                            contentItem: Text {
                                leftPadding: KP.gapM; rightPadding: 18
                                text: defaultToolChoice.displayText
                                color: KP.text; font.pixelSize: KP.fontS
                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                            }
                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                            indicator: KpGlyph {
                                x: defaultToolChoice.width - width - 5
                                y: (defaultToolChoice.height - height) / 2
                                name: "chevron-down"; size: 10; color: KP.iconNormal
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Default tool applies to newly created projects; each open project keeps its own active tool.")
                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("File association"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 140 }
                        KpButton {
                            label: qsTr("Associate .koma Files…")
                            onClicked: Workspace.associateWithKomaFiles()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 2. Interface
                ColumnLayout {
                    objectName: "komaSettingsPage_interface"
                    spacing: KP.gapM
                    Text { text: qsTr("Interface"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Theme"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 140 }
                        SegmentedChoice {
                            objectName: "komaSettingsTheme"
                            Layout.preferredWidth: 190
                            options: [qsTr("Dark"), qsTr("Light")]
                            currentIndex: dialog.draft.theme === "light" ? 1 : 0
                            onChosen: function(index) { dialog.setDraft("theme", index === 1 ? "light" : "dark") }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The approved monochrome interface is preserved; this choice selects its dark or light surface palette.")
                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Panel layout"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 140 }
                        KpButton {
                            objectName: "komaSettingsResetDockButton"
                            label: qsTr("Reset Workspace Panels to Default")
                            onClicked: Dock.resetToDefault()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Item { Layout.fillHeight: true }
                }

                // 3. Canvas
                ColumnLayout {
                    objectName: "komaSettingsPage_canvas"
                    spacing: KP.gapM
                    Text { text: qsTr("Canvas"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Canvas dimensions, paper surface, and resolution are document properties stored inside each .koma project.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }
                    CheckBox {
                        objectName: "komaSettingsCanvasHoverOutline"
                        text: qsTr("Show live brush outline cursor on canvas")
                        checked: Boolean((dialog.draft.input || {}).hoverOutlineEnabled)
                        onToggled: dialog.setInputDraft("hoverOutlineEnabled", checked)
                    }
                    CheckBox {
                        objectName: "komaSettingsCanvasTiltCursor"
                        text: qsTr("Tilt-aware brush cursor ellipse")
                        checked: Boolean((dialog.draft.input || {}).tiltCursorEnabled)
                        onToggled: dialog.setInputDraft("tiltCursorEnabled", checked)
                    }
                    Item { Layout.fillHeight: true }
                }

                // 4. Brush Engine
                ColumnLayout {
                    objectName: "komaSettingsPage_brushEngine"
                    spacing: KP.gapM
                    Text { text: qsTr("Brush Engine"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Digital is enabled by default. Enable Realistic below to unlock physical pigment painting and the Realistic brush collection.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }

                    AbstractButton {
                        id: realisticEnableToggle
                        objectName: "realisticEngineEnableToggle"
                        readonly property bool checkedState: Boolean(dialog.draft.realisticEngineEnabled)
                        readonly property bool disableBlocked: checkedState && dialog.draft.brushEngine === "realistic"
                        Layout.fillWidth: true
                        padding: KP.gapM
                        implicitHeight: enableToggleCol.implicitHeight + 2 * KP.gapM
                        hoverEnabled: true
                        Accessible.name: qsTr("Enable Realistic brush engine")
                        onClicked: dialog.setDraft("realisticEngineEnabled", !checkedState)

                        background: Rectangle {
                            radius: KP.radiusS
                            color: realisticEnableToggle.checkedState ? KP.rowSelected
                                 : realisticEnableToggle.hovered ? KP.hoverOverlay : KP.paneBgAlt
                            border.width: 1
                            border.color: realisticEnableToggle.checkedState ? KP.ember : KP.hairline
                        }
                        contentItem: ColumnLayout {
                            id: enableToggleCol
                            spacing: KP.gapXS
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: KP.gapS
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: KP.radiusXS
                                    color: realisticEnableToggle.checkedState ? KP.ember : "transparent"
                                    border.width: 1.5
                                    border.color: realisticEnableToggle.checkedState ? KP.ember : KP.border
                                    Text {
                                        anchors.centerIn: parent
                                        visible: realisticEnableToggle.checkedState
                                        text: "\u2713"
                                        color: KP.textAccent
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Enable Realistic Brush Engine & Collection")
                                    color: realisticEnableToggle.checkedState ? KP.textAccent : KP.text
                                    font.pixelSize: KP.fontS + 1
                                    font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                text: realisticEnableToggle.disableBlocked
                                      ? qsTr("Return the active engine to Digital below before disabling Realistic.")
                                      : qsTr("Unlocks the Realistic tab in Painting Brushes with gouache, watercolour, paper wetting, and Paint Mixer integration.")
                                color: realisticEnableToggle.disableBlocked ? KP.ember : KP.textMuted
                                font.pixelSize: KP.fontXS
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    Repeater {
                        model: [
                            {
                                id: "digital",
                                title: qsTr("Digital (Default)"),
                                summary: qsTr("Fast digital raster painting with pressure curves, bristle/graphite/stipple tips, blending/smudging, and no physical pigment simulation.")
                            },
                            {
                                id: "realistic",
                                title: qsTr("Realistic"),
                                summary: qsTr("Physical material painting with pigment-aware compositions, gouache and watercolour media, paper wetness, brush reservoirs, depletion, and Paint Mixer mixtures.")
                            }
                        ]
                        delegate: AbstractButton {
                            id: engineOption
                            required property var modelData
                            readonly property bool locked: modelData.id === "realistic" && !dialog.draft.realisticEngineEnabled
                            readonly property bool selected: dialog.draft.brushEngine === modelData.id
                            objectName: "brushEngineOption_" + modelData.id
                            Layout.fillWidth: true
                            padding: KP.gapM
                            implicitHeight: engineColumn.implicitHeight + 2 * KP.gapM
                            hoverEnabled: !locked
                            opacity: locked ? 0.55 : 1.0
                            Accessible.name: modelData.title
                            onClicked: dialog.setDraft("brushEngine", modelData.id)

                            background: Rectangle {
                                radius: KP.radiusS
                                color: engineOption.selected ? KP.rowSelected
                                     : (engineOption.hovered && !engineOption.locked) ? KP.hoverOverlay : KP.paneBgAlt
                                border.width: 1
                                border.color: engineOption.selected ? KP.ember : KP.hairline
                            }
                            contentItem: ColumnLayout {
                                id: engineColumn
                                spacing: KP.gapXS
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: KP.gapS
                                    Rectangle {
                                        Layout.preferredWidth: 14
                                        Layout.preferredHeight: 14
                                        radius: 7
                                        color: "transparent"
                                        border.width: engineOption.selected ? 4 : 1.5
                                        border.color: engineOption.selected ? KP.ember : KP.border
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: engineOption.modelData.title
                                        color: engineOption.selected ? KP.textAccent : KP.text
                                        font.pixelSize: KP.fontS + 1
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        visible: engineOption.locked
                                        text: qsTr("Locked — enable above")
                                        color: KP.textSubtle
                                        font.pixelSize: KP.fontXS
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 22
                                    text: engineOption.modelData.summary
                                    color: KP.textMuted
                                    font.pixelSize: KP.fontXS
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Switching engines never converts or destroys existing artwork, material layers, undo history, paper surfaces, presets, or Paint Mixer mixtures.")
                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }

                // 5. Tablet & Stylus
                Item {
                    objectName: "komaSettingsPage_tabletInput"
                    readonly property var inp: dialog.draft.input || dialog.defaultInputDraft()

                    Flickable {
                        anchors.fill: parent
                        clip: true
                        contentWidth: width
                        contentHeight: inputCol.implicitHeight + KP.gapM
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

                        ColumnLayout {
                            id: inputCol
                            width: parent.width - KP.gapM
                            spacing: KP.gapM

                            Text { text: qsTr("Tablet & Stylus"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Uses standard OS and vendor tablet drivers (Huion, XP-Pen, Wacom, Windows Ink, WinTab, macOS, Android) with no KomaPaint-specific drivers.")
                                color: KP.textMuted; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                            }

                            // ---- Windows Tablet Protocol ----
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: protoCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: protoCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapS
                                    Text { text: qsTr("Tablet Protocol (Windows)"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Input mode"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 132 }
                                        ComboBox {
                                            id: winProtoCombo
                                            objectName: "komaSettingsWindowsProtocol"
                                            Layout.fillWidth: true
                                            model: dialog.windowsProtocolLabels
                                            currentIndex: Math.max(0, dialog.windowsProtocolIds.indexOf(String((dialog.draft.input || {}).windowsTabletMode || "auto")))
                                            onActivated: function(idx) { dialog.setInputDraft("windowsTabletMode", dialog.windowsProtocolIds[idx]) }
                                            contentItem: Text {
                                                leftPadding: KP.gapM; rightPadding: 18
                                                text: winProtoCombo.displayText; color: KP.text; font.pixelSize: KP.fontS
                                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                                            }
                                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                                            indicator: KpGlyph { x: winProtoCombo.width - width - 5; y: (winProtoCombo.height - height) / 2; name: "chevron-down"; size: 10; color: KP.iconNormal }
                                        }
                                    }
                                    CheckBox {
                                        objectName: "komaSettingsDisableWmPointer"
                                        text: qsTr("Compatibility: Disable Qt WM_POINTER on Windows (restart required)")
                                        checked: Boolean((dialog.draft.input || {}).windowsDisableWmPointer)
                                        onToggled: dialog.setInputDraft("windowsDisableWmPointer", checked)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: Workspace.inputSettings ? Workspace.inputSettings.winTabStatus : ""
                                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            // ---- Pressure Calibration & Interactive Curve ----
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: pressCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: pressCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapS
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Pressure Curve & Calibration"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        KpButton {
                                            small: true; label: qsTr("Linear Reset")
                                            onClicked: {
                                                dialog.setInputDraft("pressureThreshold", 0.02)
                                                dialog.setInputDraft("pressureMax", 1.0)
                                                dialog.setInputDraft("pressureGamma", 1.0)
                                                dialog.setInputDraft("pressureCurveLow", 0.25)
                                                dialog.setInputDraft("pressureCurveMid", 0.50)
                                                dialog.setInputDraft("pressureCurveHigh", 0.75)
                                            }
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true; spacing: KP.gapM
                                        Rectangle {
                                            Layout.preferredWidth: 168; Layout.preferredHeight: 140
                                            radius: KP.radiusS; color: KP.inputBg; border.width: 1; border.color: KP.border
                                            Canvas {
                                                id: pressureCurveCanvas
                                                objectName: "pressureCurveCanvas"
                                                anchors.fill: parent; anchors.margins: 8
                                                onVisibleChanged: if (visible) requestPaint()
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    const w = width, h = height
                                                    ctx.clearRect(0, 0, w, h)
                                                    ctx.strokeStyle = "#3c3f45"
                                                    ctx.lineWidth = 1
                                                    for (let g = 1; g < 4; ++g) {
                                                        ctx.beginPath(); ctx.moveTo(w * g / 4, 0); ctx.lineTo(w * g / 4, h); ctx.stroke()
                                                        ctx.beginPath(); ctx.moveTo(0, h * g / 4); ctx.lineTo(w, h * g / 4); ctx.stroke()
                                                    }
                                                    const inp = dialog.draft.input || dialog.defaultInputDraft()
                                                    ctx.strokeStyle = "#e07a3d"
                                                    ctx.lineWidth = 2
                                                    ctx.beginPath()
                                                    for (let i = 0; i <= 40; ++i) {
                                                        const x = i / 40.0
                                                        const y = Workspace.inputSettings
                                                            ? Workspace.inputSettings.evaluatePressureCurve(
                                                                x, inp.pressureThreshold, inp.pressureMax,
                                                                inp.pressureGamma, inp.pressureCurveLow,
                                                                inp.pressureCurveMid, inp.pressureCurveHigh)
                                                            : x
                                                        const px = x * w
                                                        const py = (1.0 - y) * h
                                                        if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                                                    }
                                                    ctx.stroke()

                                                    const knots = [
                                                        [0.25, inp.pressureCurveLow],
                                                        [0.50, inp.pressureCurveMid],
                                                        [0.75, inp.pressureCurveHigh]
                                                    ]
                                                    ctx.fillStyle = "#f2f2f2"
                                                    for (let k = 0; k < knots.length; ++k) {
                                                        ctx.beginPath()
                                                        ctx.arc(knots[k][0] * w, (1.0 - knots[k][1]) * h, 4, 0, Math.PI * 2)
                                                        ctx.fill()
                                                    }
                                                }
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                property int activeKnot: -1
                                                function pickKnot(mx) {
                                                    const u = mx / width
                                                    if (u < 0.375) return 0
                                                    if (u < 0.625) return 1
                                                    return 2
                                                }
                                                onPressed: function(m) { activeKnot = pickKnot(m.x); updateKnot(m.y) }
                                                onPositionChanged: function(m) { if (pressed && activeKnot >= 0) updateKnot(m.y) }
                                                onReleased: activeKnot = -1
                                                function updateKnot(my) {
                                                    const v = Math.max(0.02, Math.min(0.98, 1.0 - (my - 8) / Math.max(1, height - 16)))
                                                    if (activeKnot === 0) dialog.setInputDraft("pressureCurveLow", v)
                                                    else if (activeKnot === 1) dialog.setInputDraft("pressureCurveMid", v)
                                                    else if (activeKnot === 2) dialog.setInputDraft("pressureCurveHigh", v)
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true; spacing: KP.gapXS
                                            PropertySlider {
                                                objectName: "komaSettingsPressureThreshold"
                                                Layout.fillWidth: true
                                                label: qsTr("Min threshold")
                                                unit: "%"
                                                from: 0; to: 30
                                                value: Math.round(Number((dialog.draft.input || {}).pressureThreshold || 0.02) * 100)
                                                onEdited: function(v) { dialog.setInputDraft("pressureThreshold", v / 100.0) }
                                            }
                                            PropertySlider {
                                                objectName: "komaSettingsPressureMax"
                                                Layout.fillWidth: true
                                                label: qsTr("Max calibration")
                                                unit: "%"
                                                from: 40; to: 100
                                                value: Math.round(Number((dialog.draft.input || {}).pressureMax || 1.0) * 100)
                                                onEdited: function(v) { dialog.setInputDraft("pressureMax", v / 100.0) }
                                            }
                                            PropertySlider {
                                                objectName: "komaSettingsPressureGamma"
                                                Layout.fillWidth: true
                                                label: qsTr("Curve firmness")
                                                unit: "%"
                                                from: 40; to: 250
                                                value: Math.round(Number((dialog.draft.input || {}).pressureGamma || 1.0) * 100)
                                                onEdited: function(v) { dialog.setInputDraft("pressureGamma", v / 100.0) }
                                            }
                                            PropertySlider {
                                                objectName: "komaSettingsDefaultStabilization"
                                                Layout.fillWidth: true
                                                label: qsTr("Default stabilization")
                                                unit: "%"
                                                from: 0; to: 100
                                                value: Number((dialog.draft.input || {}).stabilizationDefault || 0)
                                                onEdited: function(v) { dialog.setInputDraft("stabilizationDefault", v) }
                                            }
                                        }
                                    }

                                    // Live stylus test strip
                                    Rectangle {
                                        Layout.fillWidth: true; implicitHeight: 46
                                        radius: KP.radiusS; color: KP.inputBg; border.width: 1; border.color: KP.border
                                        ColumnLayout {
                                            anchors.fill: parent; anchors.margins: KP.gapS; spacing: 3
                                            Text {
                                                Layout.fillWidth: true
                                                text: dialog.testPadDevice
                                                color: KP.text; font.pixelSize: KP.fontXS; elide: Text.ElideRight
                                            }
                                            Rectangle {
                                                Layout.fillWidth: true; implicitHeight: 6; radius: 3; color: KP.paneBgAlt
                                                Rectangle {
                                                    height: parent.height; radius: 3; color: dialog.testPadEraser ? KP.ember : KP.textAccent
                                                    width: Math.max(0, Math.min(parent.width, parent.width * dialog.testPadPressure))
                                                }
                                            }
                                        }
                                        PointHandler {
                                            id: testPadHandler
                                            acceptedDevices: PointerDevice.Stylus | PointerDevice.Airbrush | PointerDevice.Mouse
                                            onPointChanged: {
                                                const inp = dialog.draft.input || dialog.defaultInputDraft()
                                                const rawP = testPadHandler.active ? Number(point.pressure || 0) : 0.0
                                                const cal = Workspace.inputSettings
                                                    ? Workspace.inputSettings.evaluatePressureCurve(
                                                        rawP, inp.pressureThreshold, inp.pressureMax,
                                                        inp.pressureGamma, inp.pressureCurveLow,
                                                        inp.pressureCurveMid, inp.pressureCurveHigh)
                                                    : rawP
                                                dialog.testPadPressure = testPadHandler.active ? cal : 0.0
                                                dialog.testPadEraser = Boolean(point.device && point.device.pointerType === PointerDevice.Eraser)
                                                dialog.testPadDevice = qsTr("Live test: pressure %1% (raw %2%) · %3")
                                                    .arg(Math.round(cal * 100))
                                                    .arg(Math.round(rawP * 100))
                                                    .arg(dialog.testPadEraser ? qsTr("Eraser") : qsTr("Pen / Pointer"))
                                            }
                                        }
                                    }
                                }
                            }

                            // ---- Stylus Buttons, Eraser & Touch / Palm ----
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: behavCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: behavCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapS
                                    Text { text: qsTr("Stylus Buttons, Eraser & Touch"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Barrel button 1"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 132 }
                                        ComboBox {
                                            id: barrel1Combo
                                            objectName: "komaSettingsBarrel1"
                                            Layout.fillWidth: true
                                            model: dialog.barrelActionLabels
                                            currentIndex: Math.max(0, dialog.barrelActionIds.indexOf(String((dialog.draft.input || {}).barrelButton1Action || "right_click")))
                                            onActivated: function(idx) { dialog.setInputDraft("barrelButton1Action", dialog.barrelActionIds[idx]) }
                                            contentItem: Text { leftPadding: KP.gapM; rightPadding: 18; text: barrel1Combo.displayText; color: KP.text; font.pixelSize: KP.fontS; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                                            indicator: KpGlyph { x: barrel1Combo.width - width - 5; y: (barrel1Combo.height - height) / 2; name: "chevron-down"; size: 10; color: KP.iconNormal }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Barrel button 2"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 132 }
                                        ComboBox {
                                            id: barrel2Combo
                                            objectName: "komaSettingsBarrel2"
                                            Layout.fillWidth: true
                                            model: dialog.barrelActionLabels
                                            currentIndex: Math.max(0, dialog.barrelActionIds.indexOf(String((dialog.draft.input || {}).barrelButton2Action || "pan")))
                                            onActivated: function(idx) { dialog.setInputDraft("barrelButton2Action", dialog.barrelActionIds[idx]) }
                                            contentItem: Text { leftPadding: KP.gapM; rightPadding: 18; text: barrel2Combo.displayText; color: KP.text; font.pixelSize: KP.fontS; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                                            indicator: KpGlyph { x: barrel2Combo.width - width - 5; y: (barrel2Combo.height - height) / 2; name: "chevron-down"; size: 10; color: KP.iconNormal }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Touch behavior"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 132 }
                                        ComboBox {
                                            id: touchModeCombo
                                            objectName: "komaSettingsTouchMode"
                                            Layout.fillWidth: true
                                            model: dialog.touchModeLabels
                                            currentIndex: Math.max(0, dialog.touchModeIds.indexOf(String((dialog.draft.input || {}).touchMode || "stylus_nav_only")))
                                            onActivated: function(idx) { dialog.setInputDraft("touchMode", dialog.touchModeIds[idx]) }
                                            contentItem: Text { leftPadding: KP.gapM; rightPadding: 18; text: touchModeCombo.displayText; color: KP.text; font.pixelSize: KP.fontS; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                                            indicator: KpGlyph { x: touchModeCombo.width - width - 5; y: (touchModeCombo.height - height) / 2; name: "chevron-down"; size: 10; color: KP.iconNormal }
                                        }
                                    }
                                    GridLayout {
                                        Layout.fillWidth: true; columns: 2; columnSpacing: KP.gapM; rowSpacing: KP.gapXS
                                        CheckBox {
                                            objectName: "komaSettingsEraserEnd"
                                            text: qsTr("Recognize stylus eraser end")
                                            checked: Boolean((dialog.draft.input || {}).eraserEndEnabled)
                                            onToggled: dialog.setInputDraft("eraserEndEnabled", checked)
                                        }
                                        CheckBox {
                                            objectName: "komaSettingsPalmRejection"
                                            text: qsTr("Palm rejection during stylus activity")
                                            checked: Boolean((dialog.draft.input || {}).palmRejection)
                                            onToggled: dialog.setInputDraft("palmRejection", checked)
                                        }
                                        CheckBox {
                                            objectName: "komaSettingsHoverOutline"
                                            text: qsTr("Show hover brush outline")
                                            checked: Boolean((dialog.draft.input || {}).hoverOutlineEnabled)
                                            onToggled: dialog.setInputDraft("hoverOutlineEnabled", checked)
                                        }
                                        CheckBox {
                                            objectName: "komaSettingsTiltCursor"
                                            text: qsTr("Tilt-aware brush outline")
                                            checked: Boolean((dialog.draft.input || {}).tiltCursorEnabled)
                                            onToggled: dialog.setInputDraft("tiltCursorEnabled", checked)
                                        }
                                        CheckBox {
                                            objectName: "komaSettingsTouchPinch"
                                            text: qsTr("Two-finger pinch zoom")
                                            checked: Boolean((dialog.draft.input || {}).touchPinchZoom)
                                            onToggled: dialog.setInputDraft("touchPinchZoom", checked)
                                        }
                                        CheckBox {
                                            objectName: "komaSettingsTouchRotate"
                                            text: qsTr("Two-finger canvas rotation")
                                            checked: Boolean((dialog.draft.input || {}).touchRotate)
                                            onToggled: dialog.setInputDraft("touchRotate", checked)
                                        }
                                    }
                                }
                            }

                            // ---- Diagnostics ----
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: diagCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: diagCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapXS
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Detected Devices & Input Diagnostics"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        KpButton {
                                            small: true; label: qsTr("Refresh")
                                            onClicked: if (Workspace.inputSettings) Workspace.inputSettings.refreshDevices()
                                        }
                                    }
                                    Repeater {
                                        model: Workspace.inputSettings ? Workspace.inputSettings.detectedDevices : []
                                        delegate: Text {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            text: "• " + modelData.name + " (" + modelData.type + ", " + modelData.backend + ") — " + modelData.capabilities
                                            color: KP.textMuted; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: Workspace.inputSettings
                                        text: Workspace.inputSettings
                                            ? qsTr("Last event: %1 (%2) · Rate: %3 Hz · Process median/p95: %4 / %5 µs · Dedup: %6 · Recovered: %7")
                                                .arg(Workspace.inputSettings.liveDiagnostics.backend || "None")
                                                .arg(Workspace.inputSettings.liveDiagnostics.capabilities || "-")
                                                .arg(Math.round(Number(Workspace.inputSettings.liveDiagnostics.sampleRateHz || 0)))
                                                .arg(Number(Workspace.inputSettings.liveDiagnostics.medianUs || 0).toFixed(1))
                                                .arg(Number(Workspace.inputSettings.liveDiagnostics.p95Us || 0).toFixed(1))
                                                .arg(Number(Workspace.inputSettings.liveDiagnostics.droppedDuplicates || 0))
                                                .arg(Number(Workspace.inputSettings.liveDiagnostics.recoveredStrokes || 0))
                                            : ""
                                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }

                // 6. Keyboard Shortcuts
                ColumnLayout {
                    objectName: "komaSettingsPage_shortcuts"
                    spacing: KP.gapS

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Keyboard Shortcuts"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        KpButton {
                            objectName: "komaSettingsResetAllShortcuts"
                            small: true
                            label: qsTr("Reset All Shortcuts")
                            onClicked: {
                                dialog.capturingActionId = ""
                                dialog.pendingConflictActionId = ""
                                dialog.setDraft("shortcuts", Workspace.shortcuts ? Object.assign({}, Workspace.shortcuts.defaultBindingsMap()) : ({}))
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapS
                        KpTextField {
                            id: shortcutSearchInput
                            objectName: "komaSettingsShortcutSearch"
                            Layout.fillWidth: true
                            placeholder: qsTr("Search actions or keys…")
                            text: dialog.shortcutSearch
                            onTextChanged: dialog.shortcutSearch = text
                        }
                        ComboBox {
                            id: shortcutCatCombo
                            objectName: "komaSettingsShortcutCategory"
                            Layout.preferredWidth: 132
                            model: dialog.shortcutCategories
                            currentIndex: Math.max(0, dialog.shortcutCategories.indexOf(dialog.shortcutCategoryFilter))
                            onActivated: function(idx) { dialog.shortcutCategoryFilter = dialog.shortcutCategories[idx] }
                            contentItem: Text { leftPadding: KP.gapM; rightPadding: 18; text: shortcutCatCombo.displayText; color: KP.text; font.pixelSize: KP.fontS; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                            indicator: KpGlyph { x: shortcutCatCombo.width - width - 5; y: (shortcutCatCombo.height - height) / 2; name: "chevron-down"; size: 10; color: KP.iconNormal }
                        }
                    }

                    // Conflict resolution banner
                    Rectangle {
                        objectName: "shortcutConflictBanner"
                        Layout.fillWidth: true
                        visible: dialog.pendingConflictActionId.length > 0
                        implicitHeight: visible ? conflictRow.implicitHeight + 2 * KP.gapS : 0
                        radius: KP.radiusS
                        color: KP.rowSelected
                        border.width: 1
                        border.color: KP.warning
                        RowLayout {
                            id: conflictRow
                            anchors.fill: parent
                            anchors.margins: KP.gapS
                            spacing: KP.gapS
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Shortcut \"%1\" is already assigned to \"%2\". Replace it?")
                                    .arg(dialog.pendingConflictSeq)
                                    .arg(dialog.pendingConflictOwnerLabel)
                                color: KP.text
                                font.pixelSize: KP.fontXS
                                wrapMode: Text.WordWrap
                            }
                            KpButton {
                                objectName: "shortcutConflictReplaceButton"
                                small: true
                                label: qsTr("Replace")
                                variant: "primary"
                                onClicked: dialog.setShortcutDraft(dialog.pendingConflictActionId, dialog.pendingConflictSeq, true)
                            }
                            KpButton {
                                small: true
                                label: qsTr("Cancel")
                                onClicked: { dialog.pendingConflictActionId = ""; dialog.pendingConflictSeq = ""; dialog.pendingConflictOwnerLabel = "" }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: KP.radiusS
                        color: KP.paneBgAlt
                        border.width: 1
                        border.color: KP.hairline

                        ListView {
                            id: shortcutList
                            objectName: "komaSettingsShortcutList"
                            anchors.fill: parent
                            anchors.margins: KP.gapXS
                            clip: true
                            spacing: 2
                            boundsBehavior: Flickable.StopAtBounds
                            model: {
                                // Track draft.shortcuts / search / filter reactively
                                var _deps = [dialog.draft, dialog.shortcutSearch, dialog.shortcutCategoryFilter]
                                return dialog.filteredShortcutActions()
                            }
                            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                id: scRow
                                required property var modelData
                                readonly property string actionId: modelData.id
                                readonly property string boundSeq: String((dialog.draft.shortcuts || {})[actionId] !== undefined
                                    ? (dialog.draft.shortcuts || {})[actionId] : modelData.sequence)
                                readonly property bool capturing: dialog.capturingActionId === actionId
                                width: shortcutList.width - 6
                                height: 30
                                radius: KP.radiusXS
                                color: capturing ? KP.rowSelected : "transparent"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: KP.gapS
                                    anchors.rightMargin: KP.gapXS
                                    spacing: KP.gapS

                                    Text {
                                        Layout.preferredWidth: 56
                                        text: scRow.modelData.category
                                        color: KP.textSubtle
                                        font.pixelSize: KP.fontMicro
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: scRow.modelData.label
                                        color: KP.text
                                        font.pixelSize: KP.fontS
                                        elide: Text.ElideRight
                                    }
                                    AbstractButton {
                                        id: keyCaptureBtn
                                        objectName: "komaSettingsShortcutKey_" + scRow.actionId
                                        Layout.preferredWidth: 132
                                        Layout.preferredHeight: 24
                                        hoverEnabled: true
                                        focusPolicy: Qt.StrongFocus
                                        onClicked: {
                                            dialog.pendingConflictActionId = ""
                                            dialog.capturingActionId = scRow.actionId
                                            keyCaptureBtn.forceActiveFocus()
                                        }
                                        Keys.onPressed: function(event) {
                                            if (!scRow.capturing) return
                                            event.accepted = true
                                            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                                                dialog.capturingActionId = ""
                                                return
                                            }
                                            if (!Workspace.shortcuts) return
                                            const formatted = Workspace.shortcuts.formatKeyEvent(event.key, event.modifiers)
                                            if (formatted.length === 0) return
                                            dialog.capturingActionId = ""
                                            dialog.setShortcutDraft(scRow.actionId, formatted, false)
                                        }
                                        background: Rectangle {
                                            radius: KP.radiusXS
                                            color: scRow.capturing ? KP.emberSoft
                                                 : (keyCaptureBtn.hovered ? KP.inputBgHover : KP.inputBg)
                                            border.width: 1
                                            border.color: scRow.capturing ? KP.ember : KP.border
                                        }
                                        contentItem: Text {
                                            text: scRow.capturing ? qsTr("Press key…")
                                                : (scRow.boundSeq.length > 0 ? scRow.boundSeq : qsTr("Unassigned"))
                                            color: scRow.capturing ? KP.textAccent
                                                 : (scRow.boundSeq.length > 0 ? KP.text : KP.textSubtle)
                                            font.pixelSize: KP.fontXS
                                            font.family: KP.fontMono
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                    IconButton {
                                        objectName: "komaSettingsShortcutClear_" + scRow.actionId
                                        glyph: "close"
                                        iconSize: 10
                                        cell: 22
                                        enabled: scRow.boundSeq.length > 0
                                        tooltip: qsTr("Clear shortcut")
                                        onActivated: dialog.setShortcutDraft(scRow.actionId, "", true)
                                    }
                                    IconButton {
                                        objectName: "komaSettingsShortcutReset_" + scRow.actionId
                                        glyph: "reset"
                                        iconSize: 11
                                        cell: 22
                                        enabled: scRow.boundSeq !== scRow.modelData.defaultSequence
                                        tooltip: qsTr("Restore default (%1)").arg(scRow.modelData.defaultSequence)
                                        onActivated: dialog.setShortcutDraft(scRow.actionId, scRow.modelData.defaultSequence, false)
                                    }
                                }
                            }
                        }
                    }
                }

                // 7. Performance & Storage
                Item {
                    objectName: "komaSettingsPage_performance"
                    Flickable {
                        anchors.fill: parent
                        clip: true
                        contentWidth: width
                        contentHeight: perfCol.implicitHeight + KP.gapM
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

                        ColumnLayout {
                            id: perfCol
                            width: parent.width - KP.gapM
                            spacing: KP.gapM

                            Text { text: qsTr("Performance & Storage"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: memCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: memCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapS
                                    Text { text: qsTr("Memory & History Limits"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold }
                                    PropertySlider {
                                        objectName: "komaSettingsTileCacheLimit"
                                        Layout.fillWidth: true
                                        label: qsTr("Viewport tile cache")
                                        unit: " MB"
                                        from: 64; to: 2048
                                        value: Number((dialog.draft.performance || {}).tileCacheLimitMB || 256)
                                        onEdited: function(v) { dialog.setPerformanceDraft("tileCacheLimitMB", v) }
                                    }
                                    CheckBox {
                                        objectName: "komaSettingsUndoUnlimited"
                                        text: qsTr("Keep undo history while memory allows (recommended)")
                                        checked: Number((dialog.draft.performance || {}).undoHistoryLimit || 0) === 0
                                        onToggled: dialog.setPerformanceDraft(
                                                       "undoHistoryLimit",
                                                       checked ? 0
                                                               : Math.max(10, Number((dialog.draft.performance || {}).undoHistoryLimit || 0) || 200))
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: KP.gapS
                                        enabled: Number((dialog.draft.performance || {}).undoHistoryLimit || 0) > 0
                                        opacity: enabled ? 1 : KP.disabledOpacity
                                        Text {
                                            text: qsTr("Keep at most")
                                            color: KP.textMuted; font.pixelSize: KP.fontS
                                        }
                                        KpSpinBox {
                                            objectName: "komaSettingsUndoLimit"
                                            from: 10; to: 100000; stepSize: 10
                                            value: Number((dialog.draft.performance || {}).undoHistoryLimit || 0)
                                            onValueModified: dialog.setPerformanceDraft("undoHistoryLimit", value)
                                        }
                                        Text {
                                            text: qsTr("steps")
                                            color: KP.textMuted; font.pixelSize: KP.fontS
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("Undo history is not counted in fixed steps by default. When the machine runs out of memory, the oldest steps are dropped so painting can continue, and KomaPaint tells you when that happens.")
                                        color: KP.textMuted; font.pixelSize: KP.fontXS
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: simCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: simCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapS
                                    Text { text: qsTr("Realistic Wet Simulation"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold }
                                    CheckBox {
                                        objectName: "komaSettingsWetSimEnabled"
                                        text: qsTr("Enable background wet pigment drying & diffusion")
                                        checked: Boolean((dialog.draft.performance || {}).wetSimEnabled)
                                        onToggled: dialog.setPerformanceDraft("wetSimEnabled", checked)
                                    }
                                    PropertySlider {
                                        objectName: "komaSettingsWetSimInterval"
                                        Layout.fillWidth: true
                                        enabled: Boolean((dialog.draft.performance || {}).wetSimEnabled)
                                        label: qsTr("Wet simulation interval")
                                        unit: " ms"
                                        from: 100; to: 1000
                                        value: Number((dialog.draft.performance || {}).wetSimIntervalMs || 200)
                                        onEdited: function(v) { dialog.setPerformanceDraft("wetSimIntervalMs", v) }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: recCol.implicitHeight + 2 * KP.gapM
                                radius: KP.radiusS; color: KP.paneBgAlt; border.width: 1; border.color: KP.hairline
                                ColumnLayout {
                                    id: recCol
                                    anchors.fill: parent; anchors.margins: KP.gapM; spacing: KP.gapS
                                    Text { text: qsTr("Files & Crash Recovery"); color: KP.text; font.pixelSize: KP.fontS; font.weight: Font.DemiBold }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: qsTr("Automatic recovery"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 140 }
                                        ComboBox {
                                            id: recoveryChoice
                                            objectName: "komaSettingsRecoveryInterval"
                                            Layout.preferredWidth: 220
                                            model: dialog.recoveryLabels
                                            currentIndex: Math.max(0, dialog.recoveryIntervals.indexOf(Number(dialog.draft.recoveryInterval)))
                                            onActivated: function(index) { dialog.setDraft("recoveryInterval", dialog.recoveryIntervals[index]) }
                                            contentItem: Text {
                                                leftPadding: KP.gapM; rightPadding: 18
                                                text: recoveryChoice.displayText
                                                color: KP.text; font.pixelSize: KP.fontS
                                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                                            }
                                            background: Rectangle { color: KP.inputBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
                                            indicator: KpGlyph {
                                                x: recoveryChoice.width - width - 5
                                                y: (recoveryChoice.height - height) / 2
                                                name: "chevron-down"; size: 10; color: KP.iconNormal
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("Recovery snapshots are separate from project files and never overwrite saved artwork.")
                                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        },
        Text {
            objectName: "komaSettingsValidation"
            Layout.fillWidth: true
            visible: dialog.validationMessage.length > 0
            text: dialog.validationMessage
            color: KP.error
            font.pixelSize: KP.fontXS
            wrapMode: Text.WordWrap
        }
    ]

    actions: [
        KpButton { objectName: "komaSettingsCancel"; label: qsTr("Cancel"); onClicked: dialog.reject() },
        KpButton { objectName: "komaSettingsDefaults"; label: qsTr("Restore Defaults"); onClicked: dialog.restoreDraftDefaults() },
        KpButton {
            objectName: "komaSettingsApply"
            label: qsTr("Apply")
            variant: "primary"
            onClicked: if (dialog.applyDraft()) dialog.accept()
        }
    ]
}
