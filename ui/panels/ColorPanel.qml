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
            title: "Color"
            onCloseClicked: Panels.setVisible("color", false)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            // HSV picker placeholder — a satur/value rectangle with a hue strip.
            // Not an interactive picker yet; serves as a visual placeholder.
            Canvas {
                id: picker
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                onPaint: {
                    var ctx = getContext("2d");
                    // Saturation/Value gradient approximated as dark→saturated swatches.
                    var w = width, h = height - 10;
                    for (var y = 0; y < h; y++) {
                        var grd = ctx.createLinearGradient(0, 0, w, 0);
                        grd.addColorStop(0, "hsl(210,0%," + Math.round((1 - y / h) * 70 + 10) + "%)");
                        grd.addColorStop(1, panel.hslString(Workspace.colors.foreground));
                        ctx.fillStyle = grd;
                        ctx.fillRect(0, y, w, 1);
                    }
                    // Hue strip
                    for (var x = 0; x < width; x++) {
                        ctx.fillStyle = "hsl(" + Math.round(x / width * 360) + ", 80%, 50%)";
                        ctx.fillRect(x, h, 1, 10);
                    }
                }
                Connections {
                    target: Workspace.colors
                    function onForegroundChanged() {
                        picker.requestPaint();
                    }
                }
            }

            // Foreground/Background swatches with HSB inputs
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    width: 38
                    height: 38
                    radius: KP.radiusS
                    color: Workspace.colors.background
                    border.color: KP.border
                    border.width: 1
                    Rectangle {
                        x: 10
                        y: -2
                        width: 24
                        height: 24
                        radius: KP.radiusS
                        color: Workspace.colors.foreground
                        border.color: KP.border
                        border.width: 1
                        MouseArea {
                            anchors.fill: parent
                            onDoubleClicked: Workspace.colors.swap()
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: Workspace.colors.swap()
                    }
                }
                ColumnLayout {
                    spacing: 2
                    Layout.fillWidth: true
                    RowLayout {
                        spacing: 4
                        Label {
                            text: "R"
                            color: KP.textSubtle
                            font.pixelSize: KP.fontXS
                            font.family: KP.fontUi
                            Layout.preferredWidth: 8
                        }
                        SpinBox {
                            id: rSpin
                            from: 0
                            to: 255
                            value: Workspace.colors.foreground.r
                            editable: true
                            Layout.fillWidth: true
                            font.pixelSize: KP.fontS
                            font.family: KP.fontMono
                            palette.text: KP.text
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                            onValueModified: Workspace.colors.foreground = Qt.rgba(value / 255, gSpin.value / 255, bSpin.value / 255, 1)
                        }
                    }
                    RowLayout {
                        spacing: 4
                        Label {
                            text: "G"
                            color: KP.textSubtle
                            font.pixelSize: KP.fontXS
                            font.family: KP.fontUi
                            Layout.preferredWidth: 8
                        }
                        SpinBox {
                            id: gSpin
                            from: 0
                            to: 255
                            value: Workspace.colors.foreground.g
                            editable: true
                            Layout.fillWidth: true
                            font.pixelSize: KP.fontS
                            font.family: KP.fontMono
                            palette.text: KP.text
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                            onValueModified: Workspace.colors.foreground = Qt.rgba(rSpin.value / 255, value / 255, bSpin.value / 255, 1)
                        }
                    }
                    RowLayout {
                        spacing: 4
                        Label {
                            text: "B"
                            color: KP.textSubtle
                            font.pixelSize: KP.fontXS
                            font.family: KP.fontUi
                            Layout.preferredWidth: 8
                        }
                        SpinBox {
                            id: bSpin
                            from: 0
                            to: 255
                            value: Workspace.colors.foreground.b
                            editable: true
                            Layout.fillWidth: true
                            font.pixelSize: KP.fontS
                            font.family: KP.fontMono
                            palette.text: KP.text
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                            onValueModified: Workspace.colors.foreground = Qt.rgba(rSpin.value / 255, gSpin.value / 255, value / 255, 1)
                        }
                    }
                    RowLayout {
                        spacing: 4
                        Label {
                            text: "#"
                            color: KP.textSubtle
                            font.pixelSize: KP.fontXS
                            font.family: KP.fontMono
                            Layout.preferredWidth: 8
                        }
                        Label {
                            text: hex(Workspace.colors.foreground)
                            color: KP.text
                            font.family: KP.fontMono
                            font.pixelSize: KP.fontS
                        }
                    }
                }
            }

            // Quick swatches (curated greyscale + common hues)
            Grid {
                Layout.fillWidth: true
                rows: 2
                columns: 10
                spacing: 2
                Repeater {
                    model: ["#000000", "#1f2124", "#3a3d42", "#5a5d63", "#7a7d83", "#a0a3a8", "#c7c8cc", "#e7e8ea", "#ffffff", "#b84f4f", "#c8913a", "#d9c54a", "#6ea764", "#4e9a9c", "#6b9bd1", "#8969bd", "#aa5a87", "#8f5e3b"]
                    Rectangle {
                        width: 22
                        height: 16
                        color: modelData
                        border.color: KP.border
                        radius: KP.radiusS
                        MouseArea {
                            anchors.fill: parent
                            onClicked: Workspace.colors.foreground = modelData
                        }
                    }
                }
            }
        }
    }

    function hex(c) {
        function h(v) {
            return v.toString(16).padStart(2, '0');
        }
        return ("#" + h(c.r) + h(c.g) + h(c.b)).toUpperCase();
    }
    function hslString(c) {
        return "hsl(" + Math.round(c.hslHue) + "," + Math.round(c.hslSaturation * 100) + "%," + Math.round(c.hslLightness * 100) + "%)";
    }
}
