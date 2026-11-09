import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

Rectangle {
    id: header
    height: KP.panelHeaderH
    color: KP.paneHeaderBg
    border.color: KP.border
    border.width: 1

    property string title: ""
    signal closeClicked

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 4
        Label {
            text: header.title
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            font.bold: true
            font.capitalization: Font.MixedCase
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        // Panel menu
        Label {
            text: "≡"
            color: KP.textSubtle
            font.pixelSize: 14
            Layout.preferredWidth: 16
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.PointingHandCursor
                onClicked: /* panel menu placeholder */  {
                }
            }
        }
        // Close
        Label {
            text: "×"
            color: KP.textSubtle
            font.pixelSize: 14
            Layout.preferredWidth: 16
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.PointingHandCursor
                onClicked: header.closeClicked()
            }
        }
    }
}
