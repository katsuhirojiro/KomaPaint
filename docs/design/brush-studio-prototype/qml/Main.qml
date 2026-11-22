import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: app
    objectName: "brushStudio"
    title: "KomaPaint · Brush Studio Design Prototype"
    width: 1440; height: 900
    minimumWidth: 430; minimumHeight: 540
    visible: true
    color: Desk.base
    font.family: Desk.type
    palette.highlight: Desk.active
    palette.highlightedText: Desk.text
    palette.text: Desk.text
    palette.base: Desk.raised
    palette.window: Desk.bar
    palette.windowText: Desk.text
    readonly property bool compact: width < 680
    property string importMode: "new"
    readonly property bool editing: studio.route === "editor"

    function openStarter(key, copy) {
        editor.prepare()
        studio.start(key, copy)
    }
    function showPicker(mode) { shelf.show(mode) }
    function openTip(isNew) { if (isNew) editor.prepare(); studio.beginTip(isNew) }
    function showRename(isBrush) { renameSheet.wholeBrush = isBrush; renameSheet.open() }
    function focusEditor(area) { editor.focusArea(area) }
    function showPersonal() { myControls.open() }
    function showHistory() { history.open() }
    function importImage(mode) {
        importMode = mode
        if (mode === "new") editor.prepare()
        if (studio.remoteReview) studio.requestUpload(mode)
        else imageFile.open()
    }

    header: Rectangle {
        height: app.compact ? 56 : 64
        color: Desk.bar
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Desk.softLine }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: app.compact ? 14 : 27; anchors.rightMargin: app.compact ? 12 : 25
            spacing: app.compact ? 7 : 12
            Rectangle {
                implicitWidth: 32; implicitHeight: 32; radius: 7; color: Desk.accent
                Glyph { name: "logo"; tint: "#352720"; anchors.centerIn: parent; width: 20; height: 20 }
            }
            Type { text: "KomaPaint"; font.pixelSize: app.compact ? 16 : 18; font.weight: Font.DemiBold; font.letterSpacing: -.38 }
            Rectangle { visible: !app.compact; implicitWidth: 1; implicitHeight: 21; color: Desk.line; Layout.leftMargin: 3; Layout.rightMargin: 3 }
            Type { visible: !app.compact; text: "Brush Studio"; color: Desk.muted; font.pixelSize: 13 }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: !app.compact || !app.editing
                implicitWidth: protoTag.implicitWidth + (app.compact ? 14 : 20); implicitHeight: 27; radius: 4
                color: "#2a2825"; border.color: Desk.line
                Tag { id: protoTag; anchors.centerIn: parent; text: app.compact ? "PROTOTYPE" : "DESIGN PROTOTYPE"; font.pixelSize: 9; color: "#dbc5b2"; font.letterSpacing: 1.15 }
            }
            DeskButton {
                objectName: "personalButton"
                visible: app.editing
                text: app.width >= 900 ? "My Controls" : ""; iconName: "pin"; accessible: "My Controls"; style: "quiet"; size: 40
                onClicked: app.showPersonal()
            }
            DeskButton {
                objectName: "guideButton"; iconName: "help"; text: app.width >= 760 ? "Review guide" : ""; accessible: "Review guide"; style: "quiet"
                onClicked: guide.open()
            }
        }
    }

    footer: Rectangle {
        height: app.compact ? 28 : 33
        color: Desk.bar
        Rectangle { width: parent.width; height: 1; color: Desk.softLine }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: app.compact ? 13 : 27; anchors.rightMargin: app.compact ? 13 : 27
            spacing: 8
            Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: Desk.green }
            Type { text: app.editing ? "Private Drawing Pad" : "Design review · session only"; color: Desk.muted; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Type { visible: !app.compact; text: app.editing ? "Test strokes never touch your artwork" : "KOMAPAINT  /  BRUSH WORKSHOP"; font.family: app.editing ? Desk.type : Desk.mono; font.letterSpacing: app.editing ? 0 : .8; color: Desk.subtle; font.pixelSize: 10 }
            Type { visible: app.compact; text: "QT DESIGN PROTOTYPE"; font.family: Desk.mono; color: Desk.subtle; font.pixelSize: 9 }
        }
    }

    Home { id: home; objectName: "homeView"; anchors.fill: parent; host: app; visible: studio.route === "home" }
    Editor { id: editor; objectName: "editorView"; anchors.fill: parent; host: app; visible: app.editing }
    TipRoom { id: tipRoom; objectName: "tipView"; anchors.fill: parent; host: app; visible: studio.route === "tip" }

    Picker {
        id: shelf
        onDrawRequested: app.openTip(false)
        onImportRequested: mode => app.importImage(mode)
        onAccepted: key => {
            switch (mode) {
            case "duplicate": app.openStarter(key, true); break
            case "brush": studio.addBrush(key); app.focusEditor("Selected"); break
            case "group": studio.addGroup(key); app.focusEditor("Selected"); break
            case "replaceTexture": case "replaceMark": studio.replaceAsset(key); break
            default: studio.addAsset(mode, key); app.focusEditor("Selected"); break
            }
        }
    }
    FileDialog {
        id: imageFile
        title: "Choose a tip or texture image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: { studio.importFile(selectedFile, app.importMode); if (studio.route === "editor") app.focusEditor("Selected") }
    }
    Connections {
        target: studio
        function onUploadFinished(ok) { if (ok) app.focusEditor("Selected") }
        function onNoteChanged() { toast.restart() }
        function onRouteChanged() { toast.stop() }
    }
    Timer { id: toast; interval: 3700 }
    Rectangle {
        visible: toast.running && studio.note.length > 0
        z: 10; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 12
        width: Math.min(parent.width - 30, toastLine.implicitWidth + 41); height: Math.max(42, toastLine.implicitHeight + 22); radius: 7
        color: Desk.hover; border.color: Desk.accentLine
        Type { id: toastLine; anchors.centerIn: parent; width: parent.width - 28; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: studio.note; font.pixelSize: 13 }
    }

    Sheet {
        id: guide
        heading: "Try the workshop"; eyebrow: "REVIEW GUIDE"; wantedHeight: 484; wantedWidth: 515
        description: "This is an interactive design prototype, not the shipping brush engine. Try changing one thing at a time."
        inner: Component {
            ScrollView {
                id: guideScroll
                clip: true; contentWidth: availableWidth; contentHeight: guideSteps.implicitHeight
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: DeskScrollBar {}
                ColumnLayout {
                    id: guideSteps; width: guideScroll.availableWidth - 13; spacing: 12
                    Section { Layout.fillWidth: true; ruled: false; heading: "01   A lighter pencil"; detail: "Start with Pencil. In Settings → Brush, lower Opacity without changing Size. Select Warm paper to try a different texture." }
                    Section { Layout.fillWidth: true; heading: "02   A brush with many marks"; detail: "Open Cloud study or add several marks to any draft. Compare In Order, Random and Weighted. Then select just one mark and turn it." }
                    Section { Layout.fillWidth: true; heading: "03   The same stroke"; detail: "Draw on the pad and undo it. Your brush edits stay put. Open Advanced → Dynamics and compare the same example before and after a curve change." }
                    DeskButton { Layout.alignment: Qt.AlignRight; text: "Back to the workshop"; style: "accent"; onClicked: guide.close() }
                }
            }
        }
    }
    Sheet {
        id: renameSheet
        property bool wholeBrush: true
        heading: wholeBrush ? "Name this brush" : "Name this entry"
        wantedHeight: 213; wantedWidth: 430
        inner: Component {
            ColumnLayout {
                Field {
                    id: renameField
                    Layout.fillWidth: true; text: renameSheet.wholeBrush ? studio.brush.name : (studio.entry.name || "")
                    maximumLength: 72; Accessible.name: "Name"; Component.onCompleted: { forceActiveFocus(); selectAll() }
                    onAccepted: setName.click()
                }
                DeskButton {
                    id: setName
                    Layout.alignment: Qt.AlignRight; style: "accent"; text: "Use name"; enabled: renameField.text.trim().length > 0
                    onClicked: {
                        if (renameSheet.wholeBrush) studio.edit("name", renameField.text)
                        else studio.editEntry("name", renameField.text)
                        renameSheet.close()
                    }
                }
            }
        }
    }
    Sheet {
        id: history
        heading: "Draft edits"; eyebrow: "BRUSH HISTORY"; wantedHeight: 252; wantedWidth: 410
        description: "Undo changes to the brush here. Test strokes and the Drawing Pad’s own history stay put."
        inner: Component {
            ColumnLayout {
                spacing: 8
                Type { Layout.fillWidth: true; color: Desk.muted; text: studio.draftEdits + (studio.draftEdits === 1 ? " brush edit" : " brush edits"); font.pixelSize: 12 }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    DeskButton { objectName: "draftUndo"; Layout.fillWidth: true; text: "Undo brush edit"; iconName: "undo"; enabled: studio.draftEdits > 0; onClicked: studio.stepDraft("Undo") }
                    DeskButton { objectName: "draftRedo"; Layout.fillWidth: true; text: "Redo brush edit"; iconName: "redo"; enabled: studio.draftRedo; onClicked: studio.stepDraft("Redo") }
                }
            }
        }
    }
    Sheet {
        id: myControls
        heading: "My Controls"; eyebrow: "PERSONAL DRAWING CONTROLS"; wantedHeight: 560
        description: "Pin your usual adjustments here. Values in this panel only affect your pad test, not the brush or its creator defaults."
        inner: Component { Personal { } }
    }
}
