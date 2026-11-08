import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"
import "../panels"
import "../themes"

ApplicationWindow {
    id: root

    visible: true
    title: App.versionDisplay
    width: Settings.windowSize.width
    height: Settings.windowSize.height
    visibility: Settings.windowMaximized ? Window.Maximized : Window.Windowed
    color: KP.windowBg

    minimumWidth: 1024
    minimumHeight: 640

    // Persist window size when the user resizes / (un)maximizes.
    onWidthChanged: if (visibility === Window.Windowed)
        Settings.setWindowSize(Qt.size(width, height))
    onHeightChanged: if (visibility === Window.Windowed)
        Settings.setWindowSize(Qt.size(width, height))
    onVisibilityChanged: Settings.setWindowMaximized(visibility === Window.Maximized)

    Component.onCompleted:
    // Nothing to perform yet — document creation, welcome dialog, etc.
    // will hook in here as they are implemented.
    {
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
                    onTriggered: /* placeholder */  {
                    }
                }
                MenuItem {
                    text: qsTr("&Open...")
                    onTriggered: /* placeholder */  {
                    }
                }
                MenuItem {
                    text: qsTr("&Save")
                    onTriggered: /* placeholder */  {
                    }
                }
                MenuItem {
                    text: qsTr("Save &As...")
                    onTriggered: /* placeholder */  {
                    }
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
                    text: qsTr("E&xit")
                    onTriggered: Qt.quit()
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
                    text: qsTr("Zoom &In")
                    onTriggered: Workspace.zoomIn()
                    shortcut: "Ctrl++"
                }
                MenuItem {
                    text: qsTr("Zoom &Out")
                    onTriggered: Workspace.zoomOut()
                    shortcut: "Ctrl+-"
                }
                MenuItem {
                    text: qsTr("&Actual Pixels")
                    onTriggered: Workspace.resetZoom()
                    shortcut: "Ctrl+1"
                }
                MenuItem {
                    text: qsTr("Fit to Window")
                    onTriggered: Workspace.zoomFit()
                    shortcut: "Ctrl+0"
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
                    onTriggered: Workspace.layers.removeLayer(Workspace.layers.activeLayerIndex())
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

        // ------------------------------------------------------------- Toolbar
        MainToolbar {
            id: toolbar
            Layout.fillWidth: true
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
                    Layout.fillHeight: true
                    spacing: 1

                    LayersPanel {
                        id: layersPanel
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Panels.isVisible("layers")
                        onVisibilityChanged: if (visible && !Panels.isVisible("layers"))
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
