import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// The command bar: the row of things you reach for all day.
//
// It is not a shelf of every command. It holds four groups with a visible
// rhythm — document (new / open / save), edit (undo / redo), view (zoom out,
// zoom readout, zoom in, fit, actual size) and brush (size, opacity) — and the
// groups can be turned off from its own context menu, remembered across
// restarts. Everything else lives in the menus and panels, which is why the bar
// stays calm.
//
// The brush cluster is here on purpose: size and opacity change more often than
// anything else while painting, so they stay one click away and show a live tip
// preview instead of only a number.
Rectangle {
    id: bar

    signal newDocumentRequested
    signal openRequested
    signal saveRequested
    signal preferencesRequested

    property bool showDocumentGroup: true
    property bool showEditGroup: true
    property bool showViewGroup: true
    property bool showBrushGroup: true
    // Rebuilt whenever the dock layout changes, so the panel ticks are true.
    // Panel ids the shell can actually render; the docking model also knows
    // panels that have no content component yet, and offering those in the menu
    // would open an empty box.
    property var availablePanelIds: []
    property var panelItems: []
    readonly property var offeredPanels: bar.panelItems.filter(function (entry) {
        return bar.availablePanelIds.length === 0 || bar.availablePanelIds.indexOf(entry.id) >= 0
    })

    readonly property string settingsKey: "ui.commandBar.hiddenGroups"
    readonly property real zoomPercent: Workspace.zoom
    // The brush cluster folds away before it can squeeze the view group.
    readonly property bool roomForBrush: width > 860

    implicitHeight: KP.commandBarHeight
    color: KP.toolbarBg

    function loadGroups() {
        const hidden = String(Workspace.sessionValue(settingsKey, ""))
        showDocumentGroup = hidden.indexOf("document") < 0
        showEditGroup = hidden.indexOf("edit") < 0
        showViewGroup = hidden.indexOf("view") < 0
        showBrushGroup = hidden.indexOf("brush") < 0
    }
    function storeGroups() {
        const hidden = []
        if (!showDocumentGroup) hidden.push("document")
        if (!showEditGroup) hidden.push("edit")
        if (!showViewGroup) hidden.push("view")
        if (!showBrushGroup) hidden.push("brush")
        Workspace.setSessionValue(settingsKey, hidden.join(","))
    }

    Component.onCompleted: { loadGroups(); panelItems = Dock.panels() }
    Connections {
        target: Dock
        function onLayoutChanged() { bar.panelItems = Dock.panels() }
    }

    // A divider between command groups: a hairline that stops short of the
    // bar's height so the row reads as one continuous surface.
    component BarRule: Rectangle {
        implicitWidth: 1
        implicitHeight: 18
        color: KP.hairline
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: KP.hairline
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: KP.gapM
        anchors.rightMargin: KP.gapM
        spacing: KP.gapS

        // ---- Document
        Row {
            visible: bar.showDocumentGroup
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                objectName: "newProjectToolbarButton"
                glyph: "new"
                label: qsTr("New")
                tip: qsTr("New project (Ctrl+N)")
                onClicked: bar.newDocumentRequested()
            }
            KpButton {
                objectName: "openToolbarButton"
                glyph: "open"
                label: qsTr("Open")
                tip: qsTr("Open an image (Ctrl+O)")
                onClicked: bar.openRequested()
            }
            KpButton {
                objectName: "saveToolbarButton"
                glyph: "save"
                label: qsTr("Save")
                tip: qsTr("Save as .koma (Ctrl+S)")
                onClicked: bar.saveRequested()
            }
        }

        BarRule { visible: bar.showDocumentGroup && bar.showEditGroup; Layout.alignment: Qt.AlignVCenter }

        // ---- Edit
        Row {
            visible: bar.showEditGroup
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                glyph: "undo"
                tip: Workspace.history.canUndo
                     ? qsTr("Undo %1 (Ctrl+Z)").arg(Workspace.history.undoText)
                     : qsTr("Nothing to undo")
                enabled: Workspace.history.canUndo
                onClicked: Workspace.history.undo()
            }
            KpButton {
                glyph: "redo"
                tip: Workspace.history.canRedo
                     ? qsTr("Redo %1 (Ctrl+Shift+Z)").arg(Workspace.history.redoText)
                     : qsTr("Nothing to redo")
                enabled: Workspace.history.canRedo
                onClicked: Workspace.history.redo()
            }
        }

        BarRule { visible: bar.showEditGroup && bar.showViewGroup; Layout.alignment: Qt.AlignVCenter }

        // ---- View
        Row {
            visible: bar.showViewGroup
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                glyph: "zoom-out"
                tip: qsTr("Zoom out")
                onClicked: Workspace.zoomOut()
            }
            Item {
                width: 60
                height: KP.controlHeight

                Rectangle {
                    anchors.fill: parent
                    radius: KP.radiusS
                    color: zoomMouse.containsMouse ? KP.hoverOverlay : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(bar.zoomPercent) + "%"
                        color: KP.text
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontS
                    }
                }
                MouseArea {
                    id: zoomMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: zoomMenu.popup()
                }
                KpMenu {
                    id: zoomMenu
                    y: parent.height + 2
                    KpMenuItem { text: qsTr("Fit to window\tCtrl+0"); onTriggered: Workspace.zoomFit() }
                    KpMenuItem { text: qsTr("Actual size\tCtrl+1"); onTriggered: Workspace.resetZoom() }
                    KpMenuSeparator { }
                    KpMenuItem { text: qsTr("200%"); onTriggered: Workspace.zoom = 200 }
                    KpMenuItem { text: qsTr("400%"); onTriggered: Workspace.zoom = 400 }
                    KpMenuItem { text: qsTr("800%"); onTriggered: Workspace.zoom = 800 }
                }
            }
            KpButton {
                glyph: "zoom-in"
                tip: qsTr("Zoom in")
                onClicked: Workspace.zoomIn()
            }
            KpButton {
                glyph: "zoom-fit"
                tip: qsTr("Fit to window (Ctrl+0)")
                onClicked: Workspace.zoomFit()
            }
            KpButton {
                glyph: "actual"
                tip: qsTr("Actual size (Ctrl+1)")
                onClicked: Workspace.resetZoom()
            }
        }

        BarRule {
            visible: bar.showViewGroup && bar.showBrushGroup && bar.roomForBrush
            Layout.alignment: Qt.AlignVCenter
        }

        // ---- Brush
        Row {
            visible: bar.showBrushGroup && bar.roomForBrush
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapS

            Item {
                width: 30
                height: KP.controlHeight

                Rectangle {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    radius: 13
                    color: KP.inputBg
                    border.width: 1
                    border.color: tipMouse.containsMouse ? KP.borderLight : KP.border

                    Rectangle {
                        anchors.centerIn: parent
                        readonly property real shown: Math.max(2, Math.min(20, 2 + Math.pow(Workspace.brush.size / 256, 0.55) * 18))
                        width: shown
                        height: shown
                        radius: width / 2
                        color: Workspace.colors.foreground
                        opacity: Workspace.brush.opacity / 100
                    }
                }
                MouseArea {
                    id: tipMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.text: qsTr("Brush tip — click to open Brush Size")
                    ToolTip.visible: tipMouse.containsMouse
                    ToolTip.delay: 500
                    onClicked: Dock.showPanel("brushsize")
                }
            }

            Column {
                spacing: 1
                Text {
                    text: qsTr("SIZE")
                    color: KP.textSectionLabel
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                KpNumberField {
                    objectName: "commandBarSizeField"
                    width: 52
                    height: KP.controlHeightSmall
                    value: Workspace.brush.size
                    from: Workspace.brush.minimumSize
                    to: Workspace.brush.maximumSize
                    onEdited: function(v) { Workspace.brush.size = v }
                }
            }
            Column {
                spacing: 1
                Text {
                    text: qsTr("OPACITY")
                    color: KP.textSectionLabel
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                KpNumberField {
                    objectName: "commandBarOpacityField"
                    width: 46
                    height: KP.controlHeightSmall
                    value: Workspace.brush.opacity
                    from: 0
                    to: 100
                    unit: "%"
                    onEdited: function(v) { Workspace.brush.opacity = v }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // ---- Workspace commands
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapXS

            KpButton {
                glyph: "grid"
                tip: qsTr("Panels")
                onClicked: panelsMenu.popup()
            }
            KpButton {
                objectName: "preferencesToolbarButton"
                glyph: "settings"
                tip: qsTr("Preferences")
                onClicked: bar.preferencesRequested()
            }
        }
    }

    KpMenu {
        id: panelsMenu
        x: bar.width - width - KP.gapM
        y: bar.height - 2

        Repeater {
            model: bar.offeredPanels
            KpMenuItem {
                required property var modelData
                text: modelData.title
                checkable: true
                checked: modelData.visible
                onTriggered: Dock.togglePanel(modelData.id)
            }
        }
        KpMenuSeparator { }
        KpMenuItem {
            text: qsTr("Reset layout")
            onTriggered: Dock.resetToDefault()
        }
    }

    KpMenu {
        id: barMenu
        KpMenuItem {
            text: qsTr("Document group")
            checkable: true
            checked: bar.showDocumentGroup
            onTriggered: { bar.showDocumentGroup = !bar.showDocumentGroup; bar.storeGroups() }
        }
        KpMenuItem {
            text: qsTr("Edit group")
            checkable: true
            checked: bar.showEditGroup
            onTriggered: { bar.showEditGroup = !bar.showEditGroup; bar.storeGroups() }
        }
        KpMenuItem {
            text: qsTr("View group")
            checkable: true
            checked: bar.showViewGroup
            onTriggered: { bar.showViewGroup = !bar.showViewGroup; bar.storeGroups() }
        }
        KpMenuItem {
            text: qsTr("Brush group")
            checkable: true
            checked: bar.showBrushGroup
            onTriggered: { bar.showBrushGroup = !bar.showBrushGroup; bar.storeGroups() }
        }
    }

    // Right-click anywhere on the bar to choose which groups it shows.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: barMenu.popup()
    }
}
