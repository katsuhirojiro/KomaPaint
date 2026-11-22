import QtQuick

Rectangle {
    id: root
    property var values: [0, .25, .5, .75, 1]
    property string axis: "Pressure"
    property string target: "Size"
    property int selected: 2
    signal edited(var points)
    readonly property int inset_left: 32
    readonly property int inset_right: 16
    readonly property int inset_top: 28
    readonly property int inset_bottom: 34
    color: Desk.base; border.color: Desk.line; radius: 6
    implicitHeight: 162
    onValuesChanged: plot.requestPaint()
    onSelectedChanged: plot.requestPaint()
    function setLevel(i, val) {
        let levels = Array.from(values)
        levels[i] = Math.max(0, Math.min(1, val))
        edited(levels)
    }
    Tag { x: root.inset_left; y: 9; text: root.target.toUpperCase() + "  ↑"; font.pixelSize: 8; font.letterSpacing: .75 }
    Type { x: 8; y: root.inset_top - 6; text: "100"; font.pixelSize: 8; color: Desk.muted }
    Type { x: 16; y: root.height-root.inset_bottom-5; text: "0"; font.pixelSize: 8; color: Desk.muted }
    Tag { anchors.bottom: parent.bottom; anchors.bottomMargin: 10; x: root.inset_left; text: root.axis.toUpperCase() + "  →"; font.pixelSize: 8; font.letterSpacing: .7 }
    Tag { anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 12; text: "0—100"; font.pixelSize: 8; font.letterSpacing: .45 }
    Canvas {
        id: plot
        anchors.fill: parent
        onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
        onPaint: {
            const g = getContext("2d")
            g.reset(); g.clearRect(0,0,width,height)
            const x = root.inset_left, y = root.inset_top, w = width-root.inset_left-root.inset_right, h = height-root.inset_top-root.inset_bottom
            g.lineWidth = 1; g.strokeStyle = Desk.line
            for (let i=0; i<=4; ++i) {
                g.beginPath(); g.moveTo(x+i*w/4,y); g.lineTo(x+i*w/4,y+h); g.stroke()
                g.beginPath(); g.moveTo(x,y+i*h/4); g.lineTo(x+w,y+i*h/4); g.stroke()
            }
            g.beginPath(); g.moveTo(x,y+h)
            for (let i=0;i<5;++i) g.lineTo(x+i*w/4,y+h*(1-root.values[i]))
            g.lineTo(x+w,y+h); g.closePath(); g.fillStyle = "#624630"; g.fill()
            g.beginPath()
            for (let i=0;i<5;++i) {
                const px=x+i*w/4, py=y+h*(1-root.values[i]); if(i===0)g.moveTo(px,py); else g.lineTo(px,py)
            }
            g.strokeStyle = Desk.accent; g.lineWidth = 2; g.stroke()
            for(let i=0;i<5;++i) {
                g.beginPath(); g.arc(x+i*w/4,y+h*(1-root.values[i]),i===root.selected?5.2:3.8,0,Math.PI*2)
                g.fillStyle = i===root.selected ? Desk.text : Desk.base; g.fill(); g.stroke()
            }
        }
    }
    MouseArea {
        id: editArea
        objectName: "curveGraph"
        anchors.fill: parent; anchors.topMargin: 14; anchors.bottomMargin: 16
        preventStealing: true; cursorShape: Qt.SizeVerCursor
        activeFocusOnTab: true
        Accessible.name: root.axis + " to " + root.target + " curve. Left and right choose a point. Up and down change it."
        function level(y) { return 1 - (y + editArea.y - root.inset_top) / (root.height - root.inset_top - root.inset_bottom) }
        onPressed: mouse => {
            forceActiveFocus()
            root.selected = Math.max(0, Math.min(4, Math.round((mouse.x - root.inset_left) / (root.width - root.inset_left - root.inset_right) * 4)))
            root.setLevel(root.selected, level(mouse.y))
        }
        onPositionChanged: mouse => { if (pressed) root.setLevel(root.selected, level(mouse.y)) }
        Keys.onLeftPressed: root.selected = Math.max(0, root.selected-1)
        Keys.onRightPressed: root.selected = Math.min(4, root.selected+1)
        Keys.onUpPressed: root.setLevel(root.selected, root.values[root.selected]+.05)
        Keys.onDownPressed: root.setLevel(root.selected, root.values[root.selected]-.05)
    }
}
