import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../themes"

// Preferences: the theme and the icon colours.
//
// The dialog edits KomaTheme.qml directly — the theme mode and the four icon
// colour overrides all live in that singleton, so a change here re-colours the
// window (and the icons) immediately, with no second copy of the colours to
// keep in sync. MainWindow.qml listens to those properties and stores them in
// QSettings, which is how the choice survives a restart.
//
// A colour override begins empty, meaning "use the theme default"; Reset
// clears it again so the icon follows whatever theme is active.
Dialog {
    id: dialog
    objectName: "preferencesDialog"
    anchors.centerIn: parent
    modal: true
    title: qsTr("Preferences")
    width: 510
    padding: KP.gapL

    background: Rectangle {
        color: KP.paneBg
        border.color: KP.border
        radius: KP.radiusM
    }

    // The four icon tints, in interface order. `kind` is the suffix of the
    // KomaTheme properties (icon<KIND>Pref / icon<KIND>Default).
    readonly property var iconStates: [
        { kind: "Normal",   label: qsTr("Normal") },
        { kind: "Hover",    label: qsTr("Hover") },
        { kind: "Selected", label: qsTr("Selected") },
        { kind: "Disabled", label: qsTr("Disabled") }
    ]

    // Bracket access on the singleton keeps one code path for all four
    // colours; the bindings still track the properties they read.
    function effectiveColour(kind) { return KP["icon" + kind] }
    function defaultColour(kind) { return KP["icon" + kind + "Default"] }
    function isCustom(kind) { return KP["icon" + kind + "Custom"] }
    function setColour(kind, value) { KP["icon" + kind + "Pref"] = value }
    function resetColour(kind) { KP["icon" + kind + "Pref"] = "" }
    function describe(kind) {
        return isCustom(kind) ? effectiveColour(kind).toString().toUpperCase()
                              : effectiveColour(kind).toString().toUpperCase() + qsTr("  default")
    }

    // Which colour the shared colour dialog is editing.
    property string picking: "Normal"

    contentItem: ColumnLayout {
        spacing: KP.gapM

        Label {
            text: qsTr("Appearance")
            color: KP.text
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapM
            Label {
                text: qsTr("Theme")
                color: KP.textMuted
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                Layout.preferredWidth: 92
            }
            SegmentedChoice {
                id: themeChoice
                objectName: "themeChoice"
                Layout.preferredWidth: 180
                options: [qsTr("Dark"), qsTr("Light")]
                currentIndex: KP.isDark ? 0 : 1
                onChosen: function(index) { KP.mode = (index === 1) ? "light" : "dark" }
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Icon colours")
            color: KP.text
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("The toolbar, tool palette, layer and panel icons follow the theme. Set your own colour here, or reset a row to keep the theme's colour. Project-type illustrations keep their own colours and are never tinted.")
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: dialog.iconStates
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: KP.gapM

                Label {
                    text: modelData.label
                    color: KP.text
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontS
                    Layout.preferredWidth: 92
                }
                Rectangle {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    radius: KP.radiusS
                    color: dialog.effectiveColour(modelData.kind)
                    border.width: 1
                    border.color: KP.border
                }
                Label {
                    text: dialog.describe(modelData.kind)
                    color: KP.textMuted
                    font.family: KP.fontMono
                    font.pixelSize: KP.fontS
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Button {
                    objectName: "iconColour" + modelData.kind + "Button"
                    text: qsTr("Change…")
                    onClicked: {
                        dialog.picking = modelData.kind
                        iconColourDialog.selectedColor = dialog.effectiveColour(modelData.kind)
                        iconColourDialog.open()
                    }
                }
                Button {
                    objectName: "iconColour" + modelData.kind + "Reset"
                    text: qsTr("Reset")
                    enabled: dialog.isCustom(modelData.kind)
                    onClicked: dialog.resetColour(modelData.kind)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapM
            Button {
                objectName: "resetIconColoursButton"
                text: qsTr("Reset all icon colours")
                enabled: KP.iconColoursCustom
                onClicked: KP.resetIconColours()
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "closePreferencesButton"
                text: qsTr("Close")
                onClicked: dialog.close()
            }
        }
    }

    ColorDialog {
        id: iconColourDialog
        objectName: "iconColourDialog"
        title: qsTr("Icon colour — %1").arg(dialog.picking)
        onAccepted: dialog.setColour(dialog.picking, selectedColor.toString())
    }
}
