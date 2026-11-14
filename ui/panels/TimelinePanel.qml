import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// The animation rail.
//
// Playback is not implemented by the engine yet, so this panel shows only what
// is real: the frame count and rate the document reports, the current frame, and
// a rail you can scrub. The controls that would do nothing (step to end, onion
// skin, recording) are simply absent rather than greyed out as decoration.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property var timeline: Workspace.timeline

    component RailRule: Rectangle {
        width: 1
        height: 16
        color: KP.hairline
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Transport
        Item {
            Layout.fillWidth: true
            implicitHeight: 30

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapM
                anchors.rightMargin: KP.gapM
                spacing: KP.gapXS

                IconButton {
                    objectName: "timelineStopButton"
                    glyph: "stop"
                    iconSize: 13
                    cell: 22
                    tooltip: qsTr("Return to the first frame")
                    onActivated: panel.timeline.stop()
                }
                IconButton {
                    objectName: "timelinePlayButton"
                    glyph: panel.timeline.playing ? "pause" : "play"
                    iconSize: 13
                    cell: 22
                    selected: panel.timeline.playing
                    accentBar: true
                    tooltip: panel.timeline.playing ? qsTr("Pause") : qsTr("Play")
                    onActivated: panel.timeline.playing = !panel.timeline.playing
                }

                RailRule { }

                Text {
                    text: qsTr("FRAME")
                    color: KP.textSectionLabel
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                Text {
                    text: (panel.timeline.currentFrame + 1) + " / " + panel.timeline.frameCount
                    color: KP.text
                    font.pixelSize: KP.fontS + 1
                    font.family: KP.fontMono
                }
                RailRule { }
                Text {
                    text: panel.timeline.fps + " fps"
                    color: KP.textMuted
                    font.pixelSize: KP.fontXS
                    font.family: KP.fontMono
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("Frame timing is not editable yet")
                    color: KP.textSubtle
                    font.pixelSize: KP.fontXS
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

        // ---- Frames
        Flickable {
            id: frameFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, frameRow.width + 2 * KP.gapM)
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: KpScrollBar {
                policy: frameFlick.contentWidth > frameFlick.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            Item {
                id: frameRow
                x: KP.gapM
                width: panel.timeline.frameCount * 26 + 8
                height: frameFlick.height

                // Number rail
                Row {
                    id: numbers
                    y: KP.gapS
                    Repeater {
                        model: panel.timeline.frameCount
                        Text {
                            required property int index
                            width: 26
                            height: 14
                            text: index + 1
                            color: index === panel.timeline.currentFrame ? KP.textAccent : KP.textSubtle
                            font.pixelSize: KP.fontMicro
                            font.family: KP.fontMono
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                // Cells: the first frame holds the drawing in this build, and
                // every cell is a real target for the current-frame value.
                Row {
                    id: cells
                    y: KP.gapS + 18
                    Repeater {
                        model: panel.timeline.frameCount
                        AbstractButton {
                            id: cell
                            required property int index
                            width: 24
                            height: Math.max(24, frameFlick.height - KP.gapS - 26)
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            Accessible.name: qsTr("Frame %1").arg(index + 1)
                            onClicked: panel.timeline.currentFrame = index

                            background: Rectangle {
                                color: "transparent"
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.rightMargin: 2
                                    radius: KP.radiusS
                                    color: cell.index === panel.timeline.currentFrame ? KP.rowSelected
                                         : cell.hovered ? KP.hoverOverlay
                                         : (cell.index % 2 === 0 ? KP.paneBgAlt : "transparent")
                                    border.width: cell.index === panel.timeline.currentFrame ? 1 : 0
                                    border.color: KP.ember
                                }
                            }
                            contentItem: Item {
                                Rectangle {
                                    visible: cell.index === 0
                                    anchors.centerIn: parent
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: cell.index === panel.timeline.currentFrame ? KP.text : KP.textMuted
                                }
                            }
                        }
                    }
                }

                // Playhead
                Rectangle {
                    x: panel.timeline.currentFrame * 26
                    y: KP.gapS - 2
                    width: 1
                    height: frameFlick.height - KP.gapS
                    color: KP.ember
                    opacity: 0.85
                }
            }
        }
    }
}
