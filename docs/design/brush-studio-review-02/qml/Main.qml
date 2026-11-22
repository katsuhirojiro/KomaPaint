import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import QtQuick.Dialogs
import ReviewArt 1.0

C.ApplicationWindow {
    id: window
    objectName: "brushStudioWindow"
    visible: true
    width: 1400
    height: 900
    minimumWidth: 1000
    minimumHeight: 700
    title: "KomaPaint — Brush Studio (Second Design Review)"
    color: Theme.chrome
    property bool advancedOpen: false
    property bool modalOpen: addPicker.visible || tipRoom.visible || importDialog.visible || about.visible
    property string importKind: "mark"
    property var pickKind: "mark"

    function pickerText(kind) {
        if (kind === "mark") return ["Add a Mark", "Every mark is an image — a drawn tip, a scanned shape, or one from this library. Add as many as you need."]
        if (kind === "texture") return ["Add Texture", "A texture is another image. It can follow the paper or be carried by the stroke, and it applies only inside the group that holds it."]
        if (kind === "mask") return ["Add a Mask", "A mask subtracts paint inside its group. Use it to break edges, carve holes, or make a shape breathe."]
        if (kind === "brush") return ["Add Another Brush", "Bring a live copy of any brush into this one. Its marks, groups and textures stay editable here."]
        if (kind === "move") return ["Change Order", "Choose a group or the whole brush to move this part into. The arrows in the tree reorder neighbours."]
        return ["Choose", ""]
    }
    function openPicker(kind) {
        const copy = pickerText(kind)
        addPicker.kind = kind
        addPicker.heading = copy[0]
        addPicker.explanation = copy[1]
        addPicker.target = studio.addTo.uid
        addPicker.replace = false
        addPicker.open()
    }

    Shortcut { sequence: "Ctrl+Z"; onActivated: studio.history("undo") }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: studio.history("redo") }
    Shortcut { sequence: "Ctrl+Y"; onActivated: studio.history("redo") }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: window.advancedOpen = !window.advancedOpen }
    Shortcut { sequence: "Ctrl+S"; onActivated: propertiesDock.openCopy() }

    header: Rectangle {
        id: header
        height: 31
        color: Theme.chrome
        Rule { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.seam }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9
            spacing: 4
            Glyph { name: "logo"; width: 18; height: 18; tint: Theme.ink; Layout.rightMargin: 3 }
            LabelText { text: "KomaPaint"; font.pixelSize: 12; font.weight: Font.DemiBold; color: Theme.mid; Layout.rightMargin: 2 }
            C.MenuBar {
                id: menuBar
                Layout.preferredHeight: 23
                spacing: 0
                padding: 0
                background: Rectangle { color: "transparent" }
                delegate: C.MenuBarItem {
                    id: barItem
                    implicitHeight: 23
                    leftPadding: 9; rightPadding: 9
                    contentItem: LabelText {
                        text: barItem.text
                        color: barItem.highlighted || barItem.down ? Theme.text : Theme.mid
                        font.pixelSize: Theme.normal
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: barItem.highlighted || barItem.down ? Theme.hover : "transparent"
                        radius: 2
                    }
                }
                StudioMenu {
                    title: "File"
                    MenuRow { text: "Save Copy…"; glyph: "copy"; onTriggered: propertiesDock.openCopy() }
                    MenuRow { text: "New Brush"; glyph: "plus"; onTriggered: studio.newBrush() }
                    MenuRow { text: "Duplicate Brush"; glyph: "copy"; onTriggered: studio.duplicateBrush() }
                    C.MenuSeparator { contentItem: Rule { implicitHeight: 1; implicitWidth: 200; color: Theme.border } }
                    MenuRow { text: "Draw a Tip…"; glyph: "draw"; onTriggered: tipRoom.open() }
                    MenuRow { text: "Import a Mark…"; glyph: "import"; onTriggered: { window.importKind = "mark"; importDialog.open() } }
                    MenuRow { text: "Import a Texture…"; glyph: "import"; onTriggered: { window.importKind = "texture"; importDialog.open() } }
                    C.MenuSeparator { contentItem: Rule { implicitHeight: 1; implicitWidth: 200; color: Theme.border } }
                    MenuRow { text: "Close"; glyph: "close"; onTriggered: window.close() }
                }
                StudioMenu {
                    title: "Edit"
                    MenuRow { text: "Undo Brush Edit"; glyph: "undo"; enabled: studio.canUndo; onTriggered: studio.history("undo") }
                    MenuRow { text: "Redo Brush Edit"; glyph: "redo"; enabled: studio.canRedo; onTriggered: studio.history("redo") }
                    C.MenuSeparator { contentItem: Rule { implicitHeight: 1; implicitWidth: 200; color: Theme.border } }
                    MenuRow { text: "Duplicate Part"; glyph: "copy"; onTriggered: studio.act(studio.chosen.uid, "duplicate") }
                    MenuRow { text: "Remove Part"; glyph: "trash"; enabled: !studio.chosen.root; onTriggered: studio.act(studio.chosen.uid, "remove") }
                }
                StudioMenu {
                    title: "Brush"
                    MenuRow { text: "Add a Mark"; glyph: "draw"; onTriggered: window.openPicker("mark") }
                    MenuRow { text: "Add Another Brush"; glyph: "copy"; onTriggered: window.openPicker("brush") }
                    MenuRow { text: "Add Texture"; glyph: "texture"; onTriggered: window.openPicker("texture") }
                    MenuRow { text: "Add Mask"; glyph: "mask"; onTriggered: window.openPicker("mask") }
                    MenuRow { text: "Group of Marks"; glyph: "group"; onTriggered: studio.newGroup("Group of Marks", studio.addTo.uid) }
                    C.MenuSeparator { contentItem: Rule { implicitHeight: 1; implicitWidth: 200; color: Theme.border } }
                    MenuRow { text: "Change Order…"; glyph: "adjust"; onTriggered: window.openPicker("move") }
                    MenuRow { text: "Advanced Brush Setup"; glyph: "layers"; onTriggered: window.advancedOpen = true }
                }
                StudioMenu {
                    title: "Help"
                    MenuRow { text: "About This Review…"; glyph: "paper"; onTriggered: about.open() }
                }
            }
            Rule { Layout.preferredHeight: 14; Layout.preferredWidth: 1; Layout.leftMargin: 6; Layout.rightMargin: 4; color: Theme.border }
            Command { text: "Brush Studio"; tab: true; selected: true; onClicked: {} }
            LabelText {
                text: "Second design review"; color: Theme.muted; font.pixelSize: Theme.small
                Layout.leftMargin: 5
            }
            Item { Layout.fillWidth: true }
            LabelText {
                text: studio.notice
                color: Theme.mid; font.pixelSize: Theme.small
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                Layout.maximumWidth: 360
            }
            Rectangle { width: 1; height: 14; color: Theme.border }
            LabelText { text: "Isolated mockup · not production"; color: Theme.muted; font.pixelSize: 10 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 0
            BrushDock { visible: !window.advancedOpen || window.width >= 1150 }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 38
                    color: Theme.panel
                    Rule { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.seam }
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 7
                        spacing: 5
                        Column {
                            spacing: 0
                            LabelText {
                                text: studio.whole.name
                                font.pixelSize: Theme.label; font.weight: Font.DemiBold
                            }
                            LabelText {
                                text: studio.chosen.root ? "Whole brush selected" : studio.chosen.name + " selected"
                                color: Theme.muted; font.pixelSize: 9
                            }
                        }
                        Item { width: 7 }
                        Command { glyph: "undo"; hint: "Undo brush edit"; quiet: true; enabled: studio.canUndo; onClicked: studio.history("undo") }
                        Command { glyph: "redo"; hint: "Redo brush edit"; quiet: true; enabled: studio.canRedo; onClicked: studio.history("redo") }
                        Item { Layout.fillWidth: true }
                        Command {
                            objectName: "toolbarAdd"; text: "Add"; glyph: "plus"; drop: true
                            onClicked: quickAdd.open()
                        }
                        Command {
                            objectName: "toolbarAdvanced"; text: "Advanced Brush Setup"; glyph: "layers"; strong: !window.advancedOpen
                            onClicked: window.advancedOpen = !window.advancedOpen
                        }
                    }
                }
                DrawingPad {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 220
                }
                AdvancedSetup {
                    id: advanced
                    Layout.fillWidth: true
                    Layout.preferredHeight: 310
                    visible: window.advancedOpen && !window.modalOpen
                    onCloseRequested: window.advancedOpen = false
                    onAddWanted: kind => window.openPicker(kind)
                    onMoveWanted: window.openPicker("move")
                    onTipWanted: tipRoom.open()
                    onImportWanted: kind => { window.importKind = kind; importDialog.open() }
                }
            }
            PropertiesDock {
                id: propertiesDock
                visible: !window.advancedOpen
                onAdvancedRequested: window.advancedOpen = true
                onAddRequested: kind => window.openPicker(kind)
                onGroupRequested: studio.newGroup("Group of Marks", studio.addTo.uid)
                onMoveRequested: window.openPicker("move")
                onTipRequested: tipRoom.open()
                onImportRequested: kind => { window.importKind = kind; importDialog.open() }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 22
            color: Theme.chrome
            Rule { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.seam }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                spacing: 8
                LabelText {
                    Layout.fillWidth: true
                    text: studio.notice.length ? studio.notice : "Draw to test the selected brush. Each edit keeps your strokes."
                    color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight
                }
                LabelText { text: "No imposed part count"; color: Theme.muted; font.pixelSize: 9; visible: window.width > 1100 }
                Rule { Layout.preferredHeight: 11; Layout.preferredWidth: 1; color: Theme.border; visible: window.width > 1100 }
                LabelText { text: "In memory · review build"; color: Theme.muted; font.pixelSize: 9; visible: window.width > 1100 }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#59000000"
        visible: window.modalOpen
        z: 500000
    }

    StudioMenu {
        id: quickAdd
        MenuRow { text: "Add a Mark"; glyph: "draw"; onTriggered: window.openPicker("mark") }
        MenuRow { text: "Add Another Brush"; glyph: "copy"; onTriggered: window.openPicker("brush") }
        MenuRow { text: "Add Texture"; glyph: "texture"; onTriggered: window.openPicker("texture") }
        MenuRow { text: "Add Mask"; glyph: "mask"; onTriggered: window.openPicker("mask") }
        MenuRow { text: "Group of Marks"; glyph: "group"; onTriggered: studio.newGroup("Group of Marks", studio.addTo.uid) }
    }
    Picker {
        id: addPicker
        objectName: "addPicker"
        onImportWanted: kind => { window.importKind = kind; importDialog.open() }
        onTipWanted: tipRoom.open()
    }
    TipRoom { id: tipRoom }
    FileDialog {
        id: importDialog
        title: window.importKind === "texture" ? "Import a texture image" : "Import a mark image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)", "All files (*)"]
        onAccepted: studio.importImage(selectedFile, window.importKind, studio.addTo.uid, "")
    }

    C.Dialog {
        id: about
        title: "About this review"
        modal: true
        onAboutToShow: {
            const pw = parent && parent.width > 20 ? parent.width : Window.width
            const ph = parent && parent.height > 20 ? parent.height : Window.height
            width = Math.min(540, pw - 60)
            height = Math.min(430, ph - 50)
            x = Math.max(0, (pw - width) / 2)
            y = Math.max(0, (ph - height) / 2)
        }
        x: Math.max(0, (Window.width - width) / 2)
    y: Math.max(0, (Window.height - height) / 2)
        width: 520
        padding: 0
        background: Rectangle { color: Theme.panel; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 32; color: Theme.chrome
                LabelText { anchors.left: parent.left; anchors.margins: 11; anchors.verticalCenter: parent.verticalCenter; text: "Brush Studio — Second Design Review"; font.weight: Font.DemiBold }
                Command { anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter; glyph: "close"; quiet: true; hint: "Close"; onClicked: about.close() }
            }
            ColumnLayout {
                Layout.margins: 15; spacing: 9
                LabelText { text: "An isolated usability mockup, not the production brush engine or a final design."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                LabelText {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.mid
                    text: "• Composition is the feature: marks, brushes, groups, textures, masks and responses all use the same tree.\n• The cloud brush is one arrangement, not a brush type. Nothing here is specific to clouds.\n• Advanced Brush Setup exposes the structure in the same editor; the drawing pad never leaves.\n• Images are read from this computer and stay in this session. No files are written."
                    lineHeight: 1.25
                }
                Rule { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border; Layout.topMargin: 3 }
                LabelText { text: "FIRST ITERATION"; color: Theme.muted; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: .8 }
                LabelText {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.mid; font.pixelSize: Theme.small
                    text: "https://github.com/katsuhirojiro/KomaPaint/releases/tag/brush-studio-windows-44999457194-1"
                }
                LabelText { text: "Keep both builds side by side when comparing. This window reads no production files."; color: Theme.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 3
                    Item { Layout.fillWidth: true }
                    Command { text: "Close"; strong: true; onClicked: about.close() }
                }
            }
        }
    }
}
