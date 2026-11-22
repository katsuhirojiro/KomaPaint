import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

ScrollView {
    id: home
    required property var host
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: DeskScrollBar {}
    readonly property int margin: width < 720 ? 21 : width < 1180 ? 34 : 56
    readonly property bool paired: width >= 1010
    readonly property int page: Math.min(1328, width - margin * 2)
    contentHeight: body.implicitHeight + 36
    ColumnLayout {
        id: body
        width: home.page
        x: Math.floor((home.availableWidth - width) / 2)
        spacing: 0
        Item { Layout.preferredHeight: home.width < 720 ? 25 : 34 }
        RowLayout {
            spacing: 10
            Rectangle { color: Desk.accent; implicitWidth: 22; implicitHeight: 1 }
            Tag { text: "THE BRUSH WORKSHOP"; color: Desk.accent; font.pixelSize: 11 }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.topMargin: 7; spacing: 8
            Type {
                text: "Brush Studio"
                font.pixelSize: home.width < 720 ? 41 : 55; font.weight: Font.DemiBold; font.letterSpacing: -2.8
            }
            Item { Layout.fillWidth: true }
            DeskButton {
                objectName: "resumeDraft"
                visible: studio.hasDraft
                text: home.width < 560 ? "Resume" : "Resume draft"; rightIcon: home.width < 560 ? "" : "arrow"
                size: 40; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 4
                onClicked: studio.navigate("editor")
            }
        }
        Type {
            Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: home.width < 900 ? 24 : 30
            text: "Start with a brush you know, or make a mark of your own."
            color: Desk.body; font.pixelSize: 16; wrapMode: Text.WordWrap
        }
        GridLayout {
            columns: home.paired ? 2 : 1
            columnSpacing: 24; rowSpacing: 22
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; Layout.preferredWidth: 2.13; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Type { text: "Start with a brush"; font.pixelSize: 21; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Tag { text: "01 / STARTERS"; color: Desk.subtle }
                }
                GridLayout {
                    Layout.fillWidth: true; columns: home.width < 510 ? 1 : 2; columnSpacing: 11; rowSpacing: 11
                    Repeater {
                        model: [
                            ["pencil", "Pencil", "Soft grain for sketching", "#eee4d4"],
                            ["pen", "Pen", "Clean, expressive lines", "#f6eddf"],
                            ["paint", "Paintbrush", "Textured, loaded strokes", "#efe2d3"],
                            ["airbrush", "Airbrush", "Soft edges and shading", "#e6e6e0"]
                        ]
                        delegate: StarterCard {
                            required property var modelData
                            objectName: modelData[0] + "Starter"
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                            Layout.preferredHeight: home.height >= 785 ? 132 : 116
                            key: modelData[0]; title: modelData[1]; caption: modelData[2]; cardPaper: modelData[3]
                            onClicked: home.host.openStarter(key, false)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 13
                    Type { text: "Make it yours"; font.pixelSize: 21; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Tag { text: "02 / BUILD"; color: Desk.subtle }
                }
                GridLayout {
                    columns: home.width < 540 ? 1 : 2; Layout.fillWidth: true; columnSpacing: 11; rowSpacing: 10
                    Repeater {
                        model: [
                            ["scratch", "Create from Scratch", "Begin with a paintable round", "round", ""],
                            ["duplicate", "Duplicate a Brush", "Change a copy, not the original", "bristle", "copy"],
                            ["tip", "Draw a Brush Tip", "Turn a drawing into a mark", "star", "draw"],
                            ["import", "Import an Image", "See it as a mark right away", "leaf", "import"]
                        ]
                        delegate: StarterCard {
                            required property var modelData
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                            objectName: modelData[0] + "Starter"
                            compact: true; key: modelData[0]; title: modelData[1]; caption: modelData[2]; artwork: modelData[3]; iconName: modelData[4]
                            onClicked: {
                                if (key === "scratch") home.host.openStarter("scratch", false)
                                else if (key === "duplicate") home.host.showPicker("duplicate")
                                else if (key === "tip") home.host.openTip(true)
                                else home.host.importImage("new")
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Type { text: "Try a field study"; font.pixelSize: 21; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Tag { text: "EXAMPLE"; color: Desk.subtle }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: studyInterior.implicitHeight + 2
                    color: Desk.panel; border.color: Desk.line; radius: 9
                    ColumnLayout {
                        id: studyInterior
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 1
                        spacing: 0
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: home.paired ? 220 : 209
                            radius: 8; clip: true; color: "#98aab7"
                            ArtSheet { anchors.fill: parent; kind: "sky"; sample: "cloud" }
                            Rectangle {
                                color: "#de252924"; radius: 4; height: 27; width: fieldLabel.implicitWidth + 20
                                anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 13
                                Tag { id: fieldLabel; anchors.centerIn: parent; text: "FIELD STUDY 01"; color: "#f9ecdf"; font.pixelSize: 9 }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.margins: 16; spacing: 8
                            Type { text: "Cloud study"; font.pixelSize: 25; font.weight: Font.DemiBold; font.letterSpacing: -.75 }
                            Type { Layout.fillWidth: true; text: "Three distinct marks and a little texture. Try a different order and see how the same stroke changes."; color: Desk.body; wrapMode: Text.WordWrap; lineHeight: 1.23; font.pixelSize: 13 }
                            DeskButton { objectName: "cloudStarter"; Layout.fillWidth: true; Layout.topMargin: 7; text: "Open cloud study"; rightIcon: "arrow"; style: "accent"; size: 43; onClicked: home.host.openStarter("cloud", false) }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.topMargin: 1
                    implicitHeight: workshop.implicitHeight + 26
                    radius: 8; color: "#222221"; border.color: Desk.line
                    RowLayout {
                        id: workshop
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 13; spacing: 10
                        Rectangle {
                            Layout.alignment: Qt.AlignTop; implicitWidth: 31; implicitHeight: 31; radius: 5; color: Desk.active
                            Glyph { name: "spark"; tint: Desk.accent; anchors.centerIn: parent; width: 18; height: 18 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Type { text: "A space to experiment"; font.weight: Font.DemiBold; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            Type { text: "Every starting point opens the same editor. Your test strokes live only in the Drawing Pad."; color: Desk.muted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap; lineHeight: 1.18 }
                        }
                    }
                }
            }
        }
    }
}
