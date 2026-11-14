import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Layers.
//
// The panel is a stack, so it is drawn as one: rows sit flush against each other
// with no boxes, the active layer is marked by an ember edge and a wash, and the
// two states that matter while painting — visible and locked — are always on the
// row, in a fixed column, so they read the same whether the row is selected or
// not. Opacity is shown on the selected row only, where it is being used, and
// names are set in the interface face while the count and percentages are set in
// tabular figures.
//
// Layer thumbnails need the engine to hand pixels to QML (a QQuickImageProvider
// over the tile store); until that exists the row carries the layer's kind and
// state rather than a placeholder picture pretending to be artwork.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property int active: Workspace.layers.activeLayerIndex
    readonly property var activeLayer: layerList.count > 0 ? Workspace.layers.layerAt(active) : null
    readonly property int barHeight: 28

    component BarRule: Rectangle {
        Layout.leftMargin: KP.gapXS
        Layout.rightMargin: KP.gapXS
        implicitWidth: 1
        implicitHeight: 14
        color: KP.hairline
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Attributes of the selected layer
        Item {
            Layout.fillWidth: true
            implicitHeight: panel.barHeight

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapS
                spacing: 0

                IconButton {
                    objectName: "layerLockButton"
                    cell: 22
                    iconSize: 16
                    iconName: "layers/lock"
                    tooltip: selected ? qsTr("Unlock layer") : qsTr("Lock layer")
                    enabled: panel.activeLayer !== null
                    selected: panel.activeLayer !== null && panel.activeLayer.locked
                    accentBar: true
                    onActivated: panel.activeLayer.locked = !panel.activeLayer.locked
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/lock-transparency"
                    tooltip: qsTr("Lock transparent pixels")
                    available: false
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/reference"
                    tooltip: qsTr("Set as reference layer")
                    available: false
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/mask"
                    tooltip: qsTr("Layer mask")
                    available: false
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    cell: 22
                    iconSize: 16
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
                color: KP.hairline
            }
        }

        // ---- The stack
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
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

            delegate: AbstractButton {
                id: row
                required property int index
                readonly property bool current: index === layerList.currentIndex
                // The row is one Layer object, read through the model: role
                // names would collide with Item's own `visible` and `opacity`,
                // and the Layer notifies for each of these, so the row follows
                // renames, hiding, locking and opacity without a refresh.
                readonly property var layerObject: Workspace.layers.layerAt(index)
                readonly property string layerName: layerObject ? layerObject.name : ""
                readonly property bool layerVisible: layerObject ? layerObject.visible : true
                readonly property bool layerLocked: layerObject ? layerObject.locked : false
                readonly property real layerOpacity: layerObject ? layerObject.opacity : 1

                width: layerList.width
                height: 30
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: row.layerName
                onClicked: layerList.currentIndex = row.index

                background: Rectangle {
                    color: row.current ? KP.rowSelected
                         : row.hovered ? KP.hoverOverlay : (row.index % 2 === 0 ? "transparent" : KP.hoverOverlay)
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
                        opacity: 0.55
                    }
                }

                contentItem: Item {
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: KP.panelInset
                        anchors.rightMargin: KP.gapS
                        spacing: KP.gapS

                        Text {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 14
                            text: (row.index + 1)
                            color: row.current ? KP.textMuted : KP.textSubtle
                            font.pixelSize: KP.fontMicro
                            font.family: KP.fontMono
                            horizontalAlignment: Text.AlignRight
                        }
                        KpIcon {
                            Layout.alignment: Qt.AlignVCenter
                            name: "layers/new-raster"
                            size: 15
                            color: row.current ? KP.iconSelected : KP.iconOff
                            opacity: row.layerVisible ? 1 : 0.5
                        }
                        Text {
                            id: nameLabel
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: true
                            text: row.layerName
                            color: row.current ? KP.textAccent
                                 : row.layerVisible ? KP.text : KP.textSubtle
                            font.pixelSize: KP.fontS + 1
                            font.weight: row.current ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        Item {
                            id: toggles
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: opacityLabel.width + 2 * 20 + KP.gapXS
                            Layout.preferredHeight: 24

                            Text {
                                id: opacityLabel
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30
                                text: Math.round(row.layerOpacity * 100) + "%"
                                color: row.current ? KP.textMuted : KP.textSubtle
                                font.pixelSize: KP.fontMicro
                                font.family: KP.fontMono
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            IconButton {
                                objectName: "layerVisible" + row.index
                                x: opacityLabel.width + KP.gapXS
                                cell: 20
                                iconSize: 15
                                iconName: "layers/visible"
                                dimmed: !row.layerVisible
                                tooltip: row.layerVisible ? qsTr("Hide layer") : qsTr("Show layer")
                                opacity: row.layerVisible || row.hovered || row.current ? 1 : 0.45
                                onActivated: if (row.layerObject) row.layerObject.visible = !row.layerObject.visible
                            }
                            IconButton {
                                objectName: "layerLocked" + row.index
                                x: opacityLabel.width + KP.gapXS + 20
                                cell: 20
                                iconSize: 15
                                iconName: "layers/lock"
                                dimmed: !row.layerLocked
                                tooltip: row.layerLocked ? qsTr("Unlock layer") : qsTr("Lock layer")
                                opacity: row.layerLocked || row.hovered || row.current ? 1 : 0.45
                                onActivated: if (row.layerObject) row.layerObject.locked = !row.layerObject.locked
                            }
                        }
                    }
                }
            }
        }

        // ---- Commands
        Item {
            Layout.fillWidth: true
            implicitHeight: panel.barHeight

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: KP.hairline
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapS
                spacing: 0

                IconButton {
                    objectName: "addLayerButton"
                    cell: 22
                    iconSize: 16
                    iconName: "layers/new-raster"
                    tooltip: qsTr("New raster layer")
                    onActivated: Workspace.layers.addLayer()
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/new-vector"
                    tooltip: qsTr("New vector layer")
                    available: false
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/folder"
                    tooltip: qsTr("New layer folder")
                    available: false
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/adjustment-layer"
                    tooltip: qsTr("New adjustment layer")
                    available: false
                }
                BarRule { }
                IconButton {
                    objectName: "layerUpButton"
                    cell: 22
                    iconSize: 13
                    glyph: "chevron-up"
                    tooltip: qsTr("Move layer up")
                    enabled: panel.active > 0
                    onActivated: Workspace.layers.moveLayer(panel.active, panel.active - 1)
                }
                IconButton {
                    objectName: "layerDownButton"
                    cell: 22
                    iconSize: 13
                    glyph: "chevron-down"
                    tooltip: qsTr("Move layer down")
                    enabled: panel.active >= 0 && panel.active < layerList.count - 1
                    onActivated: Workspace.layers.moveLayer(panel.active, panel.active + 1)
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    objectName: "deleteLayerButton"
                    cell: 22
                    iconSize: 16
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
