import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

Rectangle {
    color: KP.paneBg
    border.color: KP.border
    border.width: 1
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        PanelHeader {
            Layout.fillWidth: true
            title: "Properties"
            onCloseClicked: Panels.setVisible("properties", false)
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            GroupBox {
                Layout.fillWidth: true
                title: "Document"
                palette.windowText: KP.textMuted
                font.pixelSize: KP.fontS
                background: Rectangle {
                    color: KP.paneBgAlt
                    border.color: KP.border
                    radius: KP.radiusS
                }
                ColumnLayout {
                    anchors.fill: parent
                    RowLayout {
                        Label {
                            text: "Title"
                            color: KP.text
                            font.pixelSize: KP.fontS
                            font.family: KP.fontUi
                            Layout.preferredWidth: 60
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: Workspace.document.title
                            onTextEdited: Workspace.document.title = text
                            color: KP.text
                            font.family: KP.fontUi
                            font.pixelSize: KP.fontS
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                        }
                    }
                    RowLayout {
                        Label {
                            text: "W"
                            color: KP.text
                            font.pixelSize: KP.fontS
                            font.family: KP.fontUi
                            Layout.preferredWidth: 60
                        }
                        SpinBox {
                            id: wSpin
                            from: 1
                            to: 16384
                            value: Workspace.document.width
                            editable: true
                            onValueModified: Workspace.document.width = value
                            font.pixelSize: KP.fontS
                            font.family: KP.fontMono
                            palette.text: KP.text
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                        }
                        Label {
                            text: "H"
                            color: KP.text
                            font.pixelSize: KP.fontS
                            font.family: KP.fontUi
                        }
                        SpinBox {
                            from: 1
                            to: 16384
                            value: Workspace.document.height
                            editable: true
                            onValueModified: Workspace.document.height = value
                            Layout.fillWidth: true
                            font.pixelSize: KP.fontS
                            font.family: KP.fontMono
                            palette.text: KP.text
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                        }
                    }
                    RowLayout {
                        Label {
                            text: "DPI"
                            color: KP.text
                            font.pixelSize: KP.fontS
                            font.family: KP.fontUi
                            Layout.preferredWidth: 60
                        }
                        SpinBox {
                            from: 1
                            to: 2400
                            value: Math.round(Workspace.document.dpi)
                            stepSize: 10
                            editable: true
                            onValueModified: Workspace.document.dpi = value
                            Layout.fillWidth: true
                            font.pixelSize: KP.fontS
                            font.family: KP.fontMono
                            palette.text: KP.text
                            background: Rectangle {
                                color: KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                            }
                        }
                    }
                    Row {
                        spacing: 6
                        CheckBox {
                            text: "Transparent background"
                            checked: Workspace.document.hasTransparentBackground
                            onToggled: Workspace.document.setTransparentBackground(checked)
                            contentItem: Label {
                                text: "Transparent"
                                color: KP.text
                                font.pixelSize: KP.fontS
                                font.family: KP.fontUi
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 18
                            }
                            indicator: Rectangle {
                                x: 0
                                y: 3
                                width: 12
                                height: 12
                                color: parent.checked ? KP.accent : KP.inputBg
                                border.color: KP.borderLight
                                radius: KP.radiusS
                                border.width: 1
                            }
                        }
                    }
                }
            }
        }
    }
}
