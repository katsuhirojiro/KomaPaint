import QtQuick
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

Rectangle {
    id: shelf
    required property BrushStudioModel draft
    required property bool expanded
    required property int expandedHeight
    property var tabletSettings: null
    property color ink: "black"
    property string padNotice: ""
    signal toggleRequested
    implicitHeight: expanded ? expandedHeight : KP.panelHeaderH
    color: KP.toolbarBg
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        StudioHeader {
            objectName: "brushStudioPreviewHeader"
            Layout.fillWidth: true
            title: qsTr("Live Preview")
            actions: [
                Text {
                    visible: shelf.expanded && shelf.width >= 820
                    anchors.verticalCenter: parent.verticalCenter
                    text: pad.renderIssue.length ? qsTr("Draft cannot paint") : qsTr("Private test pad · Digital")
                    color: KP.textMuted
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                },
                KpButton {
                    objectName: "brushStudioPadUndo"
                    visible: shelf.expanded
                    small: true
                    glyph: "undo"
                    label: qsTr("Undo stroke")
                    tip: qsTr("Undo the last test stroke, not a brush edit")
                    enabled: pad.canUndoPad
                    onClicked: { shelf.padNotice = ""; pad.undoPad() }
                },
                KpButton {
                    objectName: "brushStudioPadClear"
                    visible: shelf.expanded
                    small: true
                    glyph: "reset"
                    label: qsTr("Clear")
                    tip: qsTr("Clear the test pad and its stroke history")
                    onClicked: { shelf.padNotice = ""; pad.clearPad() }
                },
                KpButton {
                    objectName: "brushStudioTogglePreview"
                    small: true
                    glyph: shelf.expanded ? "chevron-down" : "chevron-up"
                    tip: shelf.expanded ? qsTr("Collapse preview shelf") : qsTr("Expand preview shelf")
                    onClicked: shelf.toggleRequested()
                }
            ]
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: KP.gapS
            visible: shelf.expanded
            BrushStudioPad {
                id: pad
                objectName: "brushStudioPad"
                anchors.fill: parent
                clip: true
                draft: shelf.draft
                inputSettings: shelf.tabletSettings
                brushColor: shelf.ink
                deskColour: KP.previewPaper
                checkerLight: KP.previewPaper
                checkerDark: KP.previewPaper
                enabled: renderIssue.length === 0
                onStrokeError: function(message) { shelf.padNotice = message }
                onPadNotice: function(message) { shelf.padNotice = message }
                onRenderIssueChanged: shelf.padNotice = ""
            }
            Rectangle {
                visible: pad.renderIssue.length > 0
                anchors.fill: parent
                color: KP.panelInsetBg
                Text {
                    objectName: "brushStudioPadIssue"
                    anchors.fill: parent
                    anchors.margins: KP.panelInset
                    text: pad.renderIssue
                    textFormat: Text.PlainText
                    color: KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontM
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
            }
        }
        Text {
            visible: shelf.expanded && shelf.padNotice.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.panelInset
            text: shelf.padNotice
            textFormat: Text.PlainText
            color: KP.warning
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }
}
