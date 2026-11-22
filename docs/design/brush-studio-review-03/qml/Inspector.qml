import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts
import "."

Rectangle {
    id: inspector
    signal tipRequested
    signal reuseRequested
    signal importRequested
    color: Theme.panel

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.hairline
    }

    Column {
        anchors.fill: parent

        Item {
            id: head
            width: parent.width
            height: headContent.implicitHeight + 24

            Column {
                id: headContent
                x: 16
                y: 14
                width: parent.width - 32
                spacing: 5

                Text {
                    objectName: "inspectorAreaTitle"
                    width: parent.width
                    text: studio.areaTitle
                    color: Theme.text
                    font.family: Theme.ui
                    font.pixelSize: Theme.title
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: studio.areaDescription
                    color: Theme.secondary
                    font.family: Theme.ui
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                    lineHeight: 1.25
                }

                Item { width: 1; height: 3 }

                Segmented {
                    objectName: "layerSwitch"
                    visible: studio.area !== "assist" && studio.area !== "quick"
                    width: parent.width
                    height: 26
                    small: true
                    options: ["Personal", "Creator"]
                    currentIndex: studio.currentLayer === "creator" ? 1 : 0
                    onChosen: function(index) { studio.setLayer(index === 0 ? "personal" : "creator") }
                }
                Text {
                    width: parent.width
                    text: studio.layerDescription
                    color: studio.currentLayer === "creator" ? Theme.secondary : Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                }

                Row {
                    visible: studio.area !== "assist" && studio.area !== "construction" && studio.area !== "quick"
                    width: parent.width
                    spacing: 2
                    ActionButton { text: "Copy"; compact: true; onClicked: studio.copySettings() }
                    ActionButton { text: "Paste"; compact: true; onClicked: studio.pasteSettings() }
                    ActionButton { text: "Set initial"; compact: true; onClicked: studio.saveInitial() }
                    ActionButton { text: "Reset mine"; compact: true; onClicked: studio.resetPersonal() }
                    ActionButton { text: "Reset creator"; compact: true; onClicked: studio.resetCreator() }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.hairline
        }

        C.ScrollView {
            id: scroll
            objectName: "inspectorScroll"
            width: parent.width
            height: parent.height - head.height - hint.height - 1
            clip: true
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            C.ScrollBar.vertical: C.ScrollBar {
                policy: C.ScrollBar.AsNeeded
                width: 7
                contentItem: Rectangle {
                    radius: 3
                    color: Theme.meta
                    opacity: parent.pressed ? 0.9 : 0.45
                }
                background: Rectangle { color: "transparent" }
            }
            contentWidth: availableWidth

            Column {
                width: scroll.availableWidth
                topPadding: 12
                leftPadding: 14
                rightPadding: 14
                bottomPadding: 18
                spacing: 9

                ConstructionTree {
                    visible: studio.area === "construction"
                    width: parent.width - 28
                    onTipRequested: inspector.tipRequested()
                    onReuseRequested: inspector.reuseRequested()
                    onImportRequested: inspector.importRequested()
                }
                DynamicsPanel {
                    visible: studio.area === "dynamics"
                    width: parent.width - 28
                }

                Row {
                    visible: studio.area === "secondary"
                    width: parent.width - 28
                    spacing: 6
                    ActionButton {
                        text: "Add secondary brush"
                        icon: "brush"
                        onClicked: studio.addSecondary()
                    }
                    ActionButton {
                        text: "Reuse construction"
                        icon: "secondary"
                        onClicked: inspector.reuseRequested()
                    }
                }

                Repeater {
                    model: studio.sections
                    Item {
                        width: parent.width - 28
                        height: cardBody.implicitHeight + 20
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius
                            color: Theme.elevated
                            border.width: 1
                            border.color: Theme.innerHighlight
                        }
                        Column {
                            id: cardBody
                            x: 10
                            y: 10
                            width: parent.width - 20
                            spacing: 2
                            Text {
                                width: parent.width
                                text: modelData.title.toUpperCase()
                                color: Theme.meta
                                font.family: Theme.ui
                                font.pixelSize: Theme.micro
                                font.letterSpacing: 1.0
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: modelData.note.length > 0
                                width: parent.width
                                text: modelData.note
                                color: Theme.meta
                                font.family: Theme.ui
                                font.pixelSize: Theme.micro
                                wrapMode: Text.WordWrap
                                lineHeight: 1.2
                            }
                            Item { width: 1; height: 3 }
                            Repeater {
                                model: modelData.settings
                                SettingRow {
                                    width: cardBody.width
                                    info: modelData
                                }
                            }
                            Text {
                                visible: modelData.settings.length === 0 && modelData.title === "Pinned by this artist"
                                width: parent.width
                                text: "No pinned controls. Hover a setting and use its pin."
                                color: Theme.meta
                                font.family: Theme.ui
                                font.pixelSize: Theme.small
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Item {
                    visible: studio.area === "quick" && studio.sections.length > 0 && studio.sections[0].settings.length === 0
                    width: parent.width - 28
                    height: 120
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        text: "Quick Access holds the controls this artist pinned. Pins never alter the creator's brush definition."
                        color: Theme.meta
                        font.family: Theme.ui
                        font.pixelSize: Theme.small
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                ActionButton {
                    objectName: "showAdvanced"
                    visible: studio.area !== "assist" && studio.area !== "construction" && studio.area !== "quick"
                    text: studio.showAdvanced ? "Hide advanced settings" : "Show advanced settings"
                    icon: "chevron"
                    width: parent.width - 28
                    rotation: 0
                    onClicked: studio.toggleAdvanced()
                }

                Item { width: 1; height: 6 }
            }
        }

        Rectangle {
            id: hint
            width: parent.width
            height: 76
            color: Theme.track
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.hairline }

            Column {
                x: 14
                y: 9
                width: parent.width - 28
                spacing: 2
                Row {
                    width: parent.width
                    spacing: 6
                    Text {
                        text: studio.currentHint.label || ""
                        color: Theme.text
                        font.family: Theme.ui
                        font.pixelSize: Theme.small
                        font.weight: Font.Medium
                    }
                    Text {
                        text: studio.currentHint.scope || ""
                        color: Theme.meta
                        font.family: Theme.ui
                        font.pixelSize: Theme.micro
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item { width: Math.max(0, parent.width - 250); height: 1 }
                    Text {
                        text: studio.currentHint.badge || "AFFECTS PREVIEW"
                        color: studio.currentHint.rendered === false ? Theme.meta : Theme.secondary
                        font.family: Theme.ui
                        font.pixelSize: 7
                        font.letterSpacing: 0.6
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    width: parent.width
                    text: studio.currentHint.desc || "Hover a setting to see what it changes and who owns it."
                    color: Theme.meta
                    font.family: Theme.ui
                    font.pixelSize: Theme.micro
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.2
                }
                Text {
                    width: parent.width
                    text: (studio.currentHint.effect ? "Affects " + studio.currentHint.effect : "")
                        + (studio.currentHint.source ? "  ·  " + studio.currentHint.source + " layer" : "")
                    color: Theme.disabled
                    font.family: Theme.ui
                    font.pixelSize: 7
                    elide: Text.ElideRight
                }
            }
        }
    }
}
