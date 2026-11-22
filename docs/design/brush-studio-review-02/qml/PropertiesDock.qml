import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import ReviewArt 1.0

Rectangle {
    id: root
    color: Theme.chrome
    Layout.preferredWidth: 294
    Layout.minimumWidth: 276
    Layout.fillHeight: true
    Rule { anchors.left: parent.left; height: parent.height; width: 1; color: Theme.seam }

    readonly property var chosen: studio.chosen
    readonly property bool container: chosen.kind === "brush" || chosen.kind === "group"
    readonly property bool grained: chosen.kind === "texture" || chosen.kind === "mask"
    signal advancedRequested()
    signal addRequested(string kind)
    signal moveRequested()
    signal groupRequested()
    signal tipRequested()
    signal importRequested(string kind)

    function set(key, value) { studio.edit(chosen.uid, key, value) }
    function openCopy() {
        copyName.text = studio.whole.name + " copy"
        copyDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        DockLabel {
            Layout.fillWidth: true
            text: "Brush Properties"
            Command {
                anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
                objectName: "brushMenu"; glyph: "more"; hint: "Brush menu"; quiet: true
                onClicked: brushMenu.open()
            }
        }
        C.ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            C.ScrollBar.vertical: ScrollRail { policy: C.ScrollBar.AlwaysOn }
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 78
                    Layout.margins: 9; Layout.bottomMargin: 5
                    color: "#252525"; border.color: "#4c4c4c"; clip: true
                    Artwork { anchors.fill: parent; anchors.margins: 1; kind: "current" }
                }
                LabelText {
                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                    text: studio.whole.name; font.pixelSize: 15; font.weight: Font.DemiBold
                }
                LabelText {
                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                    Layout.topMargin: 1
                    text: {
                        const c = studio.counts
                        return c.mark + (c.mark === 1 ? " mark" : " marks") + " · " +
                               c.group + (c.group === 1 ? " group" : " groups") + " · " +
                               c.texture + (c.texture === 1 ? " texture" : " textures") +
                               (c.mask ? " · " + c.mask + (c.mask === 1 ? " mask" : " masks") : "")
                    }
                    color: Theme.muted; font.pixelSize: Theme.small
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                    Layout.topMargin: 6; Layout.bottomMargin: 3
                    spacing: 4
                    Command { text: "Save Copy"; objectName: "saveCopy"; onClicked: copyDialog.open() }
                    Command { glyph: "undo"; hint: "Undo brush edit"; quiet: true; enabled: studio.canUndo; onClicked: studio.history("undo") }
                    Command { glyph: "redo"; hint: "Redo brush edit"; quiet: true; enabled: studio.canRedo; onClicked: studio.history("redo") }
                    Item { Layout.fillWidth: true }
                    LabelText {
                        text: studio.dirty ? "Edited" : "Saved"
                        color: Theme.muted; font.pixelSize: 9
                    }
                }
                Rule { Layout.fillWidth: true; Layout.topMargin: 3; Layout.bottomMargin: 2 }

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 35; color: Theme.stripe
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 6
                        LabelText {
                            Layout.fillWidth: true
                            text: root.chosen.root ? "Whole brush" : root.chosen.parent + " › " + root.chosen.name
                            color: Theme.mid; font.pixelSize: 10
                        }
                        Command { glyph: "up"; hint: "Move this part earlier"; quiet: true; enabled: root.chosen.up; onClicked: studio.act(root.chosen.uid, "up") }
                        Command { glyph: "down"; hint: "Move this part later"; quiet: true; enabled: root.chosen.down; onClicked: studio.act(root.chosen.uid, "down") }
                        Command {
                            glyph: root.chosen.visible ? "eye" : "hidden"; quiet: true
                            hint: root.chosen.visible ? "Hide this part" : "Show this part"
                            onClicked: root.set("visible", !root.chosen.visible)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                    Layout.topMargin: 4
                    spacing: 1
                    Setting {
                        objectName: "sizeSetting"; Layout.fillWidth: true
                        caption: "Size"; setting: "size"; unit: "%"; minimum: 5; maximum: 300
                        amount: root.chosen.size
                        onEdited: next => root.set("size", next)
                    }
                    Setting {
                        objectName: "opacitySetting"; Layout.fillWidth: true
                        caption: "Opacity"; setting: "opacity"; amount: root.chosen.opacity
                        onEdited: next => root.set("opacity", next)
                    }
                    Setting {
                        objectName: "flowSetting"; Layout.fillWidth: true
                        caption: "Flow"; setting: "flow"; amount: root.chosen.flow
                        onEdited: next => root.set("flow", next)
                    }
                    Setting {
                        objectName: "spacingSetting"; Layout.fillWidth: true
                        caption: "Spacing"; setting: "spacing"; amount: root.chosen.spacing
                        onEdited: next => root.set("spacing", next)
                    }
                    Setting {
                        Layout.fillWidth: true
                        caption: "Scatter"; setting: "scatter"; minimum: 0; maximum: 150
                        amount: root.chosen.scatter
                        onEdited: next => root.set("scatter", next)
                    }
                    Setting {
                        Layout.fillWidth: true
                        caption: "Angle"; setting: "angle"; unit: "°"; minimum: -180; maximum: 180
                        amount: root.chosen.angle
                        onEdited: next => root.set("angle", next)
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.topMargin: 3; Layout.preferredHeight: 28
                        LabelText {
                            text: root.container ? "Selection" : root.grained ? "Anchor" : "Rotation"
                            Layout.preferredWidth: 107
                            visible: root.container || root.grained
                        }
                        Choice {
                            objectName: "selectionPolicy"
                            Layout.fillWidth: true; Layout.preferredHeight: 25
                            visible: root.container
                            model: ["Paint Together", "In Order", "Random Selection", "Weighted Selection"]
                            currentIndex: Math.max(0, model.indexOf(root.chosen.mode))
                            onActivated: root.set("mode", currentText)
                        }
                        Choice {
                            Layout.fillWidth: true; Layout.preferredHeight: 25
                            visible: root.grained
                            model: ["Paper", "Stroke"]
                            currentIndex: Math.max(0, model.indexOf(root.chosen.anchor))
                            onActivated: root.set("anchor", currentText)
                        }
                        CheckRow {
                            Layout.fillWidth: true
                            visible: !root.container && !root.grained
                            text: "Follows stroke direction"
                            checked: root.chosen.follow
                            onToggled: root.set("follow", checked)
                        }
                    }
                }

                Rule { Layout.fillWidth: true; Layout.topMargin: 5; Layout.bottomMargin: 3 }
                LabelText {
                    Layout.leftMargin: 10; Layout.topMargin: 2
                    text: "BUILD"; color: Theme.muted; font.pixelSize: 9
                    font.weight: Font.DemiBold; font.letterSpacing: .8
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 6; Layout.rightMargin: 6
                    Layout.topMargin: 1; spacing: 0
                    BuildRow {
                        objectName: "addMark"; Layout.fillWidth: true
                        text: "Add a Mark"; glyph: "draw"; onClicked: root.addRequested("mark")
                    }
                    BuildRow {
                        objectName: "addBrush"; Layout.fillWidth: true
                        text: "Add Another Brush"; glyph: "copy"; onClicked: root.addRequested("brush")
                    }
                    BuildRow {
                        objectName: "addTexture"; Layout.fillWidth: true
                        text: "Add Texture"; glyph: "texture"; onClicked: root.addRequested("texture")
                    }
                    BuildRow {
                        objectName: "addGroup"; Layout.fillWidth: true
                        text: "Group of Marks"; glyph: "group"; onClicked: root.groupRequested()
                    }
                    BuildRow {
                        objectName: "changeOrder"; Layout.fillWidth: true
                        text: "Change Order"; glyph: "adjust"; onClicked: root.moveRequested()
                    }
                }
                Rule { Layout.fillWidth: true; Layout.topMargin: 5; Layout.bottomMargin: 3 }
                LabelText {
                    Layout.leftMargin: 10; text: "VARIATION"; color: Theme.muted; font.pixelSize: 9
                    font.weight: Font.DemiBold; font.letterSpacing: .8
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 8
                    Layout.topMargin: 1; spacing: 1
                    Setting {
                        Layout.fillWidth: true; caption: "Variation"; setting: "sizeVar"
                        amount: root.chosen.sizeVar
                        onEdited: next => root.set("sizeVar", next)
                    }
                    Setting {
                        Layout.fillWidth: true; caption: "Angle variation"; setting: "angleVar"; unit: "°"; minimum: 0; maximum: 180
                        amount: root.chosen.angleVar
                        onEdited: next => root.set("angleVar", next)
                    }
                    Setting {
                        Layout.fillWidth: true; caption: "Density spread"; setting: "opacityVar"; captionWidth: 110; amount: root.chosen.opacityVar
                        onEdited: next => root.set("opacityVar", next)
                    }
                }
                Command {
                    objectName: "saveGroup"
                    Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                    Layout.bottomMargin: 8
                    text: "Reuse This Group"; glyph: "copy"; quiet: true
                    visible: root.chosen.kind === "group"
                    onClicked: studio.keepGroup(root.chosen.uid, root.chosen.name)
                }
                Item { Layout.fillHeight: true; Layout.minimumHeight: 6 }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 43
            color: Theme.panel
            Rule { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.seam }
            Command {
                objectName: "advancedSetup"
                anchors.fill: parent; anchors.margins: 7
                text: "Advanced Brush Setup"; glyph: "layers"; strong: true
                onClicked: root.advancedRequested()
            }
        }
    }

    StudioMenu {
        id: brushMenu
        MenuRow { text: "Save Copy…"; glyph: "copy"; onTriggered: copyDialog.open() }
        MenuRow { text: "Duplicate Brush"; glyph: "copy"; onTriggered: studio.duplicateBrush() }
        MenuRow { text: "New Brush"; glyph: "plus"; onTriggered: studio.newBrush() }
        C.MenuSeparator { contentItem: Rule { implicitHeight: 1; implicitWidth: 180; color: Theme.border } }
        MenuRow { text: "Draw a Tip…"; glyph: "draw"; onTriggered: root.tipRequested() }
        MenuRow { text: "Import a Mark…"; glyph: "import"; onTriggered: root.importRequested("mark") }
        MenuRow { text: "Import a Texture…"; glyph: "import"; onTriggered: root.importRequested("texture") }
        C.MenuSeparator { contentItem: Rule { implicitHeight: 1; implicitWidth: 180; color: Theme.border } }
        MenuRow {
            text: "Remove “" + root.chosen.name + "”"; glyph: "trash"
            enabled: !root.chosen.root
            onTriggered: studio.act(root.chosen.uid, "remove")
        }
    }

    C.Dialog {
        id: copyDialog
        title: "Save a copy"
        modal: true
        onAboutToShow: {
            const pw = parent && parent.width > 20 ? parent.width : Window.width
            const ph = parent && parent.height > 20 ? parent.height : Window.height
            width = Math.min(330, pw - 60)
            height = 186
            x = Math.max(0, (pw - width) / 2)
            y = Math.max(0, (ph - height) / 2)
        }
        x: Math.max(0, (Window.width - width) / 2)
    y: Math.max(0, (Window.height - height) / 2)
        width: 330
        padding: 14
        background: Rectangle { color: Theme.panel; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 8
            LabelText { text: "The copy appears under Brushes in This Session. The original brush is not changed."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            EditField {
                id: copyName; Layout.fillWidth: true
                text: studio.whole.name + " copy"; placeholderText: "Brush name"
                Accessible.name: "Copy name"
                onAccepted: saveButton.clicked()
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 4
                Item { Layout.fillWidth: true }
                Command { text: "Cancel"; onClicked: copyDialog.close() }
                Command {
                    id: saveButton; objectName: "saveCopyConfirm"; text: "Save Copy"; strong: true
                    onClicked: { studio.saveCopy(copyName.text); copyDialog.close() }
                }
            }
        }
        onOpened: { copyName.forceActiveFocus(); copyName.selectAll() }
    }
}
