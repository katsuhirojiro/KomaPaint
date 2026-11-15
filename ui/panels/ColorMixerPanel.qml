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
    property var materialPigments: [
        { value: "PY3", text: "PY3 · Primary Yellow", colour: "#e4cf29" },
        { value: "PB15", text: "PB15 · Phthalo Blue", colour: "#285bb8" },
        { value: "PR122", text: "PR122 · Quinacridone Magenta", colour: "#c51d71" },
        { value: "PW6", text: "PW6 · Titanium White", colour: "#e9e8df" },
        { value: "PBk7", text: "PBk7 · Carbon Black", colour: "#202124" },
        { value: "PBr7", text: "PBr7 · Raw Umber", colour: "#76543a" }
    ]
    property string pigmentA: "PY3"
    property string pigmentB: "PB15"
    property bool mixtureLoaded: false
    function pigmentColour(id) {
        for (let i = 0; i < materialPigments.length; ++i)
            if (materialPigments[i].value === id) return materialPigments[i].colour
        return "#808080"
    }
    function rgbHex(color) {
        function component(value) { return Math.round(value * 255).toString(16).padStart(2, "0") }
        return "#" + component(color.r) + component(color.g) + component(color.b)
    }
    readonly property color mixed: Spectral.mixHex(panel.rgbHex(panel.first), panel.rgbHex(panel.second), panel.amount)
    readonly property color materialMixed: Spectral.mixHex(panel.pigmentColour(panel.pigmentA),
                                                          panel.pigmentColour(panel.pigmentB), panel.amount)
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
            Text { text: qsTr("Named pigments (composition retained)"); color: KP.textMuted; font.pixelSize: KP.fontS }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: pigmentAChoice
                    objectName: "materialPigmentA"
                    Layout.fillWidth: true
                    model: panel.materialPigments
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: Math.max(0, indexOfValue(panel.pigmentA))
                    onActivated: panel.pigmentA = currentValue
                    Accessible.name: qsTr("First named pigment")
                }
                ComboBox {
                    id: pigmentBChoice
                    objectName: "materialPigmentB"
                    Layout.fillWidth: true
                    model: panel.materialPigments
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: Math.max(0, indexOfValue(panel.pigmentB))
                    onActivated: panel.pigmentB = currentValue
                    Accessible.name: qsTr("Second named pigment")
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Rectangle { Layout.preferredWidth: 28; Layout.preferredHeight: 26; color: panel.materialMixed; border.color: KP.borderLight }
                KpButton {
                    Layout.fillWidth: true
                    label: qsTr("Load mixture into brush")
                    objectName: "loadMaterialMixture"
                    onClicked: {
                        const components = []
                        const firstMass = 1 - panel.amount
                        const secondMass = panel.amount
                        if (firstMass > 0.000001) components.push({ pigmentId: panel.pigmentA, mass: firstMass })
                        if (secondMass > 0.000001) components.push({ pigmentId: panel.pigmentB, mass: secondMass })
                        Workspace.colors.foreground = panel.materialMixed
                        Workspace.brush.traditionalMedia = true
                        panel.mixtureLoaded = Workspace.brush.loadMaterialMixture({
                            mediumId: "watercolour", pigments: components,
                            water: 2.0, binder: 0.12, filler: 0.0, drying: 0.0, settling: 0.0
                        })
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: panel.mixtureLoaded
                text: qsTr("Named pigment proportions are loaded into the material brush.")
                color: KP.textMuted; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 26; color: panel.mixed; border.color: KP.borderLight }
                KpButton { label: qsTr("Use RGB mix"); onClicked: Workspace.colors.foreground = panel.mixed }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Spectral paint simulation. Real pigments and media may differ.")
                color: KP.textMuted; font.pixelSize: KP.fontXS; wrapMode: Text.WordWrap
            }
        }
    }
}
