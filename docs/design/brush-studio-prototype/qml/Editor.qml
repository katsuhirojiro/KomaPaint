import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: editor
    required property var host
    property string section: "Brush"
    property bool advanced: false
    property string activeDock: "Marks"
    readonly property bool low: height < 635
    readonly property int gutter: width < 680 ? 12 : width < 1150 ? 19 : 26
    function prepare() {
        section = "Brush"; activeDock = "Settings"; advanced = false
        pad.before = false
        compactScroll.contentY = 0
        settings.fresh()
    }
    function focusArea(area) {
        section = area
        activeDock = area === "Marks" ? "Marks" : "Settings"
        revealTools()
    }
    function revealTools() {
        if (workspace.stacked && compactScroll.height < 505 && compactScroll.height > 0)
            compactScroll.contentY = Math.max(0, compactScroll.contentHeight - compactScroll.height)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: editor.gutter; anchors.rightMargin: editor.gutter; anchors.bottomMargin: editor.low ? 8 : 16
        spacing: editor.low ? 7 : 12
        RowLayout {
            Layout.fillWidth: true; Layout.topMargin: editor.low ? 7 : 14
            Layout.preferredHeight: editor.low ? 52 : 70
            spacing: editor.width < 650 ? 6 : 9
            ColumnLayout {
                Layout.fillWidth: true; spacing: 1
                DeskButton {
                    text: "Brush starters"; iconName: "back"; style: "quiet"; size: editor.low ? 33 : 40
                    objectName: "backToStarters"; textSize: 12; leftPadding: 0; rightPadding: 8
                    onClicked: studio.navigate("home")
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Type {
                        Layout.preferredWidth: Math.ceil(implicitWidth) + 9; Layout.minimumWidth: 0; Layout.fillWidth: true; Layout.maximumWidth: Math.ceil(implicitWidth) + 9
                        maximumLineCount: 1; elide: Text.ElideRight
                        text: studio.brush.name
                        font.pixelSize: editor.width < 670 ? 23 : editor.low ? 26 : 31
                        font.weight: Font.DemiBold; font.letterSpacing: -1
                    }
                    DeskButton {
                        iconName: "draw"; accessible: "Rename brush"; style: "quiet"; size: editor.low ? 33 : 39
                        onClicked: editor.host.showRename(true)
                    }
                    Rectangle {
                        visible: editor.width > 790
                        implicitWidth: draftTag.implicitWidth + 14; implicitHeight: 23; radius: 4; color: Desk.raised; border.color: Desk.line
                        Tag { id: draftTag; text: "DRAFT"; anchors.centerIn: parent; font.pixelSize: 9; font.letterSpacing: 1 }
                    }
                    Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                }
            }
            DeskButton {
                id: historyButton
                objectName: "draftHistoryButton"
                Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: editor.low ? 0 : 3
                text: editor.width >= 900 ? "Draft edits" : ""; iconName: "undo"; accessible: "Brush edit history"
                style: "quiet"; size: 42
                onClicked: editor.host.showHistory()
            }
            DeskButton {
                objectName: "advancedButton"
                Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: editor.low ? 0 : 3
                text: editor.advanced ? (editor.width < 590 ? "Basic" : "Back to Basic") : "Advanced"
                iconName: editor.width < 590 ? "" : "adjust"; picked: editor.advanced; style: "outline"; size: 42
                accessible: editor.advanced ? "Back to Basic" : "Advanced"
                onClicked: {
                    editor.advanced = !editor.advanced
                    if (editor.advanced) { editor.activeDock = "Settings"; editor.revealTools() }
                }
            }
            DeskButton {
                objectName: "keepCopy"
                Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: editor.low ? 0 : 3
                text: editor.width < 615 ? "Keep" : "Keep a copy"; iconName: editor.width < 615 ? "" : "check"
                accessible: "Keep a copy"; style: "accent"; size: 42
                onClicked: studio.saveCopy()
            }
        }
        Flickable {
            id: compactScroll
            objectName: "compactScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; contentWidth: width; contentHeight: workspace.height
            interactive: workspace.stacked && contentHeight > height
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: DeskScrollBar {}
            Item {
                id: workspace
                width: compactScroll.width - (stacked && compactScroll.contentHeight > compactScroll.height ? 11 : 0)
                height: stacked ? Math.max(compactScroll.height, toolsY + 306) : compactScroll.height
                readonly property bool wide: width >= 1102
                readonly property bool stacked: compactScroll.width < 744
                readonly property int separation: stacked ? 8 : 12
                readonly property int railWidth: wide ? 252 : stacked ? width : Math.min(306, Math.round(width * .40))
                readonly property int settingWidth: wide ? (width > 1270 ? 344 : 314) : railWidth
                readonly property int canvasHeight: stacked ? Math.max(editor.low ? 240 : 285, Math.round(compactScroll.height * .52)) : compactScroll.height
                readonly property int toolsY: stacked ? canvasHeight + separation + 45 + separation : 0
                readonly property int toolsH: Math.max(0, height - toolsY)
                readonly property int dock: stacked ? 0 : width - railWidth
                DrawingPad {
                    id: pad
                    x: workspace.wide ? workspace.railWidth + workspace.separation : 0; y: 0
                    width: workspace.stacked ? workspace.width : workspace.wide ? workspace.width - x - workspace.settingWidth - workspace.separation
                                               : workspace.width - workspace.railWidth - workspace.separation
                    height: workspace.canvasHeight
                }
                Segments {
                    visible: !workspace.wide
                    x: workspace.dock; y: workspace.stacked ? workspace.canvasHeight + workspace.separation : 0
                    width: workspace.railWidth; height: 45; buttonHeight: 39
                    objectName: "workspaceTabs"
                    readonly property bool canJump: workspace.stacked && compactScroll.contentHeight > compactScroll.height
                    choices: [...(canJump ? [{key:"Pad",label:"Pad",icon:"draw"}] : []),
                              {key:"Marks",label:"Brush Marks",icon:"layers"},{key:"Settings",label:"Settings",icon:"adjust"}]
                    current: canJump && compactScroll.contentY < 1 ? "Pad" : editor.activeDock
                    onChosen: key => {
                        if (key === "Pad") compactScroll.contentY = 0
                        else { editor.activeDock = key; editor.revealTools() }
                    }
                }
                MarkRail {
                    x: workspace.wide ? 0 : workspace.dock
                    y: workspace.wide || workspace.stacked ? workspace.toolsY : 53
                    width: workspace.railWidth; height: Math.max(0, workspace.height - y)
                    visible: workspace.wide || editor.activeDock === "Marks"
                    inTabs: !workspace.wide; narrowHeight: height < 335
                    onOpenPicker: mode => editor.host.showPicker(mode)
                    onSelectBrush: { studio.choose(""); editor.section = "Brush"; if (!workspace.wide) editor.activeDock = "Settings" }
                    onSelectEntry: uid => { studio.choose(uid); editor.section = "Selected"; if (!workspace.wide) editor.activeDock = "Settings" }
                    onRenameEntry: editor.host.showRename(false)
                    activeSection: editor.section; advanced: editor.advanced
                }
                Inspector {
                    id: settings
                    x: workspace.wide ? workspace.width - workspace.settingWidth : workspace.dock
                    y: workspace.wide || workspace.stacked ? workspace.toolsY : 53
                    width: workspace.settingWidth; height: Math.max(0, workspace.height - y)
                    visible: workspace.wide || editor.activeDock === "Settings"
                    inTabs: !workspace.wide
                    section: editor.section; advanced: editor.advanced
                    onSectionChangedByUser: next => editor.section = next
                    onOpenPicker: mode => editor.host.showPicker(mode)
                    onOpenPersonal: editor.host.showPersonal()
                    onRenameEntry: editor.host.showRename(false)
                }
            }
        }
    }
}
