import QtQuick
import QtQuick.Controls.Basic as C
import "."

Item {
    id: root
    property string bindingId: ""
    property string settingKey: ""
    property bool compact: false
    property bool dragging: false
    property var localPoints: []
    property var bindingInfo: ({})
    signal resetRequested
    implicitHeight: compact ? 160 : 186
    clip: true

    function refresh(force) {
        if (!bindingId.length) bindingInfo = ({})
        else bindingInfo = studio.bindingById(bindingId)
        if (bindingInfo.points && (!dragging || force))
            localPoints = bindingInfo.points.map(function(p) { return {x: p.x, y: p.y} })
    }
    Component.onCompleted: refresh(true)
    onBindingIdChanged: refresh(true)
    Connections {
        target: studio
        function onBindingChanged() { if (!root.dragging) root.refresh(false) }
    }
    onLocalPointsChanged: graph.requestPaint()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: Theme.track
        border.width: 1
        border.color: Theme.hairline
    }

    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        Row {
            width: parent.width
            spacing: 6
            Text {
                text: "SIGNAL"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.letterSpacing: 0.8
                anchors.verticalCenter: parent.verticalCenter
            }
            SelectField {
                width: Math.min(126, parent.width - 56)
                height: 22
                options: root.bindingInfo.signalChoices || []
                current: root.bindingInfo.signal || ""
                onSelected: function(value) { studio.setBindingSignal(root.bindingId, value) }
            }
            Item { width: 2; height: 1 }
            Text {
                visible: root.bindingInfo.available === false
                text: "fallback " + (root.bindingInfo.fallback || "")
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Row {
            width: parent.width
            spacing: 6
            Text {
                text: "STRENGTH"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.letterSpacing: 0.8
                anchors.verticalCenter: parent.verticalCenter
            }
            MiniSlider {
                width: parent.width - 136
                value: root.bindingInfo.amount || 0
                onMoved: function(value) { studio.setBindingAmount(root.bindingId, value) }
            }
            Text {
                text: (root.bindingInfo.amount || 0) + "%"
                color: Theme.secondary
                font.family: Theme.ui
                font.pixelSize: Theme.micro
                font.features: {"tnum": 1}
                width: 32
                horizontalAlignment: Text.AlignRight
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Item {
            id: graphWrap
            width: parent.width
            height: root.compact ? 64 : 70
            Rectangle {
                anchors.fill: parent
                color: "#09090A"
                radius: 2
            }
            Canvas {
                id: graph
                anchors.fill: parent
                anchors.margins: 1
                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    ctx.clearRect(0, 0, w, h)
                    ctx.strokeStyle = "rgba(255,255,255,0.04)"
                    ctx.lineWidth = 1
                    for (var i = 1; i < 4; i++) {
                        ctx.beginPath(); ctx.moveTo(w * i / 4, 0); ctx.lineTo(w * i / 4, h); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(0, h * i / 4); ctx.lineTo(w, h * i / 4); ctx.stroke()
                    }
                    ctx.strokeStyle = "rgba(255,255,255,0.09)"
                    ctx.beginPath(); ctx.moveTo(0, h - 0.5); ctx.lineTo(w, h - 0.5); ctx.stroke()
                    if (root.localPoints.length < 2) return
                    ctx.strokeStyle = "#FFFFFF"
                    ctx.lineWidth = 1.5
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    for (var j = 0; j < root.localPoints.length; j++) {
                        var p = root.localPoints[j]
                        var x = p.x * w, y = (1 - p.y) * h
                        if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: {
                    var b = studio.bindingById(root.bindingId)
                    if (!b.points) return
                    var nx = Math.max(0.04, Math.min(0.96, mouse.x / width))
                    var ny = Math.max(0, Math.min(1, 1 - mouse.y / height))
                    studio.addCurvePoint(root.bindingId, nx, ny)
                    root.refresh(true)
                }
            }
            Repeater {
                model: root.localPoints
                Item {
                    id: handle
                    width: 12; height: 12
                    x: Math.max(0, Math.min(graphWrap.width - width, modelData.x * graphWrap.width - width / 2))
                    y: Math.max(0, Math.min(graphWrap.height - height, (1 - modelData.y) * graphWrap.height - height / 2))
                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: Theme.elevated
                        border.width: 2
                        border.color: Theme.text
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -5
                        cursorShape: Qt.SizeAllCursor
                        preventStealing: true
                        drag.target: handle
                        drag.minimumX: index > 0 ? root.localPoints[index - 1].x * graphWrap.width + 3 : 0
                        drag.maximumX: index < root.localPoints.length - 1
                            ? root.localPoints[index + 1].x * graphWrap.width - 3 : graphWrap.width - handle.width
                        drag.minimumY: 0
                        drag.maximumY: graphWrap.height - handle.height
                        onPressed: {
                            root.dragging = true
                            studio.beginEdit()
                        }
                        onPositionChanged: {
                            if (!pressed) return
                            var copy = root.localPoints.slice()
                            copy[index] = {
                                x: Math.max(0, Math.min(1, (handle.x + handle.width / 2) / graphWrap.width)),
                                y: Math.max(0, Math.min(1, 1 - (handle.y + handle.height / 2) / graphWrap.height))
                            }
                            root.localPoints = copy
                            graph.requestPaint()
                        }
                        onReleased: {
                            root.dragging = false
                            studio.endEdit()
                            var p = root.localPoints[index]
                            studio.setCurvePoint(root.bindingId, index, p.x, p.y)
                        }
                    }
                }
            }
        }

        Row {
            width: parent.width
            spacing: 6
            Text {
                text: "0"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
            }
            Text {
                text: root.bindingInfo.signal || "input"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
                width: parent.width - 46
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                text: "100"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
            }
        }
        Row {
            width: parent.width
            spacing: 2
            ActionButton { text: "Linear"; compact: true; onClicked: { studio.resetCurve(root.bindingId); root.refresh(true) } }
            Item { width: Math.max(0, parent.width - 176); height: 1 }
            ActionButton { text: "Per stamp"; compact: true; selected: root.bindingInfo.scope !== "Per stroke"; onClicked: studio.setBindingScope(root.bindingId, "Per stamp") }
            ActionButton { text: "Per stroke"; compact: true; selected: root.bindingInfo.scope === "Per stroke"; onClicked: studio.setBindingScope(root.bindingId, "Per stroke") }
        }
    }
}
