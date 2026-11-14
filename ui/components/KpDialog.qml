import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// The frame every message and form dialog in the application shares.
//
// Qt's stock dialog chrome is the one place where the interface would still
// look like a widget toolkit window: a grey title bar, platform buttons and a
// different type scale from everything around it. This frame replaces that with
// the surfaces, hairlines and type the rest of the shell uses, so a dialog
// reads as part of the application rather than as a system alert.
//
// It stays a Dialog, not a bare Popup, so accept()/reject() and the accepted/
// rejected signals keep behaving the way the rest of the code and the interface
// tests expect.
//
// Usage:
//     KpDialog {
//         id: question
//         heading: qsTr("Save changes?")
//         body: [
//             Text { text: qsTr("…") }
//         ]
//         actions: [
//             KpButton { label: qsTr("Cancel"); onClicked: question.reject() },
//             KpButton { label: qsTr("Save"); variant: "primary"; onClicked: question.accept() }
//         ]
//     }
//
// Buttons are right-aligned in the order given, so the primary action belongs
// last. A dialog with no actions simply has no footer.
Dialog {
    id: dialog

    // The frame's width: bodies wrap to it and the header spans it.
    property int bodyWidth: 380
    property string heading: ""
    // Muted line under the heading, e.g. what a destructive action leaves alone.
    property string note: ""

    // Body column: a list of items, laid out top to bottom.
    property alias body: bodyColumn.data
    // Footer buttons.
    property alias actions: actionRow.data

    // A Popup is not an Item, so it does not inherit the window's palette: the
    // stock controls it hosts (spin boxes, combo boxes) would fall back to the
    // system's light style. Set the same palette here so they match the theme.
    palette.window: KP.paneBg
    palette.windowText: KP.text
    palette.base: KP.inputBg
    palette.alternateBase: KP.paneBgAlt
    palette.text: KP.text
    palette.button: KP.buttonBg
    palette.buttonText: KP.text
    palette.brightText: KP.textOnAccent
    palette.highlight: KP.selection
    palette.highlightedText: KP.textOnAccent
    palette.toolTipBase: KP.paneHeaderBg
    palette.toolTipText: KP.text
    palette.placeholderText: KP.textSubtle
    palette.mid: KP.border
    palette.midlight: KP.borderLight
    palette.dark: KP.border

    title: dialog.heading
    modal: true
    closePolicy: Dialog.CloseOnEscape
    standardButtons: Dialog.NoButton
    padding: 0
    anchors.centerIn: parent
    implicitWidth: dialog.bodyWidth

    background: Rectangle {
        color: KP.floatingBg
        radius: KP.radiusPanel
        border.width: 1
        border.color: KP.border
    }

    // ---- Heading
    header: Item {
        implicitHeight: 44

        Text {
            anchors.left: parent.left
            anchors.right: closeButton.left
            anchors.leftMargin: KP.gapXL
            anchors.rightMargin: KP.gapS
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.heading
            color: KP.text
            font.pixelSize: KP.fontL
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        IconButton {
            id: closeButton
            anchors.right: parent.right
            anchors.rightMargin: KP.gapM
            anchors.verticalCenter: parent.verticalCenter
            glyph: "close"
            cell: KP.controlHeight
            iconSize: KP.iconS
            tooltip: qsTr("Close")
            onClicked: dialog.reject()
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: KP.hairline
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ---- Body
        ColumnLayout {
            id: bodyColumn
            Layout.fillWidth: true
            Layout.leftMargin: KP.gapXL
            Layout.rightMargin: KP.gapXL
            Layout.topMargin: KP.gapL
            Layout.bottomMargin: dialog.note.length > 0 ? KP.gapM : KP.gapXL
            spacing: KP.gapM

            Text {
                Layout.fillWidth: true
                visible: dialog.note.length > 0
                text: dialog.note
                color: KP.textMuted
                font.pixelSize: KP.fontS
                wrapMode: Text.WordWrap
            }
        }

        // ---- Footer
        Item {
            Layout.fillWidth: true
            implicitHeight: actionRow.children.length > 0 ? 56 : 0
            visible: actionRow.children.length > 0

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: KP.hairline
            }
            RowLayout {
                id: actionRow
                anchors.right: parent.right
                anchors.rightMargin: KP.gapL
                anchors.verticalCenter: parent.verticalCenter
                spacing: KP.gapS
            }
        }
    }
}
