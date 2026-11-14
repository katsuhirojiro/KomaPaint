import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// Document properties.
//
// Laid out as a definition list — a quiet label column and the value it names —
// rather than a stack of group boxes. Values are editable where the document
// allows it, and the transparency switch is the real document setting, not a
// display of it.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property var document: Workspace.document
    readonly property int labelWidth: 52

    component FieldLabel: Text {
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: panel.labelWidth
        color: KP.textMuted
        font.pixelSize: KP.fontS
        verticalAlignment: Text.AlignVCenter
    }

    Flickable {
        id: scroll
        objectName: "propertiesScroller"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: content.height + 2 * KP.gapM
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: KpScrollBar { policy: scroll.contentHeight > scroll.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        Column {
            id: content
            x: KP.panelInset
            y: KP.gapS
            width: scroll.width - 2 * KP.panelInset
            spacing: 0

            PanelSection {
                width: parent.width
                title: qsTr("Document")
                contentTopMargin: KP.gapM

                Item {
                    width: parent.width
                    height: documentColumn.height

                    Column {
                        id: documentColumn
                        width: parent.width
                        spacing: KP.gapS

                        RowLayout {
                            width: parent.width
                            height: KP.controlHeight
                            spacing: KP.gapM
                            FieldLabel { text: qsTr("Title") }
                            KpTextField {
                                objectName: "documentTitleField"
                                Layout.fillWidth: true
                                Layout.preferredHeight: KP.controlHeight
                                alignment: TextInput.AlignLeft
                                text: panel.document ? panel.document.title : ""
                                onEdited: function(value) { if (panel.document) panel.document.title = value }
                            }
                        }

                        RowLayout {
                            width: parent.width
                            height: KP.controlHeight
                            spacing: KP.gapM
                            FieldLabel { text: qsTr("Size") }
                            RowLayout {
                                spacing: KP.gapS
                                Layout.fillWidth: true
                                KpNumberField {
                                    objectName: "documentWidthField"
                                    // The two size fields share whatever the label
                                    // column leaves; a fixed width clipped the
                                    // height field in a narrow dock.
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 58
                                    Layout.preferredWidth: 68
                                    Layout.preferredHeight: KP.controlHeight
                                    value: panel.document ? panel.document.width : 0
                                    from: 1
                                    to: 16384
                                    unit: "W"
                                    onEdited: function(v) { if (panel.document) panel.document.width = v }
                                }
                                KpNumberField {
                                    objectName: "documentHeightField"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 58
                                    Layout.preferredWidth: 68
                                    Layout.preferredHeight: KP.controlHeight
                                    value: panel.document ? panel.document.height : 0
                                    from: 1
                                    to: 16384
                                    unit: "H"
                                    onEdited: function(v) { if (panel.document) panel.document.height = v }
                                }
                            }
                        }

                        RowLayout {
                            width: parent.width
                            height: KP.controlHeight
                            spacing: KP.gapM
                            FieldLabel { text: qsTr("Resolution") }
                            KpNumberField {
                                objectName: "documentDpiField"
                                Layout.preferredWidth: 74
                                Layout.preferredHeight: KP.controlHeight
                                value: panel.document ? Math.round(panel.document.dpi) : 0
                                from: 1
                                to: 2400
                                step: 10
                                unit: "dpi"
                                onEdited: function(v) { if (panel.document) panel.document.dpi = v }
                            }
                        }

                        RowLayout {
                            width: parent.width
                            height: 26
                            spacing: KP.gapM
                            FieldLabel { text: qsTr("Background") }

                            // A switch, because this is a state rather than a
                            // value: transparent paper on, or off.
                            Item {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 34
                                Layout.preferredHeight: 18
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 9
                                    color: transparencyToggle.checked ? KP.accent : KP.inputBg
                                    border.width: 1
                                    border.color: transparencyToggle.checked ? KP.accent : KP.border
                                    Rectangle {
                                        x: transparencyToggle.checked ? parent.width - 15 : 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        height: 14
                                        radius: 7
                                        color: transparencyToggle.checked ? KP.textOnAccent : KP.textMuted
                                        Behavior on x { NumberAnimation { duration: KP.durationFast; easing.type: KP.easingCurve } }
                                    }
                                }
                                MouseArea {
                                    id: transparencyToggle
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    property bool checked: panel.document ? panel.document.hasTransparentBackground : false
                                    onClicked: {
                                        checked = !checked
                                        if (panel.document) panel.document.setTransparentBackground(checked)
                                    }
                                }
                            }
                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: transparencyToggle.checked ? qsTr("Transparent") : qsTr("Filled")
                                color: KP.text
                                font.pixelSize: KP.fontS
                            }
                        }
                    }
                }
            }

            PanelSection {
                width: parent.width
                title: qsTr("At a glance")
                contentTopMargin: KP.gapS

                Item {
                    width: parent.width
                    height: factsColumn.height

                    Column {
                        id: factsColumn
                        width: parent.width
                        spacing: KP.gapXS

                        Repeater {
                            model: [
                                { label: qsTr("Pixels"), value: panel.document ? (panel.document.width * panel.document.height).toLocaleString(Qt.locale(), "f", 0) : "—" },
                                { label: qsTr("History"), value: Workspace.history ? Math.round(Workspace.history.memoryBytes / 1024) + " KB" : "—" },
                                { label: qsTr("File"), value: Workspace.currentFile.length > 0 ? Workspace.currentFile : qsTr("not saved yet") }
                            ]
                            delegate: Row {
                                required property var modelData
                                width: factsColumn.width
                                height: 18
                                spacing: KP.gapM
                                Text {
                                    width: 62
                                    text: modelData.label
                                    color: KP.textSubtle
                                    font.pixelSize: KP.fontXS
                                    font.family: KP.fontUi
                                }
                                Text {
                                    width: parent.width - 62 - KP.gapM
                                    text: modelData.value
                                    color: KP.textMuted
                                    font.pixelSize: KP.fontXS
                                    font.family: KP.fontMono
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // A form taller than the panel is cut at the panel's edge; the fade marks
    // the fold as "there is more below" instead of leaving a sliced line.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 14
        visible: scroll.contentHeight > scroll.height
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(KP.paneBg.r, KP.paneBg.g, KP.paneBg.b, 0.0) }
            GradientStop { position: 1.0; color: KP.paneBg }
        }
    }
}
