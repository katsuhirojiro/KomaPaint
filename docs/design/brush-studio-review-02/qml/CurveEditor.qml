import QtQuick

Rectangle {
    id: root
    property var levels: [0, .25, .5, .75, 1]
    property int point: 2
    property string input: "Pressure"
    property string output: "Size"
    signal edited(var next)
    implicitHeight: 165
    color: Theme.field; border.color: Theme.border
    readonly property int plotLeft: 28
    readonly property int plotTop: 26
    readonly property int plotBottom: 29
    readonly property int plotRight: 13
    onLevelsChanged: graph.requestPaint()
    onPointChanged: graph.requestPaint()
    function setPoint(v) {
        const numbers = Array.from(levels)
        numbers[point] = Math.max(0, Math.min(1, v)); edited(numbers)
    }
    LabelText { x: 6; y: 3; text: root.output; color: Theme.mid; font.pixelSize: 9 }
    LabelText { x: 6; y: 16; text: "100"; color: Theme.mid; font.pixelSize: 9 }
    LabelText { x: 13; y: root.height-root.plotBottom-6; text: "0"; color: Theme.mid; font.pixelSize: 9 }
    LabelText { x: root.plotLeft; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; text: root.input + " →"; color: Theme.mid; font.pixelSize: Theme.small }
    Canvas {
        id: graph
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const g = getContext("2d"); g.reset(); g.clearRect(0,0,width,height)
            const x=root.plotLeft, y=root.plotTop, w=width-root.plotRight-x, h=height-root.plotBottom-y
            g.beginPath(); g.lineWidth = 1; g.strokeStyle = "#4d4d4d"
            for (let i=0; i<=4; i++) {
                g.moveTo(x+i*w/4,y); g.lineTo(x+i*w/4,y+h)
                g.moveTo(x,y+i*h/4); g.lineTo(x+w,y+i*h/4)
            }
            g.stroke()
            g.beginPath(); g.strokeStyle = Theme.ink; g.lineWidth = 1.5
            for(let i=0;i<5;i++) {
                const a=x+i*w/4,b=y+h*(1-root.levels[i]); if(i===0)g.moveTo(a,b);else g.lineTo(a,b)
            }
            g.stroke()
            for(let i=0;i<5;i++) {
                g.beginPath(); g.arc(x+i*w/4,y+h*(1-root.levels[i]),i===root.point ? 4.5 : 3.5,0,2*Math.PI)
                g.fillStyle=i===root.point ? Theme.ink : Theme.field; g.fill(); g.stroke()
            }
        }
    }
    MouseArea {
        id: surface
        objectName: "responseGraph"
        anchors.fill: parent
        activeFocusOnTab: true; preventStealing: true; cursorShape: Qt.SizeVerCursor
        Accessible.name: root.input + " to " + root.output + " curve. Arrow keys choose and adjust points."
        function level(y) { return 1 - (y-root.plotTop)/(height-root.plotTop-root.plotBottom) }
        onPressed: mouse => {
            forceActiveFocus()
            root.point=Math.min(4,Math.max(0,Math.round((mouse.x-root.plotLeft)/(width-root.plotLeft-root.plotRight)*4)))
            root.setPoint(level(mouse.y))
        }
        onPositionChanged: mouse => { if (pressed) root.setPoint(level(mouse.y)) }
        Keys.onLeftPressed: root.point=Math.max(0,root.point-1)
        Keys.onRightPressed: root.point=Math.min(4,root.point+1)
        Keys.onDownPressed: root.setPoint(root.levels[root.point]-.05)
        Keys.onUpPressed: root.setPoint(root.levels[root.point]+.05)
    }
    Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.focus; visible: surface.activeFocus }
}
