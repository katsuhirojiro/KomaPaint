import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Painting Brushes & Sub Tools panel.
//
// For Painting Brushes ("brush"), presets are organized into two genuinely
// separate engine collections:
//   1. Primary Engine Tabs: Digital (default) and Realistic (locked until enabled
//      in Settings → Brush Engine).
//   2. Persistent Secondary Collection Tabs within each engine (create, rename,
//      reorder, set default opening tab, delete with deliberate confirmation).
//   3. Independent Brush Presets (create from current, duplicate, rename,
//      move/copy across tabs, reorder, reset, delete) with live stroke previews.
Rectangle {
    id: panel
    color: KP.paneBg
    clip: true

    readonly property var library: Workspace.subTools
    readonly property var brush: Workspace.brush
    readonly property bool realisticEngine: Workspace.realisticEngine
    readonly property bool realisticEnabled: Workspace.realisticEngineEnabled
    readonly property bool isBrushTool: Boolean(library && library.brushTool)
    property string toolLabel: Workspace.activeTool
    readonly property bool painting: Boolean(library && library.paintTool)
    readonly property bool hasCollections: Boolean(library && library.hasCollections)
    readonly property int rowHeight: 40

    property bool showLockedBanner: false
    property string tabDialogMode: "create" // "create" | "rename"
    property string targetTabId: ""
    property string targetTabName: ""
    property int targetTabBrushCount: 0
    property string brushDialogMode: "create" // "create" | "rename"
    property string targetBrushId: ""
    property string targetBrushExecTool: ""
    property int targetBrushIndex: -1

    function presetSize(preset) { return Math.round(preset.size) }

    function isStrokeRow(preset) {
        const exec = preset.executableTool || Workspace.activeTool
        return exec === "pen" || exec === "pencil" || exec === "brush"
            || exec === "airbrush" || exec === "eraser" || exec === "boundary_eraser"
            || exec === "smudge"
    }

    function presetBadgeText(preset) {
        const exec = preset.executableTool || Workspace.activeTool
        if (isStrokeRow(preset)) {
            if (exec === "boundary_eraser") return qsTr("Boundary")
            return panel.presetSize(preset) + " px"
        }
        const params = preset.toolParams || {}
        if (exec === "smart_fill") return qsTr("Smart")
        if (exec === "fill") return qsTr("Bucket")
        if (exec === "wand") return params.contiguous === false ? qsTr("Global") : qsTr("Region")
        if (exec === "lasso") return params.lassoMode || qsTr("Free")
        return ""
    }

    function tipLabel(preset) {
        const exec = preset.executableTool || Workspace.activeTool
        if (exec === "boundary_eraser") return qsTr("boundary-aware")
        if (exec === "smart_fill") {
            const p = preset.toolParams || {}
            return qsTr("gap %1px").arg(p.gapClosing !== undefined ? p.gapClosing : 10)
        }
        if (exec === "fill") {
            const p = preset.toolParams || {}
            return qsTr("tol %1").arg(p.tolerance !== undefined ? p.tolerance : 28)
        }
        if (exec === "wand") {
            const p = preset.toolParams || {}
            return qsTr("tol %1").arg(p.tolerance !== undefined ? p.tolerance : 26)
        }
        if (exec === "lasso") {
            const p = preset.toolParams || {}
            return p.lassoMode || qsTr("freehand")
        }
        if (!isStrokeRow(preset)) return preset.category || exec
        if (panel.isBrushTool && panel.realisticEngine)
            return preset.mediumId === "watercolour" ? qsTr("Watercolour") : qsTr("Gouache")
        if (preset.blendStrength > 0) return qsTr("blend")
        if (preset.tipStyle === "bristle") return qsTr("bristle")
        if (preset.tipStyle === "graphite") return qsTr("graphite")
        if (preset.tipStyle === "stipple") return qsTr("stipple")
        if (preset.aspectRatio > 115) return qsTr("flat")
        return preset.textured ? qsTr("textured") : qsTr("round")
    }

    function openTabContextMenu(anchorItem, tabId, tabName, brushCount) {
        targetTabId = tabId
        targetTabName = tabName
        targetTabBrushCount = brushCount
        if (anchorItem) {
            const pt = anchorItem.mapToItem(panel, 0, anchorItem.height)
            const menuW = Math.max(160, tabContextMenu.implicitWidth)
            const px = Math.max(4, Math.min(panel.width - menuW - 4,
                                            pt.x - menuW + anchorItem.width))
            tabContextMenu.popup(panel, px, pt.y)
        } else {
            tabContextMenu.popup()
        }
    }

    function openCreateTabDialog() {
        tabDialogMode = "create"
        targetTabId = ""
        tabNameInput.text = ""
        tabNameDialog.open()
    }

    function openRenameTabDialog(tabId, tabName) {
        tabDialogMode = "rename"
        targetTabId = tabId
        tabNameInput.text = tabName
        tabNameDialog.open()
    }

    function openDeleteTabConfirm(tabId, tabName, brushCount) {
        targetTabId = tabId
        targetTabName = tabName
        targetTabBrushCount = brushCount
        deleteTabConfirmDialog.open()
    }

    function openCreateBrushDialog() {
        brushDialogMode = "create"
        targetBrushId = ""
        brushNameInput.text = library.currentName ? (library.currentName + qsTr(" Copy")) : qsTr("Custom Brush")
        brushNameDialog.open()
    }

    function openRenameBrushDialog(brushId, brushName) {
        brushDialogMode = "rename"
        targetBrushId = brushId
        brushNameInput.text = brushName
        brushNameDialog.open()
    }

    onRealisticEnabledChanged: {
        if (realisticEnabled) showLockedBanner = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Active Tool Header
        Item {
            Layout.fillWidth: true
            implicitHeight: 32
            visible: panel.hasCollections

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.panelInset
                anchors.rightMargin: KP.gapXS
                spacing: KP.gapS

                KpIcon {
                    name: "tools/" + Workspace.activeTool
                    size: 16
                    color: KP.iconNormal
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: panel.toolLabel
                        color: KP.text
                        font.pixelSize: KP.fontS + 1
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: panel.library.currentName || qsTr("No sub-tool selected")
                        color: KP.textSubtle
                        font.pixelSize: KP.fontMicro
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    objectName: "createBrushButton"
                    visible: panel.hasCollections
                    glyph: "plus"
                    iconSize: 12
                    cell: KP.controlHeightSmall
                    tooltip: qsTr("Save current settings as a new sub-tool")
                    onActivated: panel.openCreateBrushDialog()
                }
                IconButton {
                    objectName: "resetSubToolButton"
                    glyph: "reset"
                    iconSize: 12
                    cell: KP.controlHeightSmall
                    tooltip: qsTr("Return this preset to its default settings")
                    enabled: panel.library.modified && panel.library.currentBrushBuiltIn
                    onActivated: panel.library.resetCurrent()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }
        }

        // ---- Level 1 Primary Engine Tabs (Digital | Realistic) for Painting Brushes
        Rectangle {
            objectName: "primaryEngineBar"
            Layout.fillWidth: true
            implicitHeight: visible ? 28 : 0
            visible: panel.painting && panel.isBrushTool
            color: KP.paneBgAlt

            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 2

                AbstractButton {
                    id: digitalTab
                    objectName: "primaryEngineTab_digital"
                    readonly property bool active: !panel.realisticEngine
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Digital brushes")
                    onClicked: {
                        panel.showLockedBanner = false
                        Workspace.brushEngine = "digital"
                    }

                    background: Rectangle {
                        radius: KP.radiusXS
                        color: digitalTab.active ? KP.paneBg
                             : digitalTab.hovered ? KP.hoverOverlay : "transparent"
                        border.width: digitalTab.active ? 1 : 0
                        border.color: KP.border
                        Rectangle {
                            visible: digitalTab.active
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 2
                            color: KP.ember
                        }
                    }
                    contentItem: Text {
                        text: qsTr("Digital")
                        color: digitalTab.active ? KP.textAccent : KP.textMuted
                        font.pixelSize: KP.fontXS
                        font.weight: digitalTab.active ? Font.DemiBold : Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AbstractButton {
                    id: realisticTab
                    objectName: "primaryEngineTab_realistic"
                    readonly property bool active: panel.realisticEngine
                    readonly property bool locked: !panel.realisticEnabled
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Realistic brushes")
                    onClicked: {
                        if (realisticTab.locked) {
                            panel.showLockedBanner = true
                        } else {
                            panel.showLockedBanner = false
                            Workspace.brushEngine = "realistic"
                        }
                    }

                    background: Rectangle {
                        radius: KP.radiusXS
                        color: realisticTab.active ? KP.paneBg
                             : realisticTab.hovered ? KP.hoverOverlay : "transparent"
                        border.width: realisticTab.active ? 1 : 0
                        border.color: KP.border
                        Rectangle {
                            visible: realisticTab.active
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 2
                            color: KP.ember
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 4
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: realisticTab.locked
                            text: "\uD83D\uDD12"
                            color: KP.textSubtle
                            font.pixelSize: 10
                        }
                        Text {
                            text: qsTr("Realistic")
                            color: realisticTab.active ? KP.textAccent
                                 : realisticTab.locked ? KP.textSubtle : KP.textMuted
                            font.pixelSize: KP.fontXS
                            font.weight: realisticTab.active ? Font.DemiBold : Font.Medium
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }
        }

        // ---- Locked Realistic Prompt (shown when user clicks locked Realistic tab)
        Rectangle {
            objectName: "realisticLockedNotice"
            Layout.fillWidth: true
            Layout.leftMargin: KP.gapS
            Layout.rightMargin: KP.gapS
            Layout.topMargin: KP.gapXS
            visible: panel.painting && panel.isBrushTool && !panel.realisticEnabled && panel.showLockedBanner
            implicitHeight: visible ? 28 : 0
            radius: KP.radiusS
            color: KP.rowSelected
            border.width: 1
            border.color: KP.ember

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapXS
                spacing: KP.gapXS
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Enable Realistic in Settings")
                    color: KP.text
                    font.pixelSize: KP.fontMicro
                    elide: Text.ElideRight
                }
                KpButton {
                    objectName: "realisticUnlockSettingsButton"
                    small: true
                    label: qsTr("Settings…")
                    onClicked: Workspace.openBrushEngineSettings()
                }
                IconButton {
                    objectName: "realisticLockedDismissButton"
                    glyph: "close"
                    iconSize: 10
                    cell: 20
                    tooltip: qsTr("Dismiss")
                    onActivated: panel.showLockedBanner = false
                }
            }
        }

        // ---- Level 2 Persistent Secondary User Collection Tabs (universal for every tool family)
        Rectangle {
            objectName: "collectionTabBar"
            Layout.fillWidth: true
            Layout.maximumWidth: panel.width
            implicitHeight: visible ? 26 : 0
            visible: panel.hasCollections
            clip: true
            color: KP.paneBg

            IconButton {
                id: createTabBtn
                objectName: "createTabButton"
                anchors.right: parent.right
                anchors.rightMargin: KP.gapXS
                anchors.verticalCenter: parent.verticalCenter
                glyph: "plus"
                iconSize: 11
                cell: 22
                tooltip: qsTr("Create new collection tab")
                onActivated: panel.openCreateTabDialog()
            }

            IconButton {
                id: tabOptionsBtn
                objectName: "tabOptionsButton"
                anchors.right: createTabBtn.left
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                glyph: "dots-h"
                iconSize: 12
                cell: 22
                tooltip: qsTr("Manage current tab")
                onActivated: {
                    const tabs = panel.library.engineTabs
                    const idx = panel.library.currentTabIndex
                    if (idx >= 0 && idx < tabs.length) {
                        panel.openTabContextMenu(tabOptionsBtn, tabs[idx].id, tabs[idx].name, tabs[idx].brushCount)
                    }
                }
            }

            ListView {
                id: tabStrip
                objectName: "collectionTabStrip"
                anchors.left: parent.left
                anchors.leftMargin: KP.gapXS
                anchors.right: tabOptionsBtn.left
                anchors.rightMargin: 2
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: ListView.Horizontal
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                model: panel.library.engineTabs
                readonly property int activeTabIdx: panel.library.currentTabIndex
                onActiveTabIdxChanged: {
                    if (activeTabIdx >= 0 && activeTabIdx < count)
                        positionViewAtIndex(activeTabIdx, ListView.Contain)
                }

                delegate: AbstractButton {
                    id: colTab
                    required property int index
                    required property var modelData
                    readonly property bool selected: index === panel.library.currentTabIndex
                    objectName: "collectionTab_" + index
                    height: tabStrip.height
                    leftPadding: 6
                    rightPadding: 6
                    topPadding: 0
                    bottomPadding: 0
                    implicitWidth: Math.max(36, tabRow.implicitWidth + 12)
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: modelData.name

                    onClicked: panel.library.selectTab(index)

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: panel.openTabContextMenu(colTab, colTab.modelData.id, colTab.modelData.name, colTab.modelData.brushCount)
                    }

                    background: Rectangle {
                        color: colTab.selected ? KP.rowSelected
                             : colTab.hovered ? KP.hoverOverlay : "transparent"
                        radius: KP.radiusXS
                        Rectangle {
                            visible: colTab.selected
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            color: KP.accent
                        }
                    }

                    contentItem: RowLayout {
                        id: tabRow
                        spacing: 3
                        Rectangle {
                            visible: Boolean(colTab.modelData.isDefault)
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 4
                            Layout.preferredHeight: 4
                            radius: 2
                            color: KP.ember
                        }
                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            text: colTab.modelData.name
                            color: colTab.selected ? KP.textAccent : KP.textMuted
                            font.pixelSize: KP.fontMicro
                            font.weight: colTab.selected ? Font.DemiBold : Font.Normal
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }
        }

        // ---- Undo deleted tab banner
        Rectangle {
            objectName: "undoTabDeleteBanner"
            Layout.fillWidth: true
            Layout.leftMargin: KP.gapS
            Layout.rightMargin: KP.gapS
            Layout.topMargin: KP.gapXS
            visible: panel.hasCollections && panel.library.canUndoTabDelete
            implicitHeight: visible ? 24 : 0
            radius: KP.radiusXS
            color: KP.paneBgAlt
            border.width: 1
            border.color: KP.hairline
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapXS
                spacing: KP.gapXS
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Collection tab deleted")
                    color: KP.textMuted
                    font.pixelSize: KP.fontMicro
                    elide: Text.ElideRight
                }
                KpButton {
                    objectName: "undoTabDeleteButton"
                    small: true
                    label: qsTr("Undo")
                    onClicked: panel.library.undoDeleteTab()
                }
            }
        }

        // ---- Search & Category filter
        RowLayout {
            objectName: "subToolFilters"
            Layout.fillWidth: true
            Layout.maximumWidth: panel.width
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.gapS
            Layout.preferredHeight: 28
            spacing: KP.gapS
            visible: panel.hasCollections

            KpTextField {
                id: searchField
                objectName: "subToolSearch"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: 22
                placeholder: qsTr("Search sub-tools in tab")
                text: panel.library.searchText
                onEdited: function(value) { panel.library.searchText = value }
            }
            ComboBox {
                id: categoryChoice
                objectName: "subToolCategory"
                visible: false
                Layout.preferredWidth: 108
                Layout.preferredHeight: 22
                model: [qsTr("All categories")].concat(panel.library.categories)
                currentIndex: panel.library.categoryFilter.length === 0
                              ? 0 : Math.max(0, model.indexOf(panel.library.categoryFilter))
                font.pixelSize: KP.fontXS
                Accessible.name: qsTr("Filter subtools by category")
                onActivated: function(index) {
                    panel.library.categoryFilter = index === 0 ? "" : panel.library.categories[index - 1]
                }
                contentItem: Text {
                    leftPadding: KP.gapS
                    rightPadding: 16
                    text: categoryChoice.displayText
                    color: KP.text
                    font.pixelSize: KP.fontXS
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    color: KP.inputBg
                    border.width: 1
                    border.color: categoryChoice.activeFocus ? KP.focusRing : KP.border
                    radius: KP.radiusS
                }
                indicator: KpGlyph {
                    x: categoryChoice.width - width - 4
                    y: (categoryChoice.height - height) / 2
                    name: "chevron-down"
                    size: 10
                    color: KP.iconNormal
                }
            }
        }

        // ---- Error notice if a tab/library action fails
        Rectangle {
            objectName: "libraryErrorBanner"
            Layout.fillWidth: true
            Layout.leftMargin: KP.gapS
            Layout.rightMargin: KP.gapS
            visible: panel.hasCollections && panel.library.lastError.length > 0
            implicitHeight: visible ? 22 : 0
            color: KP.rowSelected
            radius: KP.radiusXS
            Text {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapS
                text: panel.library.lastError
                color: KP.ember
                font.pixelSize: KP.fontMicro
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        // ---- Sub-Tool Preset List
        ListView {
            id: list
            objectName: "subToolList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            visible: panel.hasCollections
            model: panel.library.subTools
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

            delegate: AbstractButton {
                id: row
                required property int index
                required property var modelData
                readonly property int presetIndex: modelData.index
                readonly property bool current: presetIndex === panel.library.currentIndex

                objectName: "subTool" + presetIndex
                width: list.width
                height: panel.rowHeight
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: modelData.name
                onClicked: panel.library.select(presetIndex)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        panel.targetBrushId = row.modelData.id
                        panel.targetBrushExecTool = row.modelData.executableTool || Workspace.activeTool
                        panel.targetBrushIndex = row.presetIndex
                        brushContextMenu.popup()
                    }
                }

                background: Rectangle {
                    color: row.current ? KP.rowSelected
                         : row.hovered ? KP.hoverOverlay : "transparent"
                    Rectangle {
                        visible: row.current
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 2
                        color: KP.ember
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: KP.hairline
                        opacity: 0.6
                    }
                }

                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: KP.panelInset
                    anchors.rightMargin: KP.gapS
                    spacing: KP.gapS

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: panel.isStrokeRow(row.modelData) ? 54 : 32
                        Layout.preferredHeight: 24
                        radius: KP.radiusXS
                        color: KP.isDark ? Qt.rgba(0, 0, 0, 0.22) : Qt.rgba(255, 255, 255, 0.65)
                        border.width: 1
                        border.color: KP.hairline

                        BrushStrokePreview {
                            visible: panel.isStrokeRow(row.modelData)
                            anchors.fill: parent
                            anchors.margins: 2
                            brushSize: row.modelData.size
                            hardness: row.modelData.hardness
                            brushOpacity: row.modelData.opacity
                            flow: row.modelData.flow
                            spacing: row.modelData.spacing
                            textured: row.modelData.textured
                            tipStyle: row.modelData.tipStyle || (row.modelData.textured ? "textured" : "round")
                            aspectRatio: row.modelData.aspectRatio || 100
                            tipAngle: row.modelData.tipAngle || 0
                            pressureSize: row.modelData.pressureSize || 0
                            blendStrength: row.modelData.blendStrength || 0
                            scaleHint: 0.60
                            ink: KP.text
                        }
                        KpIcon {
                            visible: !panel.isStrokeRow(row.modelData)
                            anchors.centerIn: parent
                            name: "tools/" + ((row.modelData.executableTool === "smart_fill") ? "fill" : (row.modelData.executableTool || Workspace.activeTool))
                            size: 16
                            color: row.current ? KP.textAccent : KP.iconNormal
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: KP.gapXS
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                color: row.current ? KP.textAccent : KP.text
                                font.pixelSize: KP.fontS
                                font.weight: row.current ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignVCenter
                                visible: row.modelData.modified
                                Layout.preferredWidth: 5
                                Layout.preferredHeight: 5
                                radius: 2.5
                                color: KP.ember
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: KP.gapS
                            Text {
                                visible: panel.isStrokeRow(row.modelData)
                                text: row.modelData.hardness + "%"
                                color: KP.textSubtle
                                font.pixelSize: KP.fontMicro
                                font.family: KP.fontMono
                            }
                            Text {
                                text: panel.tipLabel(row.modelData)
                                color: (panel.isBrushTool && panel.realisticEngine) ? KP.textAccent : KP.textSubtle
                                font.pixelSize: KP.fontMicro
                            }
                        }
                    }

                    Text {
                        id: rowSize
                        Layout.alignment: Qt.AlignVCenter
                        text: panel.presetBadgeText(row.modelData)
                        color: row.current ? KP.textMuted : KP.textSubtle
                        font.pixelSize: KP.fontXS
                        font.family: KP.fontMono
                    }

                    IconButton {
                        objectName: "brushMenuButton_" + row.presetIndex
                        visible: panel.hasCollections && (row.hovered || row.current)
                        glyph: "dots-h"
                        iconSize: 12
                        cell: 20
                        tooltip: qsTr("Sub-tool actions")
                        onActivated: {
                            panel.targetBrushId = row.modelData.id
                            panel.targetBrushExecTool = row.modelData.executableTool || Workspace.activeTool
                            panel.targetBrushIndex = row.presetIndex
                            brushContextMenu.popup()
                        }
                    }
                }
            }
        }

        // ---- Empty state
        ColumnLayout {
            objectName: "subToolNoResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.hasCollections && list.count === 0
            spacing: KP.gapS
            Item { Layout.fillHeight: true }
            Text {
                Layout.fillWidth: true
                text: panel.library.searchText.length > 0
                      ? qsTr("No sub-tools match this search.")
                      : qsTr("This collection tab is empty.")
                color: KP.textSubtle
                font.pixelSize: KP.fontS
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            KpButton {
                objectName: "emptyTabCreateBrushButton"
                Layout.alignment: Qt.AlignHCenter
                visible: panel.hasCollections && panel.library.searchText.length === 0
                small: true
                label: qsTr("Create Sub-Tool from Current")
                onClicked: panel.openCreateBrushDialog()
            }
            Item { Layout.fillHeight: true }
        }

        // ---- Not a sub-tool collection tool (e.g. Zoom, Hand, Eyedropper)
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.hasCollections
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 2 * KP.panelInset
                spacing: KP.gapS
                KpGlyph {
                    Layout.alignment: Qt.AlignHCenter
                    name: "brush"
                    size: 20
                    color: KP.iconOff
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("This tool has no presets.")
                    color: KP.textMuted
                    font.pixelSize: KP.fontS
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Pick a painting tool to see its presets.")
                    color: KP.textSubtle
                    font.pixelSize: KP.fontXS
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ---- Context Menu: Tab Management
    KpMenu {
        id: tabContextMenu
        objectName: "collectionTabMenu"

        KpMenuItem {
            objectName: "tabMenuRename"
            text: qsTr("Rename Tab…")
            onTriggered: panel.openRenameTabDialog(panel.targetTabId, panel.targetTabName)
        }
        KpMenuItem {
            objectName: "tabMenuMoveLeft"
            text: qsTr("Move Left")
            enabled: {
                const tabs = panel.library.engineTabs
                for (let i = 0; i < tabs.length; ++i)
                    if (tabs[i].id === panel.targetTabId) return i > 0
                return false
            }
            onTriggered: panel.library.moveTabById(panel.targetTabId, -1)
        }
        KpMenuItem {
            objectName: "tabMenuMoveRight"
            text: qsTr("Move Right")
            enabled: {
                const tabs = panel.library.engineTabs
                for (let i = 0; i < tabs.length; ++i)
                    if (tabs[i].id === panel.targetTabId) return i + 1 < tabs.length
                return false
            }
            onTriggered: panel.library.moveTabById(panel.targetTabId, 1)
        }
        KpMenuItem {
            objectName: "tabMenuSetDefault"
            text: qsTr("Set as Default Tab")
            enabled: panel.targetTabId !== panel.library.defaultTabId
            onTriggered: panel.library.setDefaultTab(panel.targetTabId)
        }
        KpMenuSeparator {}
        KpMenuItem {
            objectName: "tabMenuDelete"
            text: qsTr("Delete Tab…")
            enabled: panel.library.engineTabs.length > 1
            onTriggered: panel.openDeleteTabConfirm(panel.targetTabId, panel.targetTabName, panel.targetTabBrushCount)
        }
    }

    // ---- Context Menu: Brush Management
    KpMenu {
        id: brushContextMenu
        objectName: "brushContextMenu"

        KpMenuItem {
            objectName: "brushMenuDuplicate"
            text: qsTr("Duplicate")
            onTriggered: panel.library.duplicateBrushById(panel.targetBrushId)
        }
        KpMenuItem {
            objectName: "brushMenuRename"
            text: qsTr("Rename…")
            onTriggered: {
                const listItems = panel.library.subTools
                for (let i = 0; i < listItems.length; ++i) {
                    if (listItems[i].id === panel.targetBrushId) {
                        panel.openRenameBrushDialog(panel.targetBrushId, listItems[i].name)
                        return
                    }
                }
            }
        }
        KpMenu {
            id: moveToTabSubmenu
            title: qsTr("Move to Tab")
            enabled: panel.library.engineTabs.length > 1
            Instantiator {
                model: panel.library.engineTabs
                delegate: KpMenuItem {
                    required property var modelData
                    text: modelData.name
                    enabled: modelData.id !== panel.library.currentTabId
                    onTriggered: panel.library.moveBrushToTab(panel.targetBrushId, modelData.id)
                }
                onObjectAdded: function(index, object) { moveToTabSubmenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { moveToTabSubmenu.removeItem(object) }
            }
        }
        KpMenu {
            id: copyToTabSubmenu
            title: qsTr("Copy to Tab")
            enabled: panel.library.engineTabs.length > 1
            Instantiator {
                model: panel.library.engineTabs
                delegate: KpMenuItem {
                    required property var modelData
                    text: modelData.name
                    enabled: modelData.id !== panel.library.currentTabId
                    onTriggered: panel.library.copyBrushToTab(panel.targetBrushId, modelData.id)
                }
                onObjectAdded: function(index, object) { copyToTabSubmenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { copyToTabSubmenu.removeItem(object) }
            }
        }
        KpMenu {
            id: moveToToolSubmenu
            title: qsTr("Move to Compatible Tool")
            enabled: panel.library.compatibleToolsFor(panel.targetBrushExecTool || Workspace.activeTool).length > 1
            Instantiator {
                model: panel.library.compatibleToolsFor(panel.targetBrushExecTool || Workspace.activeTool)
                delegate: KpMenuItem {
                    required property var modelData
                    text: modelData
                    enabled: modelData !== Workspace.activeTool
                    onTriggered: {
                        const tabs = panel.library.tabsForTool(modelData)
                        if (tabs && tabs.length > 0)
                            panel.library.moveSubToolTo(panel.targetBrushId, modelData, tabs[0].id)
                    }
                }
                onObjectAdded: function(index, object) { moveToToolSubmenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { moveToToolSubmenu.removeItem(object) }
            }
        }
        KpMenuItem {
            objectName: "brushMenuMoveUp"
            text: qsTr("Move Up")
            enabled: panel.targetBrushIndex > 0
            onTriggered: panel.library.reorderBrush(panel.targetBrushIndex, panel.targetBrushIndex - 1)
        }
        KpMenuItem {
            objectName: "brushMenuMoveDown"
            text: qsTr("Move Down")
            enabled: panel.targetBrushIndex >= 0 && panel.targetBrushIndex + 1 < panel.library.subTools.length
            onTriggered: panel.library.reorderBrush(panel.targetBrushIndex, panel.targetBrushIndex + 1)
        }
        KpMenuItem {
            objectName: "brushMenuReset"
            text: qsTr("Reset to Defaults")
            onTriggered: panel.library.resetBrush(panel.targetBrushId)
        }
        KpMenuSeparator {}
        KpMenuItem {
            objectName: "brushMenuDelete"
            text: qsTr("Delete")
            onTriggered: panel.library.deleteBrush(panel.targetBrushId)
        }
    }

    // ---- Modal: Create / Rename Tab
    KpDialog {
        id: tabNameDialog
        objectName: "tabNameDialog"
        bodyWidth: 320
        heading: panel.tabDialogMode === "rename" ? qsTr("Rename Collection Tab") : qsTr("New Collection Tab")
        note: panel.isBrushTool
              ? (panel.realisticEngine ? qsTr("Saved inside your Realistic brushes collection.")
                                       : qsTr("Saved inside your Digital brushes collection."))
              : qsTr("Saved inside your %1 sub-tool collection.").arg(panel.toolLabel)
        body: [
            KpTextField {
                id: tabNameInput
                objectName: "tabNameInput"
                Layout.fillWidth: true
                placeholder: qsTr("Tab name (e.g. Portraits, Foliage)")
                onSubmitted: function() {
                    if (tabNameInput.text.trim().length > 0) tabNameConfirmButton.clicked()
                }
            }
        ]
        actions: [
            KpButton {
                label: qsTr("Cancel")
                onClicked: tabNameDialog.reject()
            },
            KpButton {
                id: tabNameConfirmButton
                objectName: "tabNameConfirmButton"
                label: panel.tabDialogMode === "rename" ? qsTr("Rename") : qsTr("Create")
                variant: "primary"
                enabled: tabNameInput.text.trim().length > 0
                onClicked: {
                    if (panel.tabDialogMode === "rename")
                        panel.library.renameTab(panel.targetTabId, tabNameInput.text)
                    else
                        panel.library.createTab(tabNameInput.text)
                    tabNameDialog.accept()
                }
            }
        ]
    }

    // ---- Modal: Deliberate Tab Deletion Confirmation
    KpDialog {
        id: deleteTabConfirmDialog
        objectName: "deleteTabConfirmDialog"
        bodyWidth: 340
        heading: qsTr("Delete collection tab?")
        note: panel.targetTabBrushCount > 0
              ? qsTr("Deleting “%1” will permanently remove this tab and its %2 brush preset(s).").arg(panel.targetTabName).arg(panel.targetTabBrushCount)
              : qsTr("Are you sure you want to delete the empty tab “%1”?").arg(panel.targetTabName)
        body: []
        actions: [
            KpButton {
                objectName: "deleteTabCancelButton"
                label: qsTr("Cancel")
                onClicked: deleteTabConfirmDialog.reject()
            },
            KpButton {
                objectName: "deleteTabConfirmButton"
                label: qsTr("Delete Tab")
                variant: "primary"
                onClicked: {
                    panel.library.deleteTab(panel.targetTabId)
                    deleteTabConfirmDialog.accept()
                }
            }
        ]
    }

    // ---- Modal: Create / Rename Brush
    KpDialog {
        id: brushNameDialog
        objectName: "brushNameDialog"
        bodyWidth: 320
        heading: panel.brushDialogMode === "rename" ? qsTr("Rename Brush") : qsTr("Save New Brush")
        note: qsTr("Stores all current size, tip, dynamics, and material settings independently.")
        body: [
            KpTextField {
                id: brushNameInput
                objectName: "brushNameInput"
                Layout.fillWidth: true
                placeholder: qsTr("Brush name")
                onSubmitted: function() {
                    if (brushNameInput.text.trim().length > 0) brushNameConfirmButton.clicked()
                }
            }
        ]
        actions: [
            KpButton {
                label: qsTr("Cancel")
                onClicked: brushNameDialog.reject()
            },
            KpButton {
                id: brushNameConfirmButton
                objectName: "brushNameConfirmButton"
                label: panel.brushDialogMode === "rename" ? qsTr("Rename") : qsTr("Save Brush")
                variant: "primary"
                enabled: brushNameInput.text.trim().length > 0
                onClicked: {
                    if (panel.brushDialogMode === "rename")
                        panel.library.renameBrush(panel.targetBrushId, brushNameInput.text)
                    else
                        panel.library.createBrushFromCurrent(brushNameInput.text)
                    brushNameDialog.accept()
                }
            }
        ]
    }
}
