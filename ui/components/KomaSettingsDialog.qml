import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

KpDialog {
    id: dialog
    objectName: "komaSettingsDialog"
    heading: qsTr("Koma Settings")
    bodyWidth: 790
    note: qsTr("Changes are staged until Apply. The default tool applies to new projects; it does not change the active project.")

    readonly property var categories: [
        { id: "general", label: qsTr("General") },
        { id: "interface", label: qsTr("Interface") },
        { id: "canvas", label: qsTr("Canvas") },
        { id: "tools", label: qsTr("Tools") },
        { id: "brushEngine", label: qsTr("Brush Engine") },
        { id: "performance", label: qsTr("Performance") },
        { id: "inputTablet", label: qsTr("Input / Tablet") },
        { id: "filesRecovery", label: qsTr("Files and Recovery") }
    ]
    readonly property var toolIds: ["brush", "pen", "pencil", "airbrush", "eraser", "zoom", "hand", "eyedropper"]
    readonly property var toolLabels: [qsTr("Brush"), qsTr("Pen"), qsTr("Pencil"), qsTr("Airbrush"),
                                       qsTr("Eraser"), qsTr("Zoom"), qsTr("Hand"), qsTr("Eyedropper")]
    readonly property var recoveryIntervals: [5, 10, 15, 30, 60, 0]
    readonly property var recoveryLabels: [qsTr("Every 5 minutes"), qsTr("Every 10 minutes"),
                                           qsTr("Every 15 minutes"), qsTr("Every 30 minutes"),
                                           qsTr("Every 60 minutes"), qsTr("Never")]
    property int categoryIndex: 0
    property int pendingCategoryIndex: -1
    property var draft: ({ theme: "dark", defaultTool: "brush", brushEngine: "digital", realisticEngineEnabled: false, recoveryInterval: 10 })
    property string validationMessage: ""

    function openCategory(categoryId) {
        let target = -1
        for (let i = 0; i < categories.length; ++i) {
            if (categories[i].id === categoryId) { target = i; break }
        }
        pendingCategoryIndex = target
        open()
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
            recoveryInterval: recoveryIntervals.indexOf(savedInterval) >= 0 ? savedInterval : 10
        }
        categoryIndex = pendingCategoryIndex >= 0 ? pendingCategoryIndex : 0
        pendingCategoryIndex = -1
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
    function restoreDraftDefaults() {
        draft = { theme: "dark", defaultTool: "brush", brushEngine: "digital", realisticEngineEnabled: false, recoveryInterval: 10 }
        validationMessage = ""
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
        Workspace.setRecoveryInterval(Number(draft.recoveryInterval))
        KP.mode = draft.theme
        return true
    }

    onOpened: loadDraft()

    body: [
        RowLayout {
            objectName: "komaSettingsContent"
            Layout.fillWidth: true
            Layout.preferredHeight: 430
            spacing: KP.gapM

            Rectangle {
                Layout.preferredWidth: 176
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
                        onClicked: dialog.categoryIndex = index
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

                ColumnLayout {
                    objectName: "komaSettingsPage_general"
                    spacing: KP.gapM
                    Text { text: qsTr("General"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("KomaPaint opens to an empty workspace. Projects appear only when opened, created, or explicitly recovered.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    objectName: "komaSettingsPage_interface"
                    spacing: KP.gapM
                    Text { text: qsTr("Interface"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Theme"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 128 }
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
                        text: qsTr("The approved monochrome interface is preserved; this choice only selects its dark or light palette.")
                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    objectName: "komaSettingsPage_canvas"
                    spacing: KP.gapM
                    Text { text: qsTr("Canvas"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Canvas dimensions, background, and resolution are document settings and stay with each project. No global canvas override is applied.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    objectName: "komaSettingsPage_tools"
                    spacing: KP.gapM
                    Text { text: qsTr("Tools"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Default tool"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 128 }
                        ComboBox {
                            id: defaultToolChoice
                            objectName: "komaSettingsDefaultTool"
                            Layout.preferredWidth: 190
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
                        text: qsTr("This is used only when creating a project. An open project's current tool remains project-specific.")
                        color: KP.textSubtle; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
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
                ColumnLayout {
                    objectName: "komaSettingsPage_performance"
                    spacing: KP.gapM
                    Text { text: qsTr("Performance"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Tile allocation, undo history, and viewport caching use the application's memory-safety controls. User-adjustable performance options will be added only when they can be validated against those controls.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    objectName: "komaSettingsPage_inputTablet"
                    spacing: KP.gapM
                    Text { text: qsTr("Input / Tablet"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Pressure is used by the active brush. Tablet-specific controls will appear here when they can be changed without overriding per-brush dynamics.")
                        color: KP.textMuted; font.pixelSize: KP.fontS; wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
                ColumnLayout {
                    objectName: "komaSettingsPage_filesRecovery"
                    spacing: KP.gapM
                    Text { text: qsTr("Files and Recovery"); color: KP.text; font.pixelSize: KP.fontM; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Automatic recovery"); color: KP.textMuted; font.pixelSize: KP.fontS; Layout.preferredWidth: 128 }
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
                    Item { Layout.fillHeight: true }
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
