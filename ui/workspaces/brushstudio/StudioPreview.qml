import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../themes"

// Reserve the comparison shelf, not a substitute renderer. B5 will put a
// production-engine preview here; a mock stroke would misrepresent the draft.
Rectangle {
    id: shelf
    required property bool expanded
    required property int expandedHeight
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
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Preview unavailable")
                    color: KP.textMuted
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                },
                Row {
                    visible: shelf.width >= 820 && shelf.expanded
                    spacing: KP.gapXS
                    KpButton { small: true; glyph: "reset"; label: qsTr("Clear"); enabled: false }
                    KpButton { small: true; label: qsTr("Paper"); enabled: false; variant: "solid" }
                    KpButton { small: true; label: qsTr("Dark"); enabled: false; variant: "solid" }
                    KpButton { small: true; label: qsTr("Checker"); enabled: false; variant: "solid" }
                    KpButton { small: true; label: qsTr("Stroke"); enabled: false; variant: "solid" }
                    KpButton { small: true; label: qsTr("Stamps"); enabled: false; variant: "solid" }
                    KpButton { small: true; label: qsTr("Pressure"); enabled: false; variant: "solid" }
                    KpButton { objectName: "brushStudioCompare"; small: true; glyph: "swap"; label: qsTr("A/B"); enabled: false }
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
        RowLayout {
            visible: shelf.expanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: KP.gapM
            spacing: KP.gapM
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: KP.radiusXS
                color: KP.previewPaper
                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: KP.panelInset
                    text: qsTr("CURRENT")
                    color: KP.isDark ? KP.textOnAccent : KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                Rectangle {
                    visible: shelf.width >= 820
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: KP.gapM
                    anchors.bottomMargin: KP.gapM
                    width: KP.hairlineWidth
                    color: KP.border
                }
                Text {
                    visible: shelf.width >= 820
                    x: parent.width / 2 + KP.panelInset
                    y: KP.panelInset
                    text: qsTr("SAVED")
                    color: KP.isDark ? KP.textOnAccent : KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: KP.panelInset
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, (shelf.width >= 820 ? parent.width / 2 : parent.width) - 2 * KP.panelInset)
                    text: qsTr("Live stroke rendering isn’t available in this build.")
                    color: KP.isDark ? KP.textOnAccent : KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontM
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: shelf.width >= 820
                    x: parent.width / 2 + KP.panelInset
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, parent.width / 2 - 2 * KP.panelInset)
                    text: qsTr("Saved comparison unavailable")
                    color: KP.isDark ? KP.textOnAccent : KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontM
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
            Rectangle {
                Layout.preferredWidth: Math.min(320, shelf.width * 0.25)
                Layout.fillHeight: true
                visible: shelf.width >= 820
                color: KP.panelInsetBg
                radius: KP.radiusXS
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: KP.panelInset
                    spacing: KP.gapS
                    Text {
                        text: qsTr("STAMPS  ·  SIZES")
                        color: KP.textSectionLabel
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontMicro
                        font.letterSpacing: KP.trackingMicro
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Stamp and size samples require Live Preview.")
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        wrapMode: Text.WordWrap
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
