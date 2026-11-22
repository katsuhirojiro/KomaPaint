import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

Rectangle {
    id: pad
    property bool before: false
    readonly property bool small: width < 530
    readonly property bool lowHeight: height < 365
    color: Desk.panel; radius: 9; border.color: Desk.line; clip: true
    ColumnLayout {
        anchors.fill: parent; anchors.margins: pad.small ? 8 : 12
        spacing: pad.lowHeight ? 5 : 8
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Layout.leftMargin: pad.small ? 2 : 4
            Type { text: "Drawing Pad"; font.pixelSize: pad.small ? 15 : 17; font.weight: Font.DemiBold }
            Rectangle {
                visible: pad.width >= 470; implicitWidth: 52; implicitHeight: 22; radius: 4
                color: pad.before ? Desk.active : Desk.darkGreen
                Row {
                    anchors.centerIn: parent; spacing: 5
                    Rectangle { visible: !pad.before; anchors.verticalCenter: parent.verticalCenter; width: 5; height: 5; radius: 2.5; color: Desk.green }
                    Tag { text: pad.before ? "BEFORE" : "LIVE"; font.pixelSize: 9; font.letterSpacing: .7; color: pad.before ? Desk.accent : Desk.green }
                }
            }
            Type { visible: pad.lowHeight && pad.width >= 290; text: studio.strokeCount + (studio.strokeCount === 1 ? " stroke" : " strokes"); color: Desk.muted; font.pixelSize: 11; Layout.leftMargin: 3 }
            Item { Layout.fillWidth: true }
            DeskButton { objectName: "padUndo"; iconName: "undo"; accessible: "Undo test stroke"; enabled: studio.canUndo; style: "quiet"; size: pad.lowHeight ? 37 : 41; onClicked: studio.padAction("pad", "Undo") }
            DeskButton { objectName: "padRedo"; iconName: "redo"; accessible: "Redo test stroke"; enabled: studio.canRedo; style: "quiet"; size: pad.lowHeight ? 37 : 41; onClicked: studio.padAction("pad", "Redo") }
            DeskButton { objectName: "padClear"; iconName: pad.small ? "trash" : ""; text: pad.small ? "" : "Clear"; accessible: "Clear test strokes"; enabled: studio.strokeCount > 0; style: "quiet"; size: pad.lowHeight ? 37 : 41; onClicked: studio.padAction("pad", "Clear") }
        }
        Rectangle {
            id: paper
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 54
            color: Desk.paper; radius: 6; clip: true
            ArtSheet { anchors.fill: parent; kind: pad.before ? "padOriginal" : "pad" }
            Column {
                visible: !pad.lowHeight || studio.study === "Sky study"
                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: pad.small ? 11 : 16; spacing: 5
                Rectangle { width: 22; height: 1; color: studio.study === "Sky study" ? "#534c49" : "#978b79" }
                Tag {
                    text: pad.before ? "ORIGINAL BRUSH" : studio.study === "Pressure study" ? "PRESSURE STUDY  /  LIGHT → FIRM"
                          : studio.study === "Sky study" ? "SKY STUDY  /  LIVE BRUSH" : "BLANK PAPER"
                    color: "#51463b"; font.pixelSize: pad.small ? 8 : 9; font.letterSpacing: pad.small ? .8 : 1.2
                }
            }
            Row {
                visible: studio.strokeCount === 0 && !pad.lowHeight
                anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 15
                spacing: 5
                Glyph { name: "draw"; tint: studio.study === "Sky study" ? "#fff0dc" : "#584b42"; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter }
                Tag { text: "DRAW HERE"; color: studio.study === "Sky study" ? "#fff0dc" : "#584b42"; font.pixelSize: 9; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                id: surface
                objectName: "padSurface"
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                preventStealing: true
                Accessible.name: "Freehand drawing test surface"
                onPressed: mouse => { forceActiveFocus(); studio.beginStroke("pad", mouse.x, mouse.y, width, height) }
                onPositionChanged: mouse => { if (pressed) studio.appendPoint("pad", mouse.x, mouse.y, width, height) }
                onReleased: mouse => { studio.appendPoint("pad", mouse.x, mouse.y, width, height); studio.endStroke("pad") }
                onCanceled: studio.endStroke("pad")
                Shortcut { sequences: [StandardKey.Undo]; enabled: surface.activeFocus && pad.visible; onActivated: studio.padAction("pad", "Undo") }
                Shortcut { sequences: [StandardKey.Redo]; enabled: surface.activeFocus && pad.visible; onActivated: studio.padAction("pad", "Redo") }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: pad.small ? 4 : 7
            Tag { visible: pad.width > 640; text: "PAD TEST"; font.letterSpacing: 1; font.pixelSize: 9; Layout.leftMargin: 4; Layout.rightMargin: 4 }
            DeskButton {
                id: ink
                objectName: "padColor"
                text: pad.width < 405 ? "" : "Color"; dotColor: studio.padColor; accessible: "Test color"
                size: pad.lowHeight ? 37 : 41; textSize: 12
                onClicked: colors.open()
                Popup {
                    id: colors
                    parent: ink; x: 0; y: -height - 6; width: 254; padding: 11; focus: true
                    background: Rectangle { color: Desk.raised; radius: 8; border.color: Desk.line }
                    contentItem: ColumnLayout {
                        spacing: 7
                        Tag { text: "TEST COLOR"; color: Desk.accent }
                        GridLayout {
                            columns: 4; rowSpacing: 5; columnSpacing: 5
                            Repeater {
                                model: [["#44392f","Graphite"],["#26252a","Ink black"],["#ad573f","Sienna"],["#dd947e","Apricot"],["#516b88","Slate blue"],["#566b59","Moss"],["#e8d1ac","Sand"],["#f9f0e2","Cloud white"]]
                                delegate: DeskButton {
                                    required property var modelData
                                    size: 51; dotColor: modelData[0]; picked: studio.padColor === modelData[0]; accessible: modelData[1]; objectName: modelData[1].replace(" ", "") + "Ink"
                                    onClicked: { studio.testColor(modelData[0]); colors.close() }
                                }
                            }
                        }
                        Type { Layout.fillWidth: true; text: "For new strokes on this pad only."; color: Desk.muted; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    }
                }
            }
            DeskButton {
                id: sizeButton
                objectName: "padSize"
                text: (pad.width >= 505 ? "Size " : "") + studio.padSize + " px"; accessible: "Test size " + studio.padSize + " pixels"; size: pad.lowHeight ? 37 : 41; textSize: 12
                onClicked: sizes.open()
                Popup {
                    id: sizes
                    parent: sizeButton; x: -Math.min(20, sizeButton.x); y: -height - 6; width: 243; padding: 11; focus: true
                    background: Rectangle { color: Desk.raised; radius: 8; border.color: Desk.line }
                    contentItem: Knob { label: "Test size"; keyName: "testSize"; hint: "For new strokes on this pad only."; unit: "px"; low: 1; high: 160; value: studio.padSize; onChanged: next => studio.testSize(next) }
                }
            }
            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
            DeskButton {
                objectName: "comparePad"
                text: pad.width >= 357 ? (pad.before ? "Original" : "Compare") : ""; iconName: "layers"; accessible: pad.before ? "Show modified brush" : "Compare original brush"; picked: pad.before; style: "quiet"; size: pad.lowHeight ? 37 : 41; textSize: 12
                onClicked: pad.before = !pad.before
            }
            DeskButton {
                id: paperButton
                objectName: "choosePaper"
                iconName: studio.study === "Sky study" ? "cloud" : "paper"; text: pad.width > 560 ? studio.study : ""; rightIcon: pad.width > 560 ? "down" : ""; accessible: "Choose pad background. " + studio.study; size: pad.lowHeight ? 37 : 41; textSize: 12
                onClicked: papers.open()
                Flyout {
                    id: papers; parent: paperButton; x: paperButton.width - width; y: -implicitHeight - 6
                    options: [{key:"Blank paper",label:"Blank paper",icon:"paper"},{key:"Pressure study",label:"Pressure study",icon:"draw"},{key:"Sky study",label:"Sky study",icon:"cloud"}]
                    onChosen: key => studio.setStudy(key)
                }
            }
        }
        RowLayout {
            visible: !pad.lowHeight
            Layout.fillWidth: true; Layout.leftMargin: 4; Layout.rightMargin: 4; spacing: 4
            Tag { text: "TEST HISTORY"; font.pixelSize: 9; font.letterSpacing: .85 }
            Type { text: "·  " + studio.strokeCount + (studio.strokeCount === 1 ? " stroke" : " strokes"); color: Desk.muted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Type { visible: pad.width > 430; text: pad.before ? "Original brush on the same pad" : "Draw to test · edits replay live"; color: Desk.muted; font.pixelSize: 11 }
        }
    }
}
