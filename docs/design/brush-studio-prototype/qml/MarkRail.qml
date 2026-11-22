import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

Rectangle {
    id: rail
    property bool inTabs: false
    property bool narrowHeight: false
    property bool advanced: false
    property string activeSection: "Brush"
    signal openPicker(string mode)
    signal selectEntry(string uid)
    signal selectBrush()
    signal renameEntry()
    color: Desk.panel; radius: 9; border.color: Desk.line
    clip: true

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10
        spacing: rail.narrowHeight ? 5 : 9
        RowLayout {
            visible: !rail.inTabs
            Layout.fillWidth: true; Layout.margins: 4; Layout.bottomMargin: 0
            Type { text: "Brush Marks"; font.pixelSize: 17; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Glyph { name: "layers"; tint: Desk.subtle; width: 19; height: 19 }
        }
        Button {
            id: whole
            objectName: "wholeBrushRow"
            Layout.fillWidth: true; implicitHeight: rail.narrowHeight ? 53 : 94
            text: "Brush sample"; padding: 7; hoverEnabled: true
            background: Rectangle {
                radius: 6; color: whole.down || rail.activeSection === "Brush" ? Desk.active : whole.hovered ? Desk.hover : Desk.raised
                border.color: whole.visualFocus || rail.activeSection === "Brush" ? Desk.accentLine : Desk.line
            }
            contentItem: Item {
                Rectangle {
                    x: 0; y: 0; width: rail.narrowHeight ? 95 : parent.width; height: rail.narrowHeight ? parent.height : parent.height - 26
                    color: Desk.paper; radius: 4; clip: true
                    ArtSheet { anchors.fill: parent; kind: "sample" }
                }
                RowLayout {
                    x: rail.narrowHeight ? 102 : 4; y: rail.narrowHeight ? 0 : parent.height - 21
                    width: parent.width - x - 3; height: rail.narrowHeight ? parent.height : 21
                    spacing: 5
                    Type { Layout.fillWidth: true; text: "Brush sample"; font.weight: Font.DemiBold; font.pixelSize: 12; elide: Text.ElideRight }
                    Glyph { name: "right"; tint: rail.activeSection === "Brush" ? Desk.accent : Desk.muted; width: 15; height: 15 }
                }
            }
            onClicked: rail.selectBrush()
        }
        RowLayout {
            visible: !rail.narrowHeight
            Layout.fillWidth: true; Layout.leftMargin: 4; Layout.rightMargin: 4; Layout.topMargin: 3
            Tag { text: "MARKS & TEXTURES"; font.pixelSize: 9; font.letterSpacing: 1.1 }
            Item { Layout.fillWidth: true }
            Tag { text: studio.markCount + (studio.markCount === 1 ? " MARK" : " MARKS"); font.pixelSize: 9; font.letterSpacing: .6 }
        }
        ScrollView {
            id: scroller
            objectName: "markList"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 21
            contentWidth: availableWidth; contentHeight: rows.implicitHeight + 3; clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: DeskScrollBar {}
            ColumnLayout {
                id: rows
                width: scroller.availableWidth - 10; spacing: 5
                Repeater {
                    model: studio.rows
                    delegate: Item {
                        id: row
                        required property var modelData
                        property bool isContainer: modelData.kind === "group" || modelData.kind === "brush"
                        property bool picked: modelData.uid === studio.selected
                        Layout.fillWidth: true; implicitHeight: rail.narrowHeight ? 53 : 59
                        Layout.leftMargin: Math.min(6, modelData.depth) * 10
                        Rectangle {
                            width: 1; height: parent.height; x: -6
                            color: Desk.line; visible: row.modelData.depth > 0
                        }
                        Rectangle {
                            anchors.fill: parent; radius: 6; border.color: pick.visualFocus || row.picked ? Desk.accentLine : Desk.softLine
                            color: row.picked ? Desk.active : pick.hovered ? Desk.hover : Desk.raised
                        }
                        Button {
                            id: pick
                            objectName: "entry_" + row.modelData.asset
                            text: row.modelData.name; anchors.fill: parent
                            hoverEnabled: true; padding: 0; background: Item {}
                            contentItem: Item {}
                            onClicked: rail.selectEntry(row.modelData.uid)
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 2
                            spacing: row.isContainer ? 4 : 7
                            DeskButton {
                                visible: row.isContainer
                                accessible: row.modelData.expanded ? "Collapse " + row.modelData.name : "Expand " + row.modelData.name
                                objectName: "expand_" + row.modelData.uid
                                style: "quiet"; size: 37; Layout.preferredWidth: 34; Layout.minimumWidth: 31
                                iconName: row.modelData.expanded ? "down" : "right"; onClicked: studio.act(row.modelData.uid,"expand")
                            }
                            Item {
                                Layout.preferredWidth: row.isContainer ? 27 : 42; Layout.preferredHeight: row.isContainer ? 32 : 42
                                opacity: row.modelData.visible && !row.modelData.fromAbove ? 1 : .5
                                Well { visible: !row.isContainer; anchors.fill: parent; asset: row.modelData.asset; kind: row.modelData.kind }
                                Glyph { visible: row.isContainer; anchors.centerIn: parent; name: row.modelData.kind === "brush" ? "brush" : "group"; tint: Desk.accent }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                                opacity: row.modelData.visible && !row.modelData.fromAbove ? 1 : .62
                                Type { Layout.fillWidth: true; text: row.modelData.name; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                Tag {
                                    Layout.fillWidth: true
                                    text: row.modelData.kind.toUpperCase() + (row.isContainer ? " · " + row.modelData.childrenCount : "")
                                         + (row.modelData.visible && !row.modelData.fromAbove ? "" : " · HIDDEN")
                                    font.pixelSize: 8; font.letterSpacing: 1; elide: Text.ElideRight
                                }
                            }
                            DeskButton {
                                objectName: "visibility_" + row.modelData.asset
                                iconName: row.modelData.visible ? "eye" : "hidden"
                                accessible: (row.modelData.visible ? "Hide " : "Show ") + row.modelData.name
                                style: "quiet"; size: 42; Layout.preferredWidth: rail.width < 231 ? 35 : 40; Layout.minimumWidth: 34
                                onClicked: studio.act(row.modelData.uid, "hide")
                            }
                        }
                    }
                }
                Type { visible: studio.rows.length === 0; Layout.fillWidth: true; text: "Add a mark to put paint on the pad."; color: Desk.muted; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.margins: 8 }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            Layout.preferredHeight: rail.narrowHeight ? 37 : 42
            DeskButton { objectName: "moveEntryUp"; iconName: "up"; accessible: "Move selected entry up"; size: rail.narrowHeight ? 37 : 42; style: "quiet"; enabled: !!rail.currentRow && rail.currentRow.canUp; onClicked: studio.act(studio.selected, "up") }
            DeskButton { objectName: "moveEntryDown"; iconName: "down"; accessible: "Move selected entry down"; size: rail.narrowHeight ? 37 : 42; style: "quiet"; enabled: !!rail.currentRow && rail.currentRow.canDown; onClicked: studio.act(studio.selected, "down") }
            Item { Layout.fillWidth: true }
            DeskButton {
                id: editButton
                objectName: "entryActions"
                iconName: "more"; text: "Edit entry"; accessible: "Edit entry"; style: "quiet"; size: rail.narrowHeight ? 37 : 42; enabled: !!rail.currentRow
                onClicked: entryMenu.open()
                Flyout {
                    id: entryMenu
                    parent: editButton
                    x: Math.max(-160, editButton.width - width); y: -implicitHeight - 5
                    options: [
                        {key:"rename",label:"Rename",icon:"draw"},
                        {key:"duplicate",label:"Duplicate entry",icon:"copy"},
                        {key:"group",label:"Group this entry",icon:"group"},
                        {key:"move",label:"Move into group…",icon:"layers",enabled: studio.groups.some(e=>e.uid!==studio.selected)},
                        {key:"out",label:"Move out of group",icon:"back",enabled: !!studio.entry.parent},
                        {key:"reuse",label:"Keep for reuse",icon:"check",enabled: studio.entry.kind==="group" || studio.entry.kind==="brush"},
                        {key:"remove",label:"Remove entry",icon:"trash"}
                    ]
                    onChosen: key => {
                        if (key === "move") movePicker.open()
                        else if (key === "rename") rail.renameEntry()
                        else studio.act(studio.selected, key)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 5
            DeskButton {
                objectName: "addMarkButton"
                Layout.fillWidth: true; Layout.minimumWidth: 0; text: "Add a Mark"; iconName: "plus"; size: rail.narrowHeight ? 39 : 43; style: "accent"
                onClicked: rail.openPicker("mark")
            }
            DeskButton {
                id: plusMenu
                objectName: "addEntryButton"
                iconName: "down"; accessible: "More ways to add to the brush"; size: rail.narrowHeight ? 39 : 43
                onClicked: addMenu.open()
                Flyout {
                    id: addMenu; parent: plusMenu; width: 236; x: plusMenu.width - width; y: -implicitHeight - 5
                    options: [
                        {key:"mark",label:"Add a Mark",icon:"plus"},
                        {key:"brush",label:"Add Another Brush",icon:"brush"},
                        {key:"texture",label:"Add Texture",icon:"texture"},
                        {key:"group",label:"Group of Marks",icon:"group"},
                        ...(rail.advanced ? [{key:"mask",label:"Add a Mask",icon:"mask"}] : [])
                    ]
                    onChosen: key => rail.openPicker(key)
                }
            }
        }
    }
    readonly property var currentRow: studio.rows.find(row=>row.uid===studio.selected)
    Sheet {
        id: movePicker
        heading: "Move into group"; eyebrow: "BRUSH MARKS"; wantedHeight: 370
        description: "Choose where the selected entry should go. A group cannot move inside itself."
        inner: Component {
            ScrollView {
                id: moveScroll; contentWidth: availableWidth; contentHeight: moveList.implicitHeight; clip: true
                ColumnLayout {
                    id: moveList; width: moveScroll.availableWidth - 3
                    Repeater {
                        model: studio.groups.filter(row=>row.uid!==studio.selected)
                        delegate: DeskButton {
                            required property var modelData
                            Layout.fillWidth: true; iconName: modelData.kind === "brush" ? "brush" : "group"; text: modelData.name
                            onClicked: { studio.drop(studio.selected, modelData.uid, true); movePicker.close() }
                        }
                    }
                }
            }
        }
    }
}
