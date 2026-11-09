import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../themes"

// New Project dialog — replaces the temporary "New Document" dialog.
//
// Presents the supported project creation modes (Illustration is the only
// fully functional one; Comic Page / Multi-page Comic / Animation are
// reserved and disabled) and collects the illustration settings: name,
// canvas size in editable units, DPI and background.
//
// The canonical physical size lives in NewProject (inches), so switching
// units never drifts; this dialog only edits and displays derived values.
Dialog {
    id: newProjectDialog
    title: qsTr("New Project")
    modal: true
    anchors.centerIn: parent
    // Custom OK/Cancel buttons are laid out inside contentItem so the OK
    // button can be disabled when the project settings are invalid.
    standardButtons: Dialog.NoButton

    onAccepted: {
        if (!NewProject.isValid()) return
        const ok = Workspace.newProject(nameInput.text.trimmed(),
                                        NewProject.widthPixels,
                                        NewProject.heightPixels,
                                        NewProject.transparentBackground,
                                        NewProject.backgroundColor,
                                        NewProject.dpi)
        if (!ok) {
            summaryError.text = qsTr("Could not create the project.")
            return
        }
    }

    onOpened: {
        NewProject.resetToDefaults()
        nameInput.text = NewProject.name
        unitCombo.currentText = NewProject.units
        widthInput.value  = NewProject.widthInUnit(NewProject.units)
        heightInput.value = NewProject.heightInUnit(NewProject.units)
        dpiInput.value    = NewProject.dpi
        transparentInput.checked = NewProject.transparentBackground
        summaryError.text = ""
    }

    contentItem: ColumnLayout {
        spacing: KP.gapM
        width: 460

        // ------------------------------------------------------------- Modes
        Label { text: qsTr("Project type"); font.pixelSize: KP.fontS; color: KP.textMuted }
        RowLayout {
            spacing: KP.gapS
            ToolButton {
                id: modeIllustration
                text: qsTr("Illustration")
                checkable: true
                checked: true
                enabled: true
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("Comic Page")
                checkable: true
                checked: false
                enabled: false
                tooltip: qsTr("Coming later")
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("Multi-page Comic")
                checkable: true
                checked: false
                enabled: false
                tooltip: qsTr("Coming later")
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("Animation")
                checkable: true
                checked: false
                enabled: false
                tooltip: qsTr("Coming later")
                Layout.fillWidth: true
            }
        }

        // ------------------------------------------------------------- Illustration settings
        Rectangle {
            visible: modeIllustration.checked
            color: KP.paneBgAlt
            border.color: KP.border
            radius: KP.radiusS
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapM
                anchors.rightMargin: KP.gapM
                anchors.topMargin: KP.gapM
                anchors.bottomMargin: KP.gapM
                spacing: KP.gapM

                TextField {
                    id: nameInput
                    placeholderText: qsTr("Project name")
                    text: NewProject.name
                    onTextEdited: NewProject.setName(text)
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: KP.gapS
                    Label { text: qsTr("Width"); Layout.preferredWidth: 60 }
                    SpinBox {
                        id: widthInput
                        from: 1; to: NewProject.maxDimension(); value: 1600; editable: true
                        Layout.fillWidth: true
                        onValueModified: {
                            NewProject.setSizeInUnit(unitCombo.currentText, widthInput.value, heightInput.value)
                            widthInput.value  = NewProject.widthInUnit(NewProject.units)
                            heightInput.value = NewProject.heightInUnit(NewProject.units)
                        }
                    }
                    ComboBox {
                        id: unitCombo
                        model: NewProject.availableUnits()
                        currentText: NewProject.units
                        onActivated: {
                            NewProject.setUnits(currentText)
                            widthInput.value  = NewProject.widthInUnit(NewProject.units)
                            heightInput.value = NewProject.heightInUnit(NewProject.units)
                        }
                    }
                }
                RowLayout {
                    spacing: KP.gapS
                    Label { text: qsTr("Height"); Layout.preferredWidth: 60 }
                    SpinBox {
                        id: heightInput
                        from: 1; to: NewProject.maxDimension(); value: 1200; editable: true
                        Layout.fillWidth: true
                        onValueModified: {
                            NewProject.setSizeInUnit(unitCombo.currentText, widthInput.value, heightInput.value)
                            widthInput.value  = NewProject.widthInUnit(NewProject.units)
                            heightInput.value = NewProject.heightInUnit(NewProject.units)
                        }
                    }
                }
                RowLayout {
                    spacing: KP.gapS
                    Label { text: qsTr("Resolution"); Layout.preferredWidth: 60 }
                    SpinBox {
                        id: dpiInput
                        from: 1; to: 1200; value: 300; editable: true
                        Layout.fillWidth: true
                        onValueModified: NewProject.setDpi(value)
                    }
                    Label { text: qsTr("dpi"); color: KP.textMuted; font.pixelSize: KP.fontS }
                }
                RowLayout {
                    spacing: KP.gapS
                    CheckBox {
                        id: transparentInput
                        text: qsTr("Transparent background")
                        checked: NewProject.transparentBackground
                        onCheckedChanged: NewProject.setTransparentBackground(checked)
                    }
                }
                RowLayout {
                    spacing: KP.gapS
                    enabled: !transparentInput.checked
                    Button {
                        text: qsTr("Background colour…")
                        onClicked: backgroundPicker.open()
                    }
                    Rectangle {
                        width: 30; height: 30
                        color: NewProject.backgroundColor
                        border.color: KP.border
                    }
                }
            }
        }

        // ------------------------------------------------------------- Summary
        Rectangle {
            color: KP.inputBg
            border.color: KP.border
            radius: KP.radiusS
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapM
                anchors.rightMargin: KP.gapM
                anchors.topMargin: KP.gapS
                anchors.bottomMargin: KP.gapS
                spacing: KP.gapXS
                Label {
                    text: qsTr("%1 × %2 px  •  %3 dpi  •  %4 × %5 inches")
                    .arg(NewProject.widthPixels)
                    .arg(NewProject.heightPixels)
                    .arg(Math.round(NewProject.dpi))
                    .arg(NewProject.widthInUnit("inches"), 0, 'f', 2)
                    .arg(NewProject.heightInUnit("inches"), 0, 'f', 2)
                    color: KP.text
                    font.family: KP.fontMono
                    font.pixelSize: KP.fontS
                    wrapMode: Text.WordWrap
                }
                Label {
                    id: summaryError
                    color: KP.error
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ------------------------------------------------------------- Buttons
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                enabled: true
                onClicked: newProjectDialog.reject()
            }
            Button {
                text: qsTr("OK")
                enabled: NewProject.isValid()
                default: true
                onClicked: newProjectDialog.accept()
            }
        }
    }

    ColorDialog {
        id: backgroundPicker
        title: qsTr("Background colour")
        selectedColor: NewProject.backgroundColor
        onAccepted: NewProject.backgroundColor = selectedColor
    }
}
