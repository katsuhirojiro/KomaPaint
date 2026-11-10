import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

Rectangle {
    id: panel
    color: KP.paneBg
    border.color: KP.border
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        PanelHeader {
            Layout.fillWidth: true
            title: "Layers"
            onCloseClicked: Panels.setVisible("layers", false)
        }

        // Layer list
        ListView {
            id: layerList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Workspace.layers
            clip: true
            spacing: 1
            currentIndex: Workspace.layers.activeLayerIndex
            onCurrentIndexChanged: Workspace.layers.setActiveLayerIndex(currentIndex)
            delegate: Rectangle {
                width: parent.width
                height: 30
                color: model.index === layerList.currentIndex ? KP.selection : KP.paneBgAlt
                border.color: KP.separator
                border.width: 1
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: layerList.currentIndex = model.index
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 6
                    // visibility eye
                    Label {
                        text: model.visible ? "◉" : "○"
                        font.family: KP.fontUi
                        font.pixelSize: 11
                        color: model.visible ? KP.text : KP.textSubtle
                        Layout.preferredWidth: 14
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var l = Workspace.layers.layerAt(model.index);
                                if (l)
                                    l.visible = !l.visible;
                            }
                        }
                    }
                    // lock
                    Label {
                        text: model.locked ? "🔒" : ""
                        font.pixelSize: 9
                        Layout.preferredWidth: 12
                        color: KP.textSubtle
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var l = Workspace.layers.layerAt(model.index);
                                if (l)
                                    l.locked = !l.locked;
                            }
                        }
                    }
                    // thumbnail placeholder
                    Rectangle {
                        width: 22
                        height: 22
                        color: KP.inputBg
                        border.color: KP.border
                        radius: KP.radiusS
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
                        color: KP.text
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: Math.round(model.opacity * 100) + "%"
                        color: KP.textSubtle
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontXS
                    }
                }
            }
        }

        // Footer row: add / delete
        Rectangle {
            Layout.fillWidth: true
            height: 26
            color: KP.paneHeaderBg
            border.color: KP.border
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 2
                ToolbarButton {
                    iconText: "+"
                    tooltip: "Add layer"
                    onClicked: Workspace.layers.addLayer()
                }
                ToolbarButton {
                    iconText: "−"
                    tooltip: "Delete layer"
                    onClicked: Workspace.layers.removeLayer(Workspace.layers.activeLayerIndex)
                }
                Item {
                    Layout.fillWidth: true
                }
                ToolbarButton {
                    iconText: "▲"
                    tooltip: "Move up"
                    onClicked: Workspace.layers.moveLayer(Workspace.layers.activeLayerIndex, Workspace.layers.activeLayerIndex - 1)
                }
                ToolbarButton {
                    iconText: "▼"
                    tooltip: "Move down"
                    onClicked: Workspace.layers.moveLayer(Workspace.layers.activeLayerIndex, Workspace.layers.activeLayerIndex + 1)
                }
            }
        }
    }
}
