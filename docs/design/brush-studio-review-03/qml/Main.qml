import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import QtQuick.Dialogs
import "."

C.ApplicationWindow {
    id: window
    objectName: "brushStudioWindow"
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 720
    title: "KomaPaint — Brush Studio (Third Design Review)"
    color: Theme.canvas

    property bool compact: width < 1100
    property bool curveExpanded: true
    property string importKind: "mark"
    property var brushNames: studio.brushChoices.map(function(item) { return item.name })

    Shortcut { sequence: "Ctrl+Z"; onActivated: studio.undoPad() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: studio.redoPad() }
    Shortcut { sequence: "Ctrl+Y"; onActivated: studio.redoPad() }
    Shortcut { sequence: "Ctrl+Alt+Z"; onActivated: studio.undoEdit() }
    Shortcut { sequence: "Ctrl+Alt+Shift+Z"; onActivated: studio.redoEdit() }
    Shortcut { sequence: "Ctrl+Shift+A"; onActivated: studio.toggleAdvanced() }
    Shortcut { sequence: "Ctrl+S"; onActivated: studio.saveInitial() }

    header: Rectangle {
        height: Theme.headerHeight
        color: Theme.panel
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.hairline }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 12
            spacing: 8

            Text {
                text: "Brush Studio"
                color: Theme.text
                font.family: Theme.ui
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Rectangle { width: 1; height: 16; color: Theme.hairline }
            SelectField {
                objectName: "brushPicker"
                Layout.preferredWidth: 156
                height: 26
                options: window.brushNames
                current: studio.brushName
                onSelected: function(value) {
                    var found = studio.brushChoices.find(function(item) { return item.name === value })
                    if (found) studio.selectBrush(found.id)
                }
            }
            Segmented {
                Layout.preferredWidth: 142
                height: 26
                small: true
                options: ["Digital", "Realistic"]
                currentIndex: studio.engine === "Realistic" ? 1 : 0
                onChosen: function(index) { studio.setEngine(index === 0 ? "Digital" : "Realistic") }
            }
            Text {
                visible: window.width >= 1210
                text: studio.engine === "Digital" ? "marks · groups · grain" : "wet material simulation"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "editUndo"
                text: window.width >= 1180 ? "Brush undo" : ""
                icon: "undo"
                compact: true
                enabled: studio.editCanUndo
                onClicked: studio.undoEdit()
            }
            ActionButton {
                objectName: "editRedo"
                text: window.width >= 1180 ? "Brush redo" : ""
                icon: "redo"
                compact: true
                enabled: studio.editCanRedo
                onClicked: studio.redoEdit()
            }
            Rectangle { width: 1; height: 16; color: Theme.hairline }
            Text {
                text: studio.modified ? "Personal changes" : "Creator only"
                color: studio.modified ? Theme.secondary : Theme.meta
                font.family: Theme.ui
                font.pixelSize: 8
            }
            Rectangle { width: 5; height: 5; radius: 3; color: studio.modified ? Theme.text : Theme.meta }
            Rectangle { width: 1; height: 16; color: Theme.hairline }
            Text {
                text: "REVIEW 03 · ISOLATED"
                color: Theme.meta
                font.family: Theme.ui
                font.pixelSize: 7
                font.letterSpacing: 0.8
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rail {
                id: rail
                Layout.preferredWidth: window.compact ? Theme.railCompact : Theme.railWidth
                Layout.fillHeight: true
                compact: window.compact
                areas: studio.areas
                current: studio.area
                brushName: studio.brushName
                engine: studio.engine
                onChosen: function(key) { studio.setArea(key) }
                onBrushRequested: studio.note("Brush switching is in the header. The creator definition and your personal layer stay separate.")
            }

            Inspector {
                Layout.preferredWidth: Theme.inspectorWidth
                Layout.fillHeight: true
                onTipRequested: tipRoom.open()
                onReuseRequested: reuseRoom.open()
                onImportRequested: { window.importKind = "mark"; importDialog.open() }
            }

            Pad {
                Layout.fillWidth: true
                Layout.fillHeight: true
                curveExpanded: window.curveExpanded
                onCurveToggle: window.curveExpanded = !window.curveExpanded
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.statusHeight
            color: Theme.panel
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.hairline }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: studio.modified ? "Personal overrides active · creator definition intact"
                                          : "Creator definition intact · no personal overrides"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 8
                    elide: Text.ElideRight
                }
                Rectangle { visible: !window.compact; width: 1; height: 11; color: Theme.hairline }
                Text {
                    visible: !window.compact
                    text: studio.counts.mark + " marks · " + studio.counts.group + " groups · "
                        + studio.counts.texture + " textures · " + studio.counts.brush + " secondary brushes"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 8
                    font.features: {"tnum": 1}
                }
                Text {
                    visible: !window.compact
                    text: studio.auditSummary[0].value + " settings catalogued"
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: 8
                }
                Rectangle { visible: !window.compact; width: 1; height: 11; color: Theme.hairline }
                Text {
                    visible: !window.compact
                    text: "Preview compositor · production renderer untouched"
                    color: Theme.disabled
                    font.family: Theme.ui
                    font.pixelSize: 8
                }
            }
        }
    }

    TipRoom { id: tipRoom }
    ReuseRoom { id: reuseRoom }

    FileDialog {
        id: importDialog
        objectName: "importDialog"
        title: "Import a mark image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)", "All files (*)"]
        onAccepted: studio.importImage(selectedFile, window.importKind)
    }
}
