import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import ReviewArt 1.0

Rectangle {
    id: root
    objectName: "advancedPanel"
    color: Theme.chrome
    signal closeRequested()
    signal addWanted(string kind)
    signal moveWanted()
    signal tipWanted()
    signal importWanted(string kind)
    property int tab: 0
    property string responseKey: "Pressure:Size"
    property string responseInput: "Pressure"
    property string responseOutput: "Size"
    readonly property var chosen: studio.chosen
    readonly property var rows: studio.rows
    readonly property var curveMap: chosen.curves ? chosen.curves : ({})
    function add(kind) {
        if (kind === "group") studio.newGroup("Group of Marks", studio.addTo.uid)
        else root.addWanted(kind)
    }
    function levels() {
        const values = root.curveMap[root.responseKey]
        return values && values.length === 5 ? values : [0, .25, .5, .75, 1]
    }
    function hasCurve() { return !!root.curveMap[root.responseKey] }
    Connections {
        target: studio
        function onSelectionChanged() {
            if (typeof partName !== "undefined") partName.text = studio.chosen.name
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 33; color: Theme.panel
            Rule { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
            Rule { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.seam }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 11; anchors.rightMargin: 12
                spacing: 7
                LabelText { text: "Advanced Brush Setup"; font.weight: Font.DemiBold; font.pixelSize: Theme.label }
                LabelText {
                    readonly property var c: studio.counts
                    text: c.brush + (c.brush === 1 ? " brush · " : " brushes · ") +
                          c.group + (c.group === 1 ? " group · " : " groups · ") +
                          c.mark + (c.mark === 1 ? " mark · " : " marks · ") +
                          c.texture + (c.texture === 1 ? " texture" : " textures") +
                          (c.mask ? " · " + c.mask + (c.mask === 1 ? " mask" : " masks") : "")
                    color: Theme.muted; font.pixelSize: Theme.small; Layout.fillWidth: true
                }
                Command { objectName: "advancedClose"; glyph: "close"; hint: "Close advanced setup"; quiet: true; onClicked: root.closeRequested() }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0

            ColumnLayout {
                Layout.preferredWidth: root.width < 1100 ? 286 : 342
                Layout.minimumWidth: 276
                Layout.fillHeight: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 31
                    Layout.leftMargin: 8; Layout.rightMargin: 5
                    spacing: 3
                    LabelText {
                        text: "BRUSH ANATOMY"; color: Theme.muted
                        font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: .8
                        Layout.fillWidth: true
                    }
                    Command {
                        objectName: "advancedAdd"; text: "Add"; glyph: "plus"; drop: true
                        onClicked: addMenu.open()
                    }
                    Command {
                        objectName: "partMenuButton"; glyph: "more"; hint: "Part actions"; quiet: true
                        onClicked: partMenu.open()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "#313131"; border.color: Theme.seam
                    ListView {
                        id: tree
                        objectName: "partTree"
                        anchors.fill: parent; anchors.margins: 1
                        model: root.rows
                        clip: true
                        C.ScrollBar.vertical: ScrollRail {}
                        currentIndex: -1
                        function revealSelected() {
                            for (let i = 0; i < count; ++i) {
                                const row = model[i]
                                if (row.uid === studio.chosen.uid) { positionViewAtIndex(i, ListView.Contain); return }
                            }
                        }
                        Component.onCompleted: revealSelected()
                        Connections {
                            target: studio
                            function onSelectionChanged() { tree.revealSelected() }
                        }
                        delegate: Rectangle {
                            id: line
                            required property var modelData
                            required property int index
                            objectName: "partRow" + index
                            property string partName: modelData.name
                            property string partKind: modelData.kind
                            property string partUid: modelData.uid
                            width: tree.width
                            height: 27
                            color: studio.chosen.uid === modelData.uid ? Theme.itemSelected
                                   : lineMouse.containsMouse ? Theme.hover : "transparent"
                            opacity: modelData.inheritedHidden ? .45 : 1
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4 + modelData.depth * 15
                                anchors.rightMargin: 4
                                spacing: 3
                                Item {
                                    Layout.preferredWidth: 15; Layout.preferredHeight: 20
                                    Glyph {
                                        anchors.centerIn: parent
                                        name: modelData.closed ? "right" : "down"
                                        width: 13; height: 13; tint: Theme.mid
                                        visible: modelData.count > 0
                                    }
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: studio.expand(modelData.uid)
                                    }
                                }
                                Glyph {
                                    Layout.preferredWidth: 15
                                    name: modelData.kind === "mark" ? "draw" : modelData.kind === "texture" ? "texture"
                                          : modelData.kind === "mask" ? "mask" : modelData.kind === "brush" ? "layers" : "group"
                                    width: 15; height: 15; tint: modelData.kind === "mask" ? Theme.muted : Theme.mid
                                }
                                LabelText {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    font.pixelSize: Theme.small
                                    font.weight: studio.chosen.uid === modelData.uid ? Font.DemiBold : Font.Normal
                                }
                                LabelText {
                                    text: modelData.kind === "brush" ? "BRUSH" : modelData.kind === "group" ? "GROUP"
                                          : modelData.kind === "texture" ? "TEXTURE" : modelData.kind === "mask" ? "MASK" : ""
                                    color: Theme.muted; font.pixelSize: 8; visible: modelData.kind !== "mark"
                                }
                                LabelText {
                                    text: modelData.count > 1 ? String(modelData.count) : ""
                                    color: Theme.muted; font.pixelSize: 9
                                    Layout.preferredWidth: 15; horizontalAlignment: Text.AlignRight
                                }
                                Command {
                                    Layout.preferredWidth: 23; Layout.preferredHeight: 22
                                    glyph: modelData.visible ? "eye" : "hidden"; quiet: true
                                    opacity: modelData.visible ? .8 : .5
                                    hint: modelData.visible ? "Hide part" : "Show part"
                                    onClicked: studio.edit(modelData.uid, "visible", !modelData.visible)
                                }
                            }
                            MouseArea {
                                id: lineMouse
                                anchors.fill: parent
                                anchors.rightMargin: 27
                                hoverEnabled: true
                                onClicked: studio.select(modelData.uid)
                                onDoubleClicked: {
                                    if (modelData.count > 0) studio.expand(modelData.uid)
                                }
                            }
                        }
                    }
                }
                LabelText {
                    Layout.fillWidth: true; Layout.leftMargin: 9; Layout.rightMargin: 9
                    Layout.topMargin: 4; Layout.bottomMargin: 4
                    text: "Groups can hold groups. No part count is imposed."
                    color: Theme.muted; font.pixelSize: 9; elide: Text.ElideRight
                }
            }
            Rule { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.seam }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 32
                    Layout.leftMargin: 4
                    spacing: 0
                    Command { objectName: "tabPart"; text: "This Part"; tab: true; selected: root.tab === 0; onClicked: root.tab = 0 }
                    Command { objectName: "tabReuse"; text: "Reuse"; tab: true; selected: root.tab === 1; onClicked: root.tab = 1 }
                    Command { objectName: "tabResponse"; text: "Response"; tab: true; selected: root.tab === 2; onClicked: root.tab = 2 }
                    Item { Layout.fillWidth: true }
                    LabelText {
                        Layout.rightMargin: 9; Layout.maximumWidth: 190
                        text: studio.chosen.root ? "Whole brush" : studio.chosen.parent + " › " + studio.chosen.name
                        color: Theme.muted; font.pixelSize: Theme.small; elide: Text.ElideLeft
                    }
                }
                Rule { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.seam }
                StackLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    currentIndex: root.tab

                    C.ScrollView {
                        id: partPane
                        clip: true
                        C.ScrollBar.vertical: ScrollRail { policy: C.ScrollBar.AlwaysOn }
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: partPane.availableWidth
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                Layout.topMargin: 5
                                spacing: 6
                                LabelText {
                                    text: root.chosen.kind === "mark" ? "Mark" : root.chosen.kind === "brush" ? "Whole brush"
                                          : root.chosen.kind === "group" ? "Group" : root.chosen.kind === "texture" ? "Texture" : "Mask"
                                    color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 63
                                }
                                EditField {
                                    id: partName
                                    objectName: "thisPartName"
                                    Layout.preferredWidth: 260; Layout.fillWidth: false; Layout.maximumWidth: 320
                                    text: studio.chosen.name
                                    Accessible.name: "Part name"
                                    onEditingFinished: studio.edit(studio.chosen.uid, "name", text)
                                }
                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                visible: root.chosen.kind === "mark" || root.chosen.kind === "texture" || root.chosen.kind === "mask"
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                spacing: 5
                                LabelText {
                                    text: root.chosen.kind === "mark" ? "Mark source" : "Grain source"
                                    Layout.preferredWidth: 63; color: Theme.muted; font.pixelSize: 10
                                }
                                ListView {
                                    id: sourceStrip
                                    objectName: "sourceStrip"
                                    Layout.fillWidth: true; Layout.preferredHeight: 49
                                    orientation: ListView.Horizontal
                                    spacing: 4; clip: true
                                    model: studio.options(root.chosen.kind, root.chosen.uid)
                                    delegate: Rectangle {
                                        id: sourceCell
                                        required property var modelData
                                        width: 48; height: 46
                                        color: root.chosen.asset === modelData.key ? Theme.itemSelected : "#252525"
                                        border.color: root.chosen.asset === modelData.key ? Theme.ink : "#4c4c4c"
                                        Image {
                                            anchors.fill: parent; anchors.margins: 2
                                            source: (root.chosen.kind === "mark" || root.chosen.kind === "texture" || root.chosen.kind === "mask") ? "image://brushes/" + (root.chosen.kind === "mark" ? "pale/" : "grain/") + modelData.key : ""
                                            fillMode: Image.PreserveAspectFit
                                            sourceSize: Qt.size(100, 100)
                                        }
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: studio.use(root.chosen.kind, sourceCell.modelData.key, "", root.chosen.uid)
                                        }
                                    }
                                }
                                Command {
                                    objectName: "sourceMore"
                                    glyph: "more"; hint: "More sources, draw or import"; quiet: true
                                    onClicked: sourcePicker.open()
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 5
                                Layout.topMargin: 1
                                columns: Math.max(2, Math.min(3, Math.floor((root.width - 350) / 265)))
                                columnSpacing: 8; rowSpacing: 0
                                visible: root.chosen.kind !== "texture" && root.chosen.kind !== "mask"
                                Setting {
                                    Layout.fillWidth: true; caption: "Size"; setting: "size"; minimum: 5; maximum: 300
                                    captionWidth: 84
                                    amount: root.chosen.size; onEdited: next => studio.edit(root.chosen.uid, "size", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Opacity"; setting: "opacity"; captionWidth: 84
                                    amount: root.chosen.opacity; onEdited: next => studio.edit(root.chosen.uid, "opacity", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Flow"; setting: "flow"; captionWidth: 84
                                    amount: root.chosen.flow; onEdited: next => studio.edit(root.chosen.uid, "flow", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Variation"; setting: "sizeVar"; captionWidth: 84
                                    amount: root.chosen.sizeVar; onEdited: next => studio.edit(root.chosen.uid, "sizeVar", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Angle variation"; captionWidth: 84; setting: "angleVar"; unit: "°"; minimum: 0; maximum: 180
                                    amount: root.chosen.angleVar; onEdited: next => studio.edit(root.chosen.uid, "angleVar", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Density"; captionWidth: 84; setting: "opacityVar"
                                    amount: root.chosen.opacityVar; onEdited: next => studio.edit(root.chosen.uid, "opacityVar", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Aspect"; captionWidth: 84; setting: "aspect"; minimum: 10; maximum: 200
                                    amount: root.chosen.aspect; onEdited: next => studio.edit(root.chosen.uid, "aspect", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Offset"; captionWidth: 84; setting: "offset"; unit: "px"; minimum: -100; maximum: 100
                                    amount: root.chosen.offset; onEdited: next => studio.edit(root.chosen.uid, "offset", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Weight"; captionWidth: 84; setting: "weight"; unit: ""; minimum: 1; maximum: 20
                                    amount: root.chosen.weight; onEdited: next => studio.edit(root.chosen.uid, "weight", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Tone"; captionWidth: 84; setting: "tone"; unit: ""; minimum: -65; maximum: 65
                                    amount: root.chosen.tone; onEdited: next => studio.edit(root.chosen.uid, "tone", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Spacing"; captionWidth: 84; setting: "spacing"; amount: root.chosen.spacing
                                    onEdited: next => studio.edit(root.chosen.uid, "spacing", next)
                                }
                            }
                            GridLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 5
                                Layout.topMargin: 1
                                columns: Math.max(2, Math.min(3, Math.floor((root.width - 350) / 265)))
                                columnSpacing: 8; rowSpacing: 0
                                visible: root.chosen.kind === "texture" || root.chosen.kind === "mask"
                                Setting {
                                    Layout.fillWidth: true; caption: "Strength"; captionWidth: 84; setting: "strength"; amount: root.chosen.strength
                                    onEdited: next => studio.edit(root.chosen.uid, "strength", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Scale"; captionWidth: 84; setting: "scale"; minimum: 12; maximum: 300
                                    amount: root.chosen.scale; onEdited: next => studio.edit(root.chosen.uid, "scale", next)
                                }
                                Setting {
                                    Layout.fillWidth: true; caption: "Angle"; captionWidth: 84; setting: "angle"; unit: "°"; minimum: -180; maximum: 180
                                    amount: root.chosen.angle; onEdited: next => studio.edit(root.chosen.uid, "angle", next)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                spacing: 6
                                visible: root.chosen.kind === "brush" || root.chosen.kind === "group"
                                LabelText { text: "Selection"; color: Theme.mid; font.pixelSize: 10; Layout.preferredWidth: 56 }
                                Choice {
                                    Layout.preferredWidth: 154
                                    model: ["Paint Together", "In Order", "Random Selection", "Weighted Selection"]
                                    currentIndex: Math.max(0, model.indexOf(root.chosen.mode))
                                    onActivated: studio.edit(root.chosen.uid, "mode", currentText)
                                }
                                LabelText { text: "Blend"; color: Theme.mid; font.pixelSize: 10; Layout.leftMargin: 6 }
                                Choice {
                                    Layout.preferredWidth: 122
                                    model: ["Normal", "Multiply", "Screen", "Soft Light", "Cut Away"]
                                    currentIndex: Math.max(0, model.indexOf(root.chosen.blend))
                                    onActivated: studio.edit(root.chosen.uid, "blend", currentText)
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                spacing: 6
                                visible: root.chosen.kind === "texture" || root.chosen.kind === "mask"
                                LabelText { text: "Anchor"; color: Theme.mid; font.pixelSize: 10; Layout.preferredWidth: 56 }
                                Choice {
                                    Layout.preferredWidth: 112
                                    model: ["Paper", "Stroke"]
                                    currentIndex: Math.max(0, model.indexOf(root.chosen.anchor))
                                    onActivated: studio.edit(root.chosen.uid, "anchor", currentText)
                                }
                                CheckRow {
                                    text: "Invert grain"
                                    checked: root.chosen.invert
                                    onToggled: studio.edit(root.chosen.uid, "invert", checked)
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                spacing: 6
                                visible: root.chosen.kind === "mark"
                                CheckRow {
                                    text: "Rotation follows stroke direction"
                                    checked: root.chosen.follow
                                    onToggled: studio.edit(root.chosen.uid, "follow", checked)
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                spacing: 6
                                visible: root.chosen.kind === "brush" || root.chosen.kind === "group"
                                LabelText { text: "Contains"; color: Theme.muted; font.pixelSize: 10; Layout.preferredWidth: 52 }
                                ListView {
                                    Layout.fillWidth: true; Layout.preferredHeight: 25
                                    orientation: ListView.Horizontal
                                    spacing: 3; clip: true
                                    model: studio.childrenOf
                                    delegate: Command {
                                        required property var modelData
                                        height: 24; quiet: true
                                        text: modelData.name
                                        onClicked: studio.select(modelData.uid)
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                                Layout.topMargin: 2; Layout.bottomMargin: 6
                                spacing: 4
                                Command { text: "Duplicate Part"; quiet: true; onClicked: studio.act(root.chosen.uid, "duplicate") }
                                Command { text: "Wrap in Group"; quiet: true; visible: !root.chosen.root && root.width > 1100; onClicked: studio.act(root.chosen.uid, "wrap") }
                                Command { text: "Move Up"; quiet: true; visible: !root.chosen.root && root.width > 1100; enabled: root.chosen.up; onClicked: studio.act(root.chosen.uid, "up") }
                                Command { text: "Move Down"; quiet: true; visible: !root.chosen.root && root.width > 1100; enabled: root.chosen.down; onClicked: studio.act(root.chosen.uid, "down") }
                                Item { Layout.fillWidth: true }
                                Command { text: "Change Order…"; quiet: true; onClicked: root.moveWanted() }
                                Command { text: "Remove"; quiet: true; visible: !root.chosen.root; onClicked: studio.act(root.chosen.uid, "remove") }
                            }
                        }
                    }

                    C.ScrollView {
                        id: reusePane
                        clip: true
                        C.ScrollBar.vertical: ScrollRail { policy: C.ScrollBar.AlwaysOn }
                        contentWidth: availableWidth
                        GridLayout {
                            width: Math.max(0, reusePane.availableWidth - 12)
                            columns: root.width > 760 ? 2 : 1
                            columnSpacing: 10; rowSpacing: 6
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 336
                                Layout.fillHeight: true
                                Layout.margins: 7
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    LabelText { text: "SAVED GROUPS"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: .8; Layout.fillWidth: true }
                                    Command {
                                        objectName: "saveGroupReuse"
                                        text: "Save This Group"; enabled: root.chosen.kind === "group"
                                        onClicked: studio.keepGroup(root.chosen.uid, root.chosen.name)
                                    }
                                }
                                LabelText {
                                    Layout.fillWidth: true
                                    text: "A saved group keeps its order, selection and variation. Add it to any brush, or inside another group."
                                    color: Theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: studio.options("group", "")
                                    delegate: Rectangle {
                                        id: groupRow
                                        required property var modelData
                                        required property int index
                                        objectName: "savedGroup" + index
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 42
                                        color: groupHover.containsMouse ? Theme.hover : "#313131"
                                        RowLayout {
                                            anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 5
                                            spacing: 7
                                            Rectangle {
                                                Layout.preferredWidth: 52; Layout.preferredHeight: 33
                                                color: "#252525"; border.color: "#4c4c4c"
                                                Artwork { anchors.fill: parent; anchors.margins: 1; kind: "groupPreview"; source: groupRow.modelData.key }
                                            }
                                            Column {
                                                Layout.fillWidth: true
                                                LabelText { text: groupRow.modelData.name; width: parent.width; font.pixelSize: Theme.small }
                                                LabelText { text: groupRow.modelData.count + " parts"; color: Theme.muted; font.pixelSize: 9 }
                                            }
                                            Command {
                                                objectName: "addSavedGroup" + groupRow.index
                                                text: "Add"; quiet: true
                                                onClicked: studio.use("group", groupRow.modelData.key, studio.addTo.uid, "")
                                            }
                                        }
                                        MouseArea { id: groupHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                    }
                                }
                                LabelText {
                                    Layout.fillWidth: true
                                    visible: studio.options("group", "").length === 0
                                    text: "Save a group you have selected in the tree, and it will be available here for any brush."
                                    color: Theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap
                                }
                                Item { height: 3 }
                                Rule { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
                                LabelText { text: "MAKE A MARK"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: .8; Layout.topMargin: 3 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    BuildRow { objectName: "drawTipRow"; Layout.fillWidth: true; text: "Draw a Tip…"; glyph: "draw"; onClicked: root.tipWanted() }
                                    BuildRow { Layout.fillWidth: true; text: "Import a Mark…"; glyph: "import"; onClicked: root.importWanted("mark") }
                                    BuildRow { Layout.fillWidth: true; text: "Import a Texture…"; glyph: "import"; onClicked: root.importWanted("texture") }
                                }
                                LabelText { text: "Images stay on this computer"; color: Theme.muted; font.pixelSize: 9 }
                                Item { Layout.fillHeight: true }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: 7
                                spacing: 3
                                LabelText { text: "BRUSHES READY TO COMBINE"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: .8 }
                                LabelText {
                                    Layout.fillWidth: true
                                    text: "Adds a live copy of the whole brush. Every mark inside stays editable here."
                                    color: Theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: studio.brushes.filter(b => b.key !== studio.currentKey)
                                    delegate: Rectangle {
                                        id: brushRow
                                        required property var modelData
                                        required property int index
                                        objectName: "reuseBrush" + index
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 43
                                        color: brushHover.containsMouse ? Theme.hover : "#313131"
                                        RowLayout {
                                            anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 5
                                            spacing: 7
                                            Rectangle {
                                                Layout.preferredWidth: 52; Layout.preferredHeight: 34
                                                color: "#252525"; border.color: "#4c4c4c"
                                                Artwork { anchors.fill: parent; anchors.margins: 1; kind: "preview"; source: brushRow.modelData.key }
                                            }
                                            Column {
                                                Layout.fillWidth: true
                                                LabelText { text: brushRow.modelData.name; width: parent.width; font.pixelSize: Theme.small }
                                                LabelText { text: brushRow.modelData.section; color: Theme.muted; font.pixelSize: 9 }
                                            }
                                            Command {
                                                text: "Add"; quiet: true
                                                onClicked: studio.use("brush", brushRow.modelData.key, studio.addTo.uid, "")
                                            }
                                        }
                                        MouseArea { id: brushHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                                    }
                                }
                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Layout.margins: 8; spacing: 11
                        CurveEditor {
                            id: curve
                            objectName: "responseCurve"
                            Layout.preferredWidth: 306
                            Layout.fillHeight: true
                            input: root.responseInput
                            output: root.responseOutput
                            levels: root.levels()
                            onEdited: next => studio.curve(root.chosen.uid, root.responseKey, next)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true; spacing: 4
                                Choice {
                                    id: inputChoice
                                    Layout.preferredWidth: 102
                                    model: ["Pressure", "Tilt", "Speed"]
                                    currentIndex: Math.max(0, model.indexOf(root.responseInput))
                                    onActivated: { root.responseInput = currentText; root.responseKey = currentText + ":" + root.responseOutput }
                                }
                                LabelText { text: "→"; color: Theme.mid }
                                Choice {
                                    id: outputChoice
                                    Layout.preferredWidth: 102
                                    model: ["Size", "Opacity", "Scatter", "Rotation"]
                                    currentIndex: Math.max(0, model.indexOf(root.responseOutput))
                                    onActivated: { root.responseOutput = currentText; root.responseKey = root.responseInput + ":" + currentText }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Command {
                                    objectName: "addResponse"
                                    text: root.hasCurve() ? "Reset This Response" : "Add This Response"
                                    strong: !root.hasCurve()
                                    onClicked: studio.curve(root.chosen.uid, root.responseKey, [0, .25, .5, .75, 1])
                                }
                                Command {
                                    text: "Remove"; quiet: true; enabled: root.hasCurve()
                                    onClicked: studio.curve(root.chosen.uid, root.responseKey, [])
                                }
                            }
                            LabelText { text: "RESPONSES ON THIS PART"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: .8; Layout.topMargin: 2 }
                            Repeater {
                                model: Object.keys(root.curveMap)
                                delegate: RowLayout {
                                    id: responseRow
                                    required property string modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 23
                                    spacing: 4
                                    Command {
                                        Layout.fillWidth: true; leftAligned: true
                                        text: modelData.replace(":", " → ")
                                        selected: modelData === root.responseKey
                                        quiet: true
                                        onClicked: {
                                            root.responseInput = modelData.split(":")[0]
                                            root.responseOutput = modelData.split(":")[1]
                                            root.responseKey = modelData
                                        }
                                    }
                                    Command {
                                        glyph: "close"; hint: "Remove this response"; quiet: true
                                        onClicked: studio.curve(root.chosen.uid, modelData, [])
                                    }
                                }
                            }
                            LabelText {
                                Layout.fillWidth: true
                                visible: Object.keys(root.curveMap).length === 0
                                text: "No responses on this part yet. Add one, then pull the line. Every mark can respond differently."
                                color: Theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap
                            }
                            Item { Layout.fillHeight: true }
                            LabelText {
                                Layout.fillWidth: true
                                text: "A response is a curve, not a mode. The same part can follow pressure for size and tilt for scatter."
                                color: Theme.muted; font.pixelSize: 9; wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }

    StudioMenu {
        id: addMenu
        MenuRow { objectName: "menuAddMark"; text: "Add a Mark"; glyph: "draw"; onTriggered: root.add("mark") }
        MenuRow { objectName: "menuAddBrush"; text: "Add Another Brush"; glyph: "copy"; onTriggered: root.add("brush") }
        MenuRow { objectName: "menuAddTexture"; text: "Add Texture"; glyph: "texture"; onTriggered: root.add("texture") }
        MenuRow { objectName: "menuAddMask"; text: "Add Mask"; glyph: "mask"; onTriggered: root.add("mask") }
        MenuRow { objectName: "menuAddGroup"; text: "Group of Marks"; glyph: "group"; onTriggered: root.add("group") }
    }
    StudioMenu {
        id: partMenu
        MenuRow { text: "Duplicate Part"; glyph: "copy"; onTriggered: studio.act(root.chosen.uid, "duplicate") }
        MenuRow { text: "Wrap in a Group"; glyph: "group"; enabled: !root.chosen.root; onTriggered: studio.act(root.chosen.uid, "wrap") }
        MenuRow { text: "Move Up"; glyph: "up"; enabled: root.chosen.up; onTriggered: studio.act(root.chosen.uid, "up") }
        MenuRow { text: "Move Down"; glyph: "down"; enabled: root.chosen.down; onTriggered: studio.act(root.chosen.uid, "down") }
        MenuRow { text: "Change Order…"; glyph: "adjust"; onTriggered: root.moveWanted() }
        MenuRow { text: "Remove Part"; glyph: "trash"; enabled: !root.chosen.root; onTriggered: studio.act(root.chosen.uid, "remove") }
    }
    Picker {
        id: sourcePicker
        kind: root.chosen.kind
        heading: root.chosen.kind === "mark" ? "Change the Mark" : "Change the Grain"
        explanation: "Every source is just an image. Replace it here without changing the rest of the brush."
        target: studio.addTo.uid
    }
}
