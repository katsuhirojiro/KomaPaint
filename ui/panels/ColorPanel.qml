import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// The colour instrument.
//
// A painter chooses colour constantly, so this is a working surface rather than
// a form: a saturation/value plane, a hue rail, an exact numeric readout, the
// two working swatches and a short history of what has been used. Everything is
// live — the plane really picks, the rail really shifts hue, the numbers can be
// typed, and the recent strip is the colours actually chosen this session.
//
// The panel writes Workspace.colors.foreground, the colour the canvas paints
// with and the swatch in the tool rail shows.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property var colors: Workspace.colors
    // Hue is remembered even when the current colour is grey, where the colour
    // itself reports no hue; otherwise the plane would jump on every greyscale
    // pick.
    property real hue: 210
    property real saturation: 0
    property real value: 0
    property bool updating: false
    // Colours picked this session, newest first. Nothing is invented here: the
    // list is the foreground colour's own history.
    property var recent: []

    // The dock sizes the colour area from this before the panel is laid out, so
    // it is a constant: swatches, the plane, the hue rail, the numbers and two
    // short rows of recent and neutral colour — all of them visible without
    // scrolling at the default window size. Anything taller scrolls.
    readonly property int preferredHeight: 288

    function componentOf(c) {
        function h(v) { return Math.round(v * 255).toString(16).padStart(2, "0") }
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }
    function pushRecent(c) {
        if (!c) return
        const hex = componentOf(c).toUpperCase()
        const list = recent.slice()
        const at = list.indexOf(hex)
        if (at >= 0) list.splice(at, 1)
        list.unshift(hex)
        recent = list.slice(0, 14)
    }
    function loadFromColour() {
        const c = colors.foreground
        if (!c) return
        if (c.hsvHue >= 0) hue = c.hsvHue
        saturation = c.hsvSaturation
        value = c.hsvValue
    }
    function applyHsv() {
        updating = true
        colors.foreground = Qt.hsva(hue / 360, saturation, value, 1)
        updating = false
    }

    Component.onCompleted: { loadFromColour(); pushRecent(colors.foreground) }
    Connections {
        target: Workspace.colors
        function onForegroundChanged() {
            if (!panel.updating) panel.loadFromColour()
            panel.pushRecent(Workspace.colors.foreground)
        }
    }

    Flickable {
        id: scroller
        objectName: "colorScroller"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: colourColumn.height + 2 * KP.panelInset
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: KpScrollBar {
            policy: scroller.contentHeight > scroller.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
        id: colourColumn
        x: KP.panelInset
        y: KP.panelInset
        width: scroller.width - 2 * KP.panelInset
        spacing: KP.gapS

        // ---- The two working swatches
        RowLayout {
            id: swatchRow
            Layout.fillWidth: true
            implicitHeight: 40
            spacing: KP.gapM

            Item {
                implicitWidth: 62
                implicitHeight: 34

                Rectangle {
                    id: bgSwatch
                    x: 26
                    y: 12
                    width: 30
                    height: 22
                    radius: KP.radiusXS
                    color: Workspace.colors.background
                    border.width: 1
                    border.color: KP.border
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        ToolTip.text: qsTr("Background colour — click to make it the drawing colour")
                        ToolTip.visible: containsMouse
                        ToolTip.delay: 500
                        onClicked: Workspace.colors.swap()
                    }
                }
                Rectangle {
                    id: fgSwatch
                    x: 0
                    y: 0
                    width: 32
                    height: 26
                    radius: KP.radiusXS
                    color: Workspace.colors.foreground
                    border.width: 1
                    border.color: KP.outline
                    // Ember corner: this is the colour that paints.
                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: -1
                        anchors.topMargin: -1
                        width: 5
                        height: 5
                        radius: 2.5
                        color: KP.ember
                    }
                }
                // Clear of both swatches, so the swap mark never sits on a
                // colour the user is trying to read.
                IconButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    cell: 14
                    iconSize: 10
                    glyph: "swap"
                    tooltip: qsTr("Swap drawing and background colour")
                    onActivated: Workspace.colors.swap()
                }
            }

            Column {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: qsTr("DRAWING COLOUR")
                    color: KP.textSectionLabel
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                KpTextField {
                    id: hexField
                    objectName: "colorHexField"
                    width: Math.min(parent.width, 96)
                    height: KP.controlHeightSmall + 2
                    // The hex value is the exact colour; typing it is the fastest
                    // way to reproduce a known colour.
                    text: panel.componentOf(Workspace.colors.foreground).toUpperCase()
                    onSubmitted: function(value) {
                        const cleaned = value.replace(/^#/, "").trim()
                        if (/^[0-9a-fA-F]{6}$/.test(cleaned)) {
                            panel.updating = true
                            Workspace.colors.foreground = "#" + cleaned
                            panel.updating = false
                            panel.loadFromColour()
                        }
                        hexField.text = Qt.binding(function() { return panel.componentOf(Workspace.colors.foreground).toUpperCase() })
                    }
                }
            }
        }

        // ---- Saturation / value plane
        Item {
            id: plane
            Layout.fillWidth: true
            Layout.preferredHeight: 84

            Canvas {
                id: planeCanvas
                anchors.fill: parent
                antialiasing: false
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (width <= 2 || height <= 2) return
                    // Pure hue, then white to the right, then black downwards:
                    // the standard saturation/value square.
                    ctx.fillStyle = "hsl(" + panel.hue + ", 100%, 50%)"
                    ctx.fillRect(0, 0, width, height)
                    const white = ctx.createLinearGradient(0, 0, width, 0)
                    white.addColorStop(0, "rgba(255,255,255,1)")
                    white.addColorStop(1, "rgba(255,255,255,0)")
                    ctx.fillStyle = white
                    ctx.fillRect(0, 0, width, height)
                    const black = ctx.createLinearGradient(0, 0, 0, height)
                    black.addColorStop(0, "rgba(0,0,0,0)")
                    black.addColorStop(1, "rgba(0,0,0,1)")
                    ctx.fillStyle = black
                    ctx.fillRect(0, 0, width, height)
                }
            }
            Connections {
                target: panel
                function onHueChanged() { planeCanvas.requestPaint() }
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: KP.outline
                radius: KP.radiusXS
            }

            // Cursor: a white ring with a dark inner ring, readable on any colour.
            Item {
                x: Math.round(panel.saturation * (plane.width - 1)) - 6
                y: Math.round((1 - panel.value) * (plane.height - 1)) - 6
                width: 12
                height: 12
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.width: 2
                    border.color: "#ffffff"
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 4
                    color: "transparent"
                    border.width: 1
                    border.color: "#00000088"
                }
            }

            MouseArea {
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.CrossCursor
                function pick(x, y) {
                    panel.saturation = Math.max(0, Math.min(1, x / Math.max(1, plane.width - 1)))
                    panel.value = 1 - Math.max(0, Math.min(1, y / Math.max(1, plane.height - 1)))
                    panel.applyHsv()
                }
                onPressed: function(mouse) { pick(mouse.x, mouse.y) }
                onPositionChanged: function(mouse) { if (pressed) pick(mouse.x, mouse.y) }
            }
        }

        // ---- Hue rail
        Item {
            id: rails
            Layout.fillWidth: true
            implicitHeight: 16

            Canvas {
                id: hueCanvas
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const gradient = ctx.createLinearGradient(0, 0, width, 0)
                    for (let i = 0; i <= 6; ++i)
                        gradient.addColorStop(i / 6, "hsl(" + (i * 60) + ", 90%, 52%)")
                    ctx.fillStyle = gradient
                    ctx.fillRect(0, 0, width, height)
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: "transparent"
                border.width: 1
                border.color: KP.outline
            }
            Rectangle {
                // Hue handle
                x: Math.round((panel.hue / 360) * (parent.width - 10))
                width: 10
                height: 16
                radius: 3
                color: "transparent"
                border.width: 2
                border.color: "#ffffff"
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: 4
                    color: "transparent"
                    border.width: 1
                    border.color: "#00000066"
                }
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.SizeHorCursor
                function pick(x) {
                    panel.hue = Math.max(0, Math.min(359, Math.round((x / Math.max(1, rails.width)) * 360)))
                    panel.applyHsv()
                }
                onPressed: function(mouse) { pick(mouse.x) }
                onPositionChanged: function(mouse) { if (pressed) pick(mouse.x) }
            }
        }

        // ---- Exact values
        RowLayout {
            id: numbers
            Layout.fillWidth: true
            spacing: KP.gapS

            KpNumberField {
                objectName: "colorHueField"
                Layout.fillWidth: true
                height: KP.controlHeightSmall + 2
                value: Math.round(panel.hue)
                from: 0
                to: 359
                unit: "H"
                onEdited: function(v) { panel.hue = v; panel.applyHsv() }
            }
            KpNumberField {
                objectName: "colorSaturationField"
                Layout.fillWidth: true
                height: KP.controlHeightSmall + 2
                value: Math.round(panel.saturation * 100)
                from: 0
                to: 100
                unit: "S"
                onEdited: function(v) { panel.saturation = v / 100; panel.applyHsv() }
            }
            KpNumberField {
                objectName: "colorValueField"
                Layout.fillWidth: true
                height: KP.controlHeightSmall + 2
                value: Math.round(panel.value * 100)
                from: 0
                to: 100
                unit: "V"
                onEdited: function(v) { panel.value = v / 100; panel.applyHsv() }
            }
        }

        // ---- Recent colours
        Item {
            id: recentSection
            Layout.fillWidth: true
            implicitHeight: recentColumn.height

            Column {
                id: recentColumn
                width: parent.width
                spacing: KP.gapS

                PanelSection {
                    Layout.fillWidth: true
                    title: qsTr("Recent")
                    hint: panel.recent.length === 1
                          ? qsTr("1 colour")
                          : qsTr("%1 colours").arg(panel.recent.length)
                    contentTopMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: swatchGrid.height
                        Grid {
                            id: swatchGrid
                            width: parent.width
                            columns: 7
                            spacing: 2
                            Repeater {
                                model: panel.recent
                                Rectangle {
                                    required property string modelData
                                    width: Math.floor((swatchGrid.width - 6 * 2) / 7)
                                    height: 15
                                    radius: KP.radiusXS
                                    color: modelData
                                    border.width: 1
                                    border.color: panel.componentOf(Workspace.colors.foreground).toUpperCase() === modelData
                                                  ? KP.accent : KP.border
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Workspace.colors.foreground = modelData
                                    }
                                }
                            }
                        }
                    }
                }

                PanelSection {
                    Layout.fillWidth: true
                    title: qsTr("Neutrals")
                    showRule: true
                    contentTopMargin: KP.gapS

                    Item {
                        width: parent.width
                        height: rampGrid.height
                        Grid {
                            id: rampGrid
                            width: parent.width
                            columns: 7
                            spacing: 2
                            Repeater {
                                model: ["#000000", "#1b1d20", "#33363b", "#4d5157", "#6d7177", "#9297a0", "#c6c9ce",
                                        "#ffffff", "#b23a3a", "#c07a34", "#c6b03f", "#5a9a5a", "#3f8a9a", "#5a7fc0"]
                                Rectangle {
                                    required property string modelData
                                    width: Math.floor((rampGrid.width - 6 * 2) / 7)
                                    height: 15
                                    radius: KP.radiusXS
                                    color: modelData
                                    border.width: 1
                                    border.color: KP.border
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Workspace.colors.foreground = modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: KP.gapS }
        }
    }

    // A form taller than the panel is cut at the panel's edge; the fade marks
    // the fold as "there is more below" instead of leaving a sliced line.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 14
        visible: scroller.contentHeight > scroller.height
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(KP.paneBg.r, KP.paneBg.g, KP.paneBg.b, 0.0) }
            GradientStop { position: 1.0; color: KP.paneBg }
        }
    }
}
