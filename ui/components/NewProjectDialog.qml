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
// units never drifts; pixels are derived at the last moment via
// round(inches * dpi).
Dialog {
    id: newProjectDialog
    title: qsTr("New Project")
    modal: true
    // Custom OK/Cancel buttons are laid out inside contentItem so the OK
    // button can be disabled when the project settings are invalid.
    standardButtons: Dialog.NoButton

    // Minimal implicit size so the dialog is large enough for all fields
    // on typical Windows DPI settings. Content that exceeds this is
    // scrollable; the button row stays pinned at the bottom.
    Layout.preferredWidth: 480
    implicitHeight: 520



    onOpened: {
        NewProject.resetToDefaults()
        nameInput.text = NewProject.name
        unitCombo.currentIndex = NewProject.availableUnits().indexOf(NewProject.units)
        widthInput.value  = NewProject.widthInUnit(NewProject.units)
        heightInput.value = NewProject.heightInUnit(NewProject.units)
        dpiInput.value    = NewProject.dpi
        transparentInput.checked = NewProject.transparentBackground
        summaryError.text = ""
    }

    contentItem: ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0

        // ------------------------------------------------------------- Modes
        Label { text: qsTr("Project type"); font.pixelSize: KP.fontS; color: KP.textMuted }
        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            Button {
                id: modeIllustration
                text: qsTr("Illustration")
                checkable: true
                checked: true
                enabled: true
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Comic Page")
                checkable: true
                checked: false
                enabled: false
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Multi-page Comic")
                checkable: true
                checked: false
                enabled: false
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Animation")
                checkable: true
                checked: false
                enabled: false
                Layout.fillWidth: true
            }
        }

        // ------------------------------------------------------------- Illustration settings (scrollable)
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            flickableDirection: Flickable.VerticalFlick
            contentWidth: width
            contentHeight: settingsColumn.implicitHeight
            ColumnLayout {
                id: settingsColumn
                width: parent.width
                spacing: KP.gapM

                Rectangle {
                    Layout.fillWidth: true
                    color: KP.paneBgAlt
                    border.color: KP.border
                    radius: KP.radiusS
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
                                currentIndex: NewProject.availableUnits().indexOf(NewProject.units)
                                onActivated: {
                                    NewProject.setUnits(unitCombo.currentText)
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
                                color: Qt.color(NewProject.backgroundColor)
                                border.color: KP.border
                            }
                        }
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
                    Layout.fillWidth: true
                }
                Label {
                    id: summaryError
                    color: KP.error
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // ------------------------------------------------------------- Buttons (pinned at bottom)
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: KP.gapM
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                enabled: true
                onClicked: newProjectDialog.reject()
            }
            Button {
                text: qsTr("OK")
                enabled: NewProject.isValid()
                onClicked: {
                    const ok = Workspace.newProject(nameInput.text.trimmed(),
                                                    NewProject.widthPixels,
                                                    NewProject.heightPixels,
                                                    NewProject.transparentBackground,
                                                    NewProject.backgroundColor,
                                                    NewProject.dpi)
                    if (ok) {
                        newProjectDialog.accept()
                    } else {
                        summaryError.text = qsTr("Could not create the project.")
                    }
                }
            }
        }
    }

    ColorDialog {
        id: backgroundPicker
        title: qsTr("Background colour")
        selectedColor: Qt.color(NewProject.backgroundColor)
        onAccepted: {
            var c = selectedColor;
            NewProject.backgroundColor = "#" +
                ("0" + c.red.toString(16)).slice(-2) +
                ("0" + c.green.toString(16)).slice(-2) +
                ("0" + c.blue.toString(16)).slice(-2);
        }
    }
}
