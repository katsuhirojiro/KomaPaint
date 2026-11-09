import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

Rectangle {
    id: timeline
    color: KP.paneBg
    border.color: KP.border
    border.width: 1
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        PanelHeader {
            Layout.fillWidth: true
            title: "Timeline"
            onCloseClicked: Panels.setVisible("timeline", false)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // Left playback / layers column
            Rectangle {
                width: 140
                Layout.fillHeight: true
                color: KP.paneHeaderBg
                border.color: KP.border
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4
                    // Playback controls
                    Row {
                        spacing: 2
                        ToolbarButton {
                            iconText: "⏮"
                            fontPx: 11
                            onClicked: Workspace.timeline.stop()
                        }
                        ToolbarButton {
                            iconText: Workspace.timeline.playing ? "⏸" : "▶"
                            fontPx: 11
                            onClicked: Workspace.timeline.playing = !Workspace.timeline.playing
                        }
                        ToolbarButton {
                            iconText: "⏭"
                            fontPx: 11
                            enabled: false
                        }
                    }
                    Label {
                        text: "Frame " + (Workspace.timeline.currentFrame + 1) + " / " + Workspace.timeline.frameCount
                        color: KP.text
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontS
                    }
                    Label {
                        text: Workspace.timeline.fps + " fps"
                        color: KP.textSubtle
                        font.family: KP.fontMono
                        font.pixelSize: KP.fontXS
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                    Label {
                        text: "Animation support is not implemented yet. This area is reserved for the timeline, exposure sheet and playback controls."
                        color: KP.textSubtle
                        wrapMode: Text.WordWrap
                        font.pixelSize: KP.fontXS
                        font.family: KP.fontUi
                    }
                }
            }
            // Frame cells area
            Flickable {
                id: frameFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: frameRow.width
                contentHeight: frameRow.height + ruler.height + 6
                boundsBehavior: Flickable.DragOverBounds
                Column {
                    id: ruler
                    width: Math.max(frameFlick.width, frameRow.width)
                    height: 18
                    Repeater {
                        model: Workspace.timeline.frameCount
                        Label {
                            x: model.index * 26 + 8
                            width: 24
                            height: 18
                            text: (model.index + 1)
                            color: (model.index === Workspace.timeline.currentFrame) ? KP.textAccent : KP.textSubtle
                            font.family: KP.fontMono
                            font.pixelSize: KP.fontXS
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignBottom
                        }
                    }
                }
                Row {
                    id: frameRow
                    y: ruler.height + 2
                    height: 36
                    Repeater {
                        model: Workspace.timeline.frameCount
                        Rectangle {
                            width: 24
                            height: 36
                            x: model.index * 26 + 4
                            color: model.index === Workspace.timeline.currentFrame ? KP.toolActive : (model.index % 2 === 0 ? KP.paneBgAlt : KP.paneBg)
                            border.color: KP.border
                            radius: KP.radiusS
                            Text {
                                anchors.centerIn: parent
                                text: model.index === 0 ? "●" : ""
                                font.pixelSize: 12
                                color: KP.textMuted
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: Workspace.timeline.currentFrame = model.index
                            }
                        }
                    }
                }
                // Playhead indicator
                Rectangle {
                    x: 4 + Workspace.timeline.currentFrame * 26 + 8 - 1
                    y: ruler.height
                    width: 2
                    height: 36 + 4
                    color: KP.accent
                }
            }
        }
    }
}
