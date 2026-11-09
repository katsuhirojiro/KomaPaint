import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Rectangle {
    id: ws
    color: KP.canvasBg

    // Inner scrollable working area
    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 24
        contentWidth: canvasDoc.width + 40
        contentHeight: canvasDoc.height + 40
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.horizontal: ScrollBar {
            active: true
        }
        ScrollBar.vertical: ScrollBar {
            active: true
        }

        Item {
            id: docHost
            width: flick.contentWidth
            height: flick.contentHeight

            // Document frame
            Rectangle {
                id: canvasDoc
                anchors.centerIn: parent
                width: Math.min(Workspace.document.width * Workspace.zoom / 100, 900)
                height: Math.min(Workspace.document.height * Workspace.zoom / 100, 700)
                color: "transparent"
                border.color: "#0c0d0f"
                border.width: 1

                // Drop shadow / document edge
                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: -4
                    anchors.leftMargin: -4
                    anchors.rightMargin: 6
                    anchors.bottomMargin: 8
                    z: -1
                    color: "transparent"
                    layer.enabled: true
                    Rectangle {
                        anchors.fill: parent
                        color: "#00000040"
                        radius: 1
                    }
                }

                // Checkerboard + paper content
                Canvas {
                    id: canvasSurface
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        var cell = Math.max(6, Math.round(12 * Workspace.zoom / 100));
                        if (Workspace.document.hasTransparentBackground) {
                            // checkerboard
                            ctx.fillStyle = "#5a5d63";
                            ctx.fillRect(0, 0, width, height);
                            ctx.fillStyle = "#767a81";
                            for (var y = 0; y < height; y += cell)
                                for (var x = 0; x < width; x += cell)
                                    if (((x / cell) + (y / cell)) % 2 === 0)
                                        ctx.fillRect(x, y, cell, cell);
                        } else {
                            ctx.fillStyle = KP.canvasPaper;
                            ctx.fillRect(0, 0, width, height);
                        }
                    }
                    Connections {
                        target: Workspace
                        function onZoomChanged() {
                            canvasSurface.requestPaint();
                        }
                    }
                    Connections {
                        target: Workspace.document
                        function onBackgroundChanged() {
                            canvasSurface.requestPaint();
                        }
                    }
                }

                // Centered "No content yet" hint
                Label {
                    anchors.centerIn: parent
                    text: Workspace.document.title
                    color: "#00000066"
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXL
                    font.italic: true
                    visible: Workspace.zoom >= 40
                }
            }
        }
    }

    // Corner zoom / doc info overlay (bottom-left)
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 6
        anchors.bottomMargin: 6
        width: infoText.implicitWidth + 12
        height: infoText.implicitHeight + 6
        color: "#00000066"
        radius: KP.radiusS
        Label {
            id: infoText
            anchors.centerIn: parent
            text: Workspace.document.width + " × " + Workspace.document.height + " px  •  " + Math.round(Workspace.document.dpi) + " dpi"
            color: "#e7e8ea"
            font.family: KP.fontMono
            font.pixelSize: KP.fontXS
        }
    }
}
