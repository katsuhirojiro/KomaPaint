import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"
import "../components/SpectralMix.js" as Spectral

// Spectral Kubelka–Munk mixing; a pigment-inspired simulation, not real paint.
Rectangle {
    id: panel
    color: KP.paneBg
    property color first: String(Workspace.sessionValue("ui.colorMixer.first", "#c65742"))
    property color second: String(Workspace.sessionValue("ui.colorMixer.second", "#5476ad"))
    property real amount: Math.max(0, Math.min(1, Number(Workspace.sessionValue("ui.colorMixer.amount", 0.5))))
    function rgbHex(color) {
        function component(value) { return Math.round(value * 255).toString(16).padStart(2, "0") }
        return "#" + component(color.r) + component(color.g) + component(color.b)
    }
    readonly property color mixed: Spectral.mixHex(panel.rgbHex(panel.first), panel.rgbHex(panel.second), panel.amount)
    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.height + 2 * KP.panelInset
        ColumnLayout {
            id: column
            x: KP.panelInset; y: KP.panelInset
            width: parent.width - 2 * KP.panelInset
            spacing: KP.gapS
            Text { text: qsTr("Pigment-inspired mix"); color: KP.textMuted; font.pixelSize: KP.fontS }
            RowLayout {
                Layout.fillWidth: true
                spacing: KP.gapS
                Rectangle { Layout.preferredWidth: 26; Layout.preferredHeight: 24; color: panel.first; border.color: KP.borderLight }
                KpButton { label: qsTr("Set A"); tip: qsTr("Use drawing colour as A"); onClicked: { panel.first = Workspace.colors.foreground; Workspace.setSessionValue("ui.colorMixer.first", panel.first.toString()) } }
                Rectangle { Layout.preferredWidth: 26; Layout.preferredHeight: 24; color: panel.second; border.color: KP.borderLight }
                KpButton { label: qsTr("Set B"); tip: qsTr("Use drawing colour as B"); onClicked: { panel.second = Workspace.colors.foreground; Workspace.setSessionValue("ui.colorMixer.second", panel.second.toString()) } }
            }
            Slider {
                objectName: "colorMixerRatio"
                Layout.fillWidth: true
                from: 0; to: 1; value: panel.amount
                Accessible.name: qsTr("Mix ratio")
                onMoved: {
                    panel.amount = value
                    if (!pressed) Workspace.setSessionValue("ui.colorMixer.amount", panel.amount)
                }
                onPressedChanged: if (!pressed) Workspace.setSessionValue("ui.colorMixer.amount", panel.amount)
            }
            RowLayout {
                Layout.fillWidth: true
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 26; color: panel.mixed; border.color: KP.borderLight }
                KpButton { label: qsTr("Use mix"); onClicked: Workspace.colors.foreground = panel.mixed }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Spectral paint simulation. Real pigments and media may differ.")
                color: KP.textMuted; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
            }
        }
    }
}
