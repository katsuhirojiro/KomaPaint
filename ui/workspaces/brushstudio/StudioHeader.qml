import QtQuick
import QtQuick.Layouts
import "../../themes"

// The studio's dock rail follows the same surface and micro-label scale as the
// application's panels. Actions added by a panel go on the right.
Rectangle {
    id: root
    property string title: ""
    property alias actions: trailing.data
    implicitHeight: KP.panelHeaderH
    color: KP.paneHeaderBg

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: KP.panelInset
        anchors.rightMargin: KP.gapM
        spacing: KP.gapM

        Text {
            Layout.fillWidth: true
            text: root.title
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: KP.textSectionLabel
            font.family: KP.fontUi
            font.pixelSize: KP.fontMicro
            font.capitalization: Font.AllUppercase
            font.letterSpacing: KP.trackingMicro
        }
        Row {
            id: trailing
            Layout.alignment: Qt.AlignVCenter
            spacing: KP.gapS
        }
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: KP.hairlineWidth
        color: KP.hairline
    }
}
