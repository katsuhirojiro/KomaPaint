import QtQuick

Canvas {
    id: root
    property string name: ""
    property color tint: Theme.mid
    property real lineWidth: 1.55
    width: 20; height: 20
    implicitWidth: 20; implicitHeight: 20
    onNameChanged: requestPaint()
    onTintChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset(); c.clearRect(0, 0, width, height)
        const s = Math.min(width, height) / 24
        c.translate((width - 24*s) / 2, (height - 24*s) / 2); c.scale(s, s)
        c.lineWidth = root.lineWidth; c.lineCap = "round"; c.lineJoin = "round"
        c.strokeStyle = tint; c.fillStyle = tint
        function line(points) {
            c.beginPath(); c.moveTo(points[0], points[1])
            for (let i=2;i<points.length;i+=2) c.lineTo(points[i],points[i+1])
            c.stroke()
        }
        function circle(x,y,r,filled=false) {
            c.beginPath(); c.arc(x,y,r,0,2*Math.PI,false); if (filled) c.fill(); else c.stroke()
        }
        function rect(x,y,w,h) { c.strokeRect(x,y,w,h) }
        switch (name) {
        case "logo":
            c.beginPath(); c.moveTo(4, 4); c.lineTo(8, 4); c.lineTo(8, 20); c.lineTo(4, 20); c.closePath(); c.fill()
            c.beginPath(); c.moveTo(18, 4); c.lineTo(13, 12); c.lineTo(20, 20); c.lineTo(15, 20); c.lineTo(9, 12); c.lineTo(14, 4); c.closePath(); c.fill(); break
        case "up": line([6,14,12,8,18,14]); break
        case "down": line([6,9,12,15,18,9]); break
        case "right": line([9,6,15,12,9,18]); break
        case "plus": line([12,5,12,19]); line([5,12,19,12]); break
        case "check": line([5,12,10,17,19,7]); break
        case "close": line([6,6,18,18]); line([18,6,6,18]); break
        case "more": [6,12,18].forEach(x=>circle(x,12,1.25,true)); break
        case "grip": [8.5,15.5].forEach(x=>[6,12,18].forEach(y=>circle(x,y,1.15,true))); break
        case "draw": line([4.5,19.5,6,14.7,16.8,4,20,7.2,9.2,18,4.5,19.5]); line([14.5,6.4,17.8,9.6]); break
        case "import": rect(3.5,4.5,17,15); circle(8.2,9,1.4); line([4,16,9,12,12,15,16,10.5,20,14.5]); break
        case "copy": rect(8,8,11.5,12); line([5,16,5,4,16,4,16,6]); break
        case "paper": line([5,3.5,14.5,3.5,19,8,19,20.5,5,20.5,5,3.5]); line([14,4,14,8.5,19,8.5]); line([8.2,13,15.8,13]); line([8.2,16.5,14,16.5]); break
        case "layers": line([3,10,12,4,21,10,12,16,3,10]); line([3,14,12,20,21,14]); break
        case "group": line([3,7,10,7,13,9.5,21,9.5,21,19,3,19,3,7]); break
        case "texture": rect(4.5,4.5,15,15); for(let i=7;i<19;i+=4){ line([i,6.8,i,17.2]); line([6.8,i,17.2,i]); } break
        case "mask": c.beginPath(); c.moveTo(12,4); c.lineTo(19,7); c.lineTo(19,12); c.quadraticCurveTo(18,17.5,12,20); c.quadraticCurveTo(6,17.5,5,12); c.lineTo(5,7); c.closePath(); c.stroke(); break
        case "adjust": [6,12,18].forEach((y,i)=>{const x=[8,15,11][i]; line([3,y,x-2.4,y]); line([x+2.4,y,21,y]); circle(x,y,2.3);}); break
        case "undo": case "redo":
            if (name==="redo") { c.translate(24,0); c.scale(-1,1) }
            line([10,5,5,10,10,15]); c.beginPath(); c.moveTo(5,10); c.lineTo(14,10); c.bezierCurveTo(21,10,21,18,15,18); c.stroke(); break
        case "trash": line([4,7,20,7]); line([8.5,7,8.5,4.3,15.5,4.3,15.5,7]); line([6,7,7,20,17,20,18,7]); line([10,10.5,10,16.5]); line([14,10.5,14,16.5]); break
        case "eye": case "hidden":
            c.beginPath(); c.moveTo(3,12); c.quadraticCurveTo(12,-1,21,12); c.quadraticCurveTo(12,25,3,12); c.stroke(); circle(12,12,2.7)
            if(name==="hidden") line([4,4,20,20]); break
        case "search": circle(10,10,5.5); line([14,14,19.5,19.5]); break
        case "zoom": circle(10,10,5.5); line([14,14,20,20]); break;
        default: break
        }
    }
}
