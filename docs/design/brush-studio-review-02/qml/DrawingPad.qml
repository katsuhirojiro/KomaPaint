import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import ReviewArt 1.0

Rectangle {
    id: root
    color: Theme.desk
    readonly property var pad: studio.padOptions
    readonly property bool shortBar: width < 580
    readonly property var papers: ["Graphite sheet", "Sky study", "Stroke tests", "Blank paper"]
    property alias size: canvas.width

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            color: Theme.chrome; Layout.fillWidth: true; Layout.preferredHeight: 32
            Rule { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.seam }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8
                spacing: 4
                LabelText { text: "Drawing Pad"; font.weight: Font.DemiBold; font.pixelSize: Theme.label; Layout.fillWidth: true }
                Command { objectName: "undoStroke"; glyph: "undo"; hint: "Undo stroke"; quiet: true; enabled: root.pad.undo; onClicked: studio.inkAction("pad", "undo") }
                Command { objectName: "redoStroke"; glyph: "redo"; hint: "Redo stroke"; quiet: true; enabled: root.pad.redo; onClicked: studio.inkAction("pad", "redo") }
                Command { objectName: "clearPad"; text: "Clear"; hint: "Clear only your test strokes"; quiet: true; enabled: root.pad.count > 0; onClicked: studio.inkAction("pad", "clear") }
                Rule { Layout.preferredWidth: 1; Layout.preferredHeight: 16; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                Choice {
                    objectName: "paperChoice"
                    Layout.preferredWidth: root.shortBar ? 112 : 135; Layout.preferredHeight: 25
                    model: root.papers
                    currentIndex: root.papers.indexOf(root.pad.sheet)
                    onActivated: studio.padSet("sheet", currentText)
                }
            }
        }
        Rectangle {
            id: canvas
            objectName: "padCanvas"
            Layout.margins: root.shortBar ? 9 : 14; Layout.bottomMargin: 0
            Layout.fillWidth: true; Layout.fillHeight: true
            color: Theme.paper; border.color: Theme.paperEdge; clip: true
            Artwork { kind: "pad"; anchors.fill: parent; anchors.margins: 1 }
            MouseArea {
                id: surface
                objectName: "padSurface"
                anchors.fill: parent; anchors.margins: 1
                cursorShape: root.pad.original ? Qt.ArrowCursor : Qt.CrossCursor
                enabled: !root.pad.original
                Accessible.name: "Private drawing pad"
                onPressed: mouse => studio.beginLine("pad", mouse.x/width, mouse.y/height)
                onPositionChanged: mouse => { if (pressed) studio.point("pad", mouse.x/width, mouse.y/height) }
                onReleased: studio.endLine("pad")
                onCanceled: studio.endLine("pad")
            }
            LabelText {
                text: "Draw here to test your brush"
                color: "#777477"
                visible: root.pad.count === 0 && !surface.pressed && root.pad.sheet === "Blank paper"
                anchors.centerIn: parent
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 43
            Layout.leftMargin: root.shortBar ? 9 : 14; Layout.rightMargin: root.shortBar ? 9 : 14
            spacing: 6
            Command {
                objectName: "padColor"; text: root.shortBar ? "Ink" : "Test ink"; hint: "Ink for new test strokes"; drop: true
                leftPadding: 26; leftAligned: true
                Rectangle { x: 7; anchors.verticalCenter: parent.verticalCenter; width: 13; height: 13; radius: 2; color: root.pad.color; border.color: "#818181" }
                onClicked: inkDialog.open()
            }
            LabelText { text: "Size"; color: Theme.mid; Layout.leftMargin: 3; visible: !root.shortBar }
            EditField {
                objectName: "padSize"; Layout.preferredWidth: 39; Layout.maximumWidth: 44
                text: String(root.pad.size); horizontalAlignment: TextInput.AlignRight
                Accessible.name: "Test size in pixels"
                validator: IntValidator { bottom: 1; top: 180 }
                onEditingFinished: { if (text.length) studio.padSet("size", Number(text)); text = Qt.binding(() => String(root.pad.size)) }
            }
            LabelText { text: "px"; color: Theme.mid; font.pixelSize: Theme.small; Layout.rightMargin: 4 }
            LabelText {
                Layout.fillWidth: true
                text: root.pad.original ? "Original · read only" : "Test strokes update with edits"
                visible: !root.shortBar; color: Theme.mid; font.pixelSize: Theme.small
            }
            Item { Layout.fillWidth: true; visible: root.shortBar }
            Command { objectName: "draftCompare"; text: "Draft"; tab: true; selected: !root.pad.original; onClicked: studio.padSet("original", false) }
            Command { objectName: "originalCompare"; text: "Original"; tab: true; selected: root.pad.original; onClicked: studio.padSet("original", true) }
        }
    }
    ColorDialog {
        id: inkDialog
        title: "Drawing pad ink"
        selectedColor: root.pad.color
        onAccepted: studio.padSet("color", selectedColor.toString())
    }
}
