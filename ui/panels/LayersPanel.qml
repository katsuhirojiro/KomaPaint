import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Layers panel.
//
//   header
//   layer attribute bar   lock · lock transparency · reference · mask      merge down
//   layer list            [eye][lock] thumbnail  name                opacity
//   command bar           new raster · new vector · folder · adjustment | move ▲ ▼   delete
//
// Icons come from ui/assets/icons/layers. Buttons whose function the
// application does not have yet are shown as unavailable (dimmed, tooltip
// says so) rather than pretending to work.
Rectangle {
    id: panel
    color: KP.paneBg
    border.color: KP.border
    border.width: 1

    readonly property int active: Workspace.layers.activeLayerIndex
    readonly property var activeLayer: layerList.count > 0 ? Workspace.layers.layerAt(active) : null
    readonly property int barHeight: 30
    readonly property int barCell: 24
    readonly property int barIcon: 18

    component BarRule: Rectangle {
        Layout.leftMargin: 3
        Layout.rightMargin: 3
        implicitWidth: 1
        implicitHeight: 16
        color: KP.separator
    }
    // ▲ / ▼ : no icon files exist for these two, so they stay text.
    component ArrowButton: AbstractButton {
        id: arrow
        property string tooltip: ""
        implicitWidth: 20
        implicitHeight: panel.barCell
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: tooltip
        ToolTip.text: tooltip
        ToolTip.visible: hovered
        ToolTip.delay: 500
        background: Rectangle {
            radius: KP.radiusM
            color: !arrow.enabled ? "transparent" : arrow.down ? KP.buttonBgPress : (arrow.hovered ? KP.buttonBgHover : "transparent")
        }
        contentItem: Label {
            text: arrow.text
            color: !arrow.enabled ? KP.iconDisabled : (arrow.hovered ? KP.iconHover : KP.iconNormal)
            font.family: KP.fontUi
            font.pixelSize: 9
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PanelHeader {
            Layout.fillWidth: true
            title: qsTr("Layers")
            onCloseClicked: Panels.setVisible("layers", false)
        }

        // ---- Attributes of the selected layer
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: panel.barHeight
            color: KP.paneBgAlt
            clip: true
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 1
                IconButton {
                    objectName: "layerLockButton"
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/lock"
                    tooltip: selected ? qsTr("Unlock layer") : qsTr("Lock layer")
                    enabled: panel.activeLayer !== null
                    selected: panel.activeLayer !== null && panel.activeLayer.locked
                    onActivated: panel.activeLayer.locked = !panel.activeLayer.locked
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/lock-transparency"
                    tooltip: qsTr("Lock transparent pixels")
                    available: false
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/reference"
                    tooltip: qsTr("Set as reference layer")
                    available: false
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/mask"
                    tooltip: qsTr("Layer mask")
                    available: false
                }
                Item {
                    Layout.fillWidth: true
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/merge-down"
                    tooltip: qsTr("Merge with layer below")
                    available: false
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.separator
            }
        }

        // ---- Layer list
        ListView {
            id: layerList
            objectName: "layerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Workspace.layers
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: Workspace.layers.activeLayerIndex
            onCurrentIndexChanged: Workspace.layers.setActiveLayerIndex(currentIndex)
            delegate: Rectangle {
                id: row
                readonly property bool current: model.index === layerList.currentIndex
                readonly property var layerObject: Workspace.layers.layerAt(model.index)
                width: layerList.width
                height: 36
                color: current ? KP.toolActive : (rowMouse.containsMouse ? KP.buttonBgHover : KP.paneBgAlt)

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: layerList.currentIndex = model.index
                }
                // Visibility and lock live in their own column, so they read
                // the same on a selected and an unselected row.
                Rectangle {
                    id: toggles
                    width: 52
                    height: parent.height
                    color: KP.paneHeaderBg
                    Row {
                        anchors.centerIn: parent
                        spacing: 0
                        IconButton {
                            objectName: "layerVisible" + model.index
                            cell: 24
                            iconSize: 18
                            iconName: "layers/visible"
                            dimmed: !model.visible
                            tooltip: model.visible ? qsTr("Hide layer") : qsTr("Show layer")
                            onActivated: if (row.layerObject) row.layerObject.visible = !row.layerObject.visible
                        }
                        IconButton {
                            objectName: "layerLocked" + model.index
                            cell: 24
                            iconSize: 18
                            iconName: "layers/lock"
                            dimmed: !model.locked
                            tooltip: model.locked ? qsTr("Unlock layer") : qsTr("Lock layer")
                            onActivated: if (row.layerObject) row.layerObject.locked = !row.layerObject.locked
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: KP.separator
                    }
                }
                RowLayout {
                    anchors.left: toggles.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 6
                    anchors.rightMargin: 8
                    spacing: 8
                    // thumbnail placeholder
                    Rectangle {
                        implicitWidth: 26
                        implicitHeight: 26
                        color: KP.inputBg
                        border.color: row.current ? KP.textOnActive : KP.border
                        radius: KP.radiusS
                        opacity: model.visible ? 1 : 0.45
                        Canvas {
                            anchors.fill: parent
                            anchors.margins: 1
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.fillStyle = "#6a6d73";
                                ctx.fillRect(0, 0, width, height);
                                ctx.fillStyle = "#8b8f95";
                                for (var y = 0; y < height; y += 4)
                                    for (var x = 0; x < width; x += 4)
                                        if (((x / 4) + (y / 4)) % 2 === 0)
                                            ctx.fillRect(x, y, 4, 4);
                            }
                        }
                    }
                    Label {
                        text: model.name
                        color: row.current ? KP.textOnActive : (model.visible ? KP.text : KP.textSubtle)
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontM
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: Math.round(model.opacity * 100) + "%"
                        color: row.current ? KP.textOnActiveMuted : KP.textMuted
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontS
                    }
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: KP.separator
                }
            }
        }

        // ---- Commands
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: panel.barHeight
            color: KP.paneHeaderBg
            clip: true
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: KP.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 1
                IconButton {
                    objectName: "addLayerButton"
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/new-raster"
                    tooltip: qsTr("New raster layer")
                    onActivated: Workspace.layers.addLayer()
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/new-vector"
                    tooltip: qsTr("New vector layer")
                    available: false
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/folder"
                    tooltip: qsTr("New layer folder")
                    available: false
                }
                IconButton {
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/adjustment-layer"
                    tooltip: qsTr("New adjustment layer")
                    available: false
                }
                BarRule {
                }
                // Reordering: the icon names the pair of arrow buttons.
                KpIcon {
                    name: "layers/move"
                    size: panel.barIcon
                    color: KP.iconNormal
                    opacity: layerList.count > 1 ? 1 : 0.4
                }
                ArrowButton {
                    objectName: "layerUpButton"
                    text: "▲"
                    tooltip: qsTr("Move layer up")
                    enabled: panel.active > 0
                    onClicked: Workspace.layers.moveLayer(panel.active, panel.active - 1)
                }
                ArrowButton {
                    objectName: "layerDownButton"
                    text: "▼"
                    tooltip: qsTr("Move layer down")
                    enabled: panel.active >= 0 && panel.active < layerList.count - 1
                    onClicked: Workspace.layers.moveLayer(panel.active, panel.active + 1)
                }
                Item {
                    Layout.fillWidth: true
                }
                IconButton {
                    objectName: "deleteLayerButton"
                    cell: panel.barCell
                    iconSize: panel.barIcon
                    iconName: "layers/delete"
                    tooltip: qsTr("Delete layer")
                    // The last remaining layer cannot be deleted.
                    enabled: layerList.count > 1
                    onActivated: Workspace.layers.removeLayer(panel.active)
                }
            }
        }
    }
}
