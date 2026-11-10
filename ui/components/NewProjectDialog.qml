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
    standardButtons: Dialog.NoButton

    // Dialog sizes to contentItem's implicit size. All controls are laid out
    // in a single ColumnLayout with no ScrollView, so nothing can collapse
    // or be pushed off-screen by a fill-height child.
    Layout.preferredWidth: 520
    implicitHeight: 640

    // Diagnostic log path: Documents/KomaPaint/NewProjectDiagnostic.log
    // Written by WorkspaceState::writeDiagnosticLog().

    onOpened: {
        Workspace.clearDiagnosticLog()
        Workspace.writeDiagnosticLog("=== New Project dialog opened ===")
        Workspace.writeDiagnosticLog("name=" + NewProject.name)
        Workspace.writeDiagnosticLog("units=" + NewProject.units)
        Workspace.writeDiagnosticLog("widthInches=" + NewProject.widthInches)
        Workspace.writeDiagnosticLog("heightInches=" + NewProject.heightInches)
        Workspace.writeDiagnosticLog("widthPixels=" + NewProject.widthPixels)
        Workspace.writeDiagnosticLog("heightPixels=" + NewProject.heightPixels)
        Workspace.writeDiagnosticLog("dpi=" + NewProject.dpi)
        Workspace.writeDiagnosticLog("transparent=" + NewProject.transparentBackground)
        Workspace.writeDiagnosticLog("bgColor=" + NewProject.backgroundColor)
        Workspace.writeDiagnosticLog("isValid=" + NewProject.isValid())
        Workspace.writeDiagnosticLog("validationError=" + NewProject.validationError())

        NewProject.resetToDefaults()
        nameInput.text = NewProject.name
        unitCombo.currentIndex = NewProject.availableUnits().indexOf(NewProject.units)
        widthInput.value = NewProject.widthInUnit(NewProject.units)
        heightInput.value = NewProject.heightInUnit(NewProject.units)
        dpiInput.value = NewProject.dpi
        transparentInput.checked = NewProject.transparentBackground
        creationStatus.text = ""
        creationStatus.color = KP.textMuted
        summaryError.text = ""

        Workspace.writeDiagnosticLog("=== After reset ===")
        Workspace.writeDiagnosticLog("name=" + NewProject.name)
        Workspace.writeDiagnosticLog("widthPixels=" + NewProject.widthPixels)
        Workspace.writeDiagnosticLog("heightPixels=" + NewProject.heightPixels)
        Workspace.writeDiagnosticLog("dpi=" + NewProject.dpi)
        Workspace.writeDiagnosticLog("isValid=" + NewProject.isValid())
        Workspace.writeDiagnosticLog("validationError=" + NewProject.validationError())
    }

    // Confirm the workspace replaced the document and the viewport was notified.
    Connections {
        target: Workspace
        function onDocumentChanged() {
            Workspace.writeDiagnosticLog("Workspace.documentChanged signal received")
            creationStatus.text = qsTr("Document replaced — viewport notified.")
            creationStatus.color = "#4caf50"
        }
    }

    contentItem: ColumnLayout {
        Layout.fillWidth: true
        spacing: KP.gapM

        // ------------------------------------------------------------- Modes
        Label { text: qsTr("Project type"); font.pixelSize: KP.fontS; color: KP.textMuted }
        RowLayout {
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

        // ------------------------------------------------------------- Illustration settings
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
                        from: 1
                        to: NewProject.maxDimension()
                        value: 1600
                        editable: true
                        Layout.fillWidth: true
                        onValueModified: {
                            NewProject.setSizeInUnit(unitCombo.currentText, widthInput.value, heightInput.value)
                            widthInput.value = NewProject.widthInUnit(NewProject.units)
                            heightInput.value = NewProject.heightInUnit(NewProject.units)
                        }
                    }
                    ComboBox {
                        id: unitCombo
                        model: NewProject.availableUnits()
                        currentIndex: NewProject.availableUnits().indexOf(NewProject.units)
                        onActivated: {
                            NewProject.setUnits(unitCombo.currentText)
                            widthInput.value = NewProject.widthInUnit(NewProject.units)
                            heightInput.value = NewProject.heightInUnit(NewProject.units)
                        }
                    }
                }

                RowLayout {
                    spacing: KP.gapS
                    Label { text: qsTr("Height"); Layout.preferredWidth: 60 }
                    SpinBox {
                        id: heightInput
                        from: 1
                        to: NewProject.maxDimension()
                        value: 1200
                        editable: true
                        Layout.fillWidth: true
                        onValueModified: {
                            NewProject.setSizeInUnit(unitCombo.currentText, widthInput.value, heightInput.value)
                            widthInput.value = NewProject.widthInUnit(NewProject.units)
                            heightInput.value = NewProject.heightInUnit(NewProject.units)
                        }
                    }
                }

                RowLayout {
                    spacing: KP.gapS
                    Label { text: qsTr("Resolution"); Layout.preferredWidth: 60 }
                    SpinBox {
                        id: dpiInput
                        from: 1
                        to: 1200
                        value: 300
                        editable: true
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
                        width: 30
                        height: 30
                        color: Qt.color(NewProject.backgroundColor)
                        border.color: KP.border
                    }
                }
            }
        }

        // ------------------------------------------------------------- Summary
        Rectangle {
            Layout.fillWidth: true
            color: KP.inputBg
            border.color: KP.border
            radius: KP.radiusS
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

        // ------------------------------------------------------------- Creation status
        // Always visible area that reports the result of clicking Create.
        Rectangle {
            Layout.fillWidth: true
            color: "#2d2d2d"
            border.color: KP.border
            radius: KP.radiusS
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapM
                anchors.rightMargin: KP.gapM
                anchors.topMargin: KP.gapS
                anchors.bottomMargin: KP.gapS
                spacing: KP.gapXS
                Label {
                    id: creationStatus
                    text: qsTr("Ready to create project.")
                    color: KP.textMuted
                    font.pixelSize: KP.fontS
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Diagnostic log: Documents/KomaPaint/NewProjectDiagnostic.log")
                    color: KP.textMuted
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // ------------------------------------------------------------- Buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                enabled: true
                onClicked: newProjectDialog.reject()
            }
            Button {
                text: qsTr("Create")
                enabled: NewProject.isValid()
                onClicked: {
                    Workspace.writeDiagnosticLog("=== Create button clicked ===")
                    Workspace.writeDiagnosticLog("name=" + nameInput.text.trimmed())
                    Workspace.writeDiagnosticLog("widthPixels=" + NewProject.widthPixels)
                    Workspace.writeDiagnosticLog("heightPixels=" + NewProject.heightPixels)
                    Workspace.writeDiagnosticLog("transparent=" + NewProject.transparentBackground)
                    Workspace.writeDiagnosticLog("bgColor=" + NewProject.backgroundColor)
                    Workspace.writeDiagnosticLog("dpi=" + NewProject.dpi)
                    Workspace.writeDiagnosticLog("isValid=" + NewProject.isValid())
                    Workspace.writeDiagnosticLog("validationError=" + NewProject.validationError())

                    creationStatus.text = qsTr("Creating project...")
                    creationStatus.color = KP.textMuted

                    const ok = Workspace.newProject(nameInput.text.trimmed(),
                                                    NewProject.widthPixels,
                                                    NewProject.heightPixels,
                                                    NewProject.transparentBackground,
                                                    NewProject.backgroundColor,
                                                    NewProject.dpi)
                    Workspace.writeDiagnosticLog("Workspace.newProject() returned: " + ok)

                    if (ok) {
                        Workspace.writeDiagnosticLog("Creation succeeded, closing dialog")
                        creationStatus.text = qsTr("Project created successfully.")
                        creationStatus.color = "#4caf50"
                        newProjectDialog.accept()
                    } else {
                        Workspace.writeDiagnosticLog("Creation FAILED — dialog stays open")
                        creationStatus.text = qsTr("Project creation failed. See diagnostic log.")
                        creationStatus.color = KP.error
                        summaryError.text = qsTr("Could not create the project.")
                    }
                }
            }
            Button {
                text: qsTr("Open Diagnostic Log")
                enabled: true
                onClicked: {
                    const path = Workspace.diagnosticLogPath()
                    Workspace.writeDiagnosticLog("Opening diagnostic log: " + path)
                    Qt.openUrlExternally(path)
                }
            }
        }
    }

    ColorDialog {
        id: backgroundPicker
        title: qsTr("Background colour")
        selectedColor: Qt.color(NewProject.backgroundColor)
        onAccepted: {
            var c = selectedColor
            NewProject.backgroundColor = "#" +
                ("0" + c.red.toString(16)).slice(-2) +
                ("0" + c.green.toString(16)).slice(-2) +
                ("0" + c.blue.toString(16)).slice(-2)
        }
    }
}
