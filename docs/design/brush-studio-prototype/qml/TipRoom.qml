import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

Item {
    id: room
    required property var host
    property bool two: width >= 865
    function useTip() { if (studio.tipCount > 0) { studio.acceptTip(tipName.text); room.host.focusEditor("Selected") } }
    onVisibleChanged: if (visible) tipName.text = ""

    ScrollView {
        id: tipScroll
        anchors.fill: parent; clip: true
        contentWidth: availableWidth; contentHeight: area.implicitHeight + 24
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: DeskScrollBar {}
        ColumnLayout {
            id: area
            width: Math.min(1192, tipScroll.width - (room.two ? 66 : 30)); x: (tipScroll.width-width)/2
            spacing: 12
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 15
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    DeskButton { text: "Back"; iconName: "back"; style: "quiet"; leftPadding: 0; onClicked: studio.cancelTip() }
                    Type { text: "Draw a Brush Tip"; Layout.fillWidth: true; font.pixelSize: room.two ? 35 : 26; font.letterSpacing: -.9; font.weight: Font.DemiBold; wrapMode: Text.WordWrap }
                }
                DeskButton { objectName: "useTip"; text: "Use as a mark"; rightIcon: "arrow"; style: "accent"; enabled: studio.tipCount > 0; size: 44; Layout.alignment: Qt.AlignBottom; onClicked: room.useTip() }
            }
            Type { text: "Draw the shape you want the brush to repeat. The paper won’t be part of the mark."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Desk.body; font.pixelSize: 14 }
            GridLayout {
                Layout.fillWidth: true; Layout.topMargin: 5
                columns: room.two ? 2 : 1; columnSpacing: 14; rowSpacing: 11
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredWidth: 660; Layout.minimumWidth: 0
                    Layout.preferredHeight: room.two ? Math.min(588, Math.max(350, room.height - 175)) : Math.min(430, Math.max(255, room.height * .55))
                    color: Desk.panel; radius: 9; border.color: Desk.line
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Type { text: "Tip surface"; Layout.leftMargin: 4; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Item { Layout.fillWidth: true }
                            DeskButton { objectName: "tipUndo"; iconName: "undo"; accessible: "Undo tip stroke"; style: "quiet"; enabled: studio.tipUndo; onClicked: studio.padAction("tip","Undo") }
                            DeskButton { objectName: "tipRedo"; iconName: "redo"; accessible: "Redo tip stroke"; style: "quiet"; enabled: studio.tipRedo; onClicked: studio.padAction("tip","Redo") }
                            DeskButton { objectName: "tipClear"; text: "Clear"; accessible: "Clear the tip"; style: "quiet"; enabled: studio.tipCount > 0; onClicked: studio.padAction("tip","Clear") }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: Desk.raised; radius: 6; border.color: Desk.line
                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 10, parent.height - 10); height: width; color: Desk.paper; radius: 5; clip: true
                                ArtSheet { anchors.fill: parent; kind: "tip" }
                                Tag { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 13; text: "DRAW HERE"; color: "#6b5e52"; visible: !studio.tipHasInk }
                                MouseArea {
                                    objectName: "tipSurface"; anchors.fill: parent; cursorShape: Qt.CrossCursor; preventStealing: true
                                    Accessible.name: "Freehand tip drawing surface"
                                    onPressed: mouse => studio.beginStroke("tip",mouse.x,mouse.y,width,height)
                                    onPositionChanged: mouse => { if (pressed) studio.appendPoint("tip",mouse.x,mouse.y,width,height) }
                                    onReleased: mouse => { studio.appendPoint("tip",mouse.x,mouse.y,width,height); studio.endStroke("tip") }
                                    onCanceled: studio.endStroke("tip")
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; Layout.preferredWidth: room.two ? 295 : 1; Layout.minimumWidth: 0
                    implicitHeight: info.implicitHeight + 28; color: Desk.panel; radius: 9; border.color: Desk.line
                    ColumnLayout {
                        id: info
                        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 14; spacing: 12
                        Section { Layout.fillWidth: true; heading: "Your imprint"; detail: "One mark, before the brush repeats it."; ruled: false }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 124; radius: 6; color: Desk.paper; clip: true
                            ArtSheet { anchors.fill: parent; kind: "tipPreview" }
                            Tag { anchors.centerIn: parent; visible: !studio.tipHasInk; text: "WAITING FOR INK"; color: "#6b5e52"; font.pixelSize: 9 }
                        }
                        Knob { objectName: "tipWidth"; keyName: "tipWidth"; Layout.fillWidth: true; label: "Drawing width"; value: studio.tipWidth; low: 2; high: 80; unit: "px"; onChanged: next => studio.setTipWidth(next) }
                        Section { Layout.fillWidth: true; heading: "Name the mark" }
                        Field { id: tipName; objectName: "tipName"; Layout.fillWidth: true; placeholderText: "Hand-drawn mark"; maximumLength: 72; Accessible.name: "Mark name"; onAccepted: room.useTip() }
                        Type { Layout.fillWidth: true; text: "The mark goes straight into the same Brush Studio editor."; color: Desk.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
