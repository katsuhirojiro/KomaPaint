import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C
import ReviewArt 1.0

C.Dialog {
    id: root
    objectName: "tipRoom"
    property string target: studio.addTo.uid
    property bool replace: false
    modal: true
    width: Math.min(530, Window.width - 80)
    padding: 0
    closePolicy: C.Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: 2
    }
    function center() {
        const pw = parent && parent.width > 20 ? parent.width : Window.width;
        const ph = parent && parent.height > 20 ? parent.height : Window.height;
        width = Math.min(548, pw - 70);
        height = Math.min(474, ph - 50);
        x = Math.max(0, (pw - width) / 2);
        y = Math.max(0, (ph - height) / 2);
    }
    onAboutToShow: center()
    onOpened: surface.forceActiveFocus()

    contentItem: Item {
        Rectangle {
            anchors.fill: parent
            color: Theme.panel
            border.color: Theme.border
            radius: 2
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 33
                color: Theme.chrome
                Rule {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.seam
                }
                LabelText {
                    anchors.left: parent.left
                    anchors.margins: 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Draw a Tip"
                    font.weight: Font.DemiBold
                    font.pixelSize: Theme.label
                }
                Command {
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "close"
                    hint: "Close"
                    quiet: true
                    onClicked: root.close()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 11
                spacing: 11
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 5
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#222224"
                        border.color: "#555"
                        clip: true
                        Artwork {
                            anchors.fill: parent
                            anchors.margins: 1
                            kind: "tip"
                        }
                        MouseArea {
                            id: surface
                            objectName: "tipSurface"
                            anchors.fill: parent
                            anchors.margins: 1
                            cursorShape: Qt.CrossCursor
                            activeFocusOnTab: true
                            Accessible.name: "Tip drawing surface"
                            onPressed: mouse => studio.beginLine("tip", mouse.x / width, mouse.y / height)
                            onPositionChanged: mouse => {
                                if (pressed)
                                    studio.point("tip", mouse.x / width, mouse.y / height);
                            }
                            onReleased: studio.endLine("tip")
                            onCanceled: studio.endLine("tip")
                        }
                        LabelText {
                            anchors.centerIn: parent
                            text: "Draw one mark, or several"
                            color: "#727272"
                            visible: !studio.tipOptions.ink
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Command {
                            objectName: "tipUndo"
                            glyph: "undo"
                            hint: "Undo tip stroke"
                            quiet: true
                            enabled: studio.tipOptions.undo
                            onClicked: studio.inkAction("tip", "undo")
                        }
                        Command {
                            objectName: "tipRedo"
                            glyph: "redo"
                            hint: "Redo tip stroke"
                            quiet: true
                            enabled: studio.tipOptions.redo
                            onClicked: studio.inkAction("tip", "redo")
                        }
                        Command {
                            text: "Clear"
                            quiet: true
                            enabled: studio.tipOptions.ink
                            onClicked: studio.inkAction("tip", "clear")
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        LabelText {
                            text: "Tip size"
                            color: Theme.mid
                        }
                        EditField {
                            Layout.preferredWidth: 40
                            text: String(studio.tipOptions.size)
                            horizontalAlignment: TextInput.AlignRight
                            validator: IntValidator {
                                bottom: 1
                                top: 84
                            }
                            onEditingFinished: {
                                if (text.length)
                                    studio.tipSize(Number(text));
                                text = Qt.binding(() => String(studio.tipOptions.size));
                            }
                        }
                        LabelText {
                            text: "px"
                            color: Theme.muted
                            font.pixelSize: 10
                        }
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 168
                    Layout.fillHeight: true
                    spacing: 6
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 128
                        color: "#252525"
                        border.color: "#4f4f4f"
                        Artwork {
                            anchors.fill: parent
                            anchors.margins: 4
                            kind: "tipPreview"
                        }
                        LabelText {
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Stamp preview"
                            color: Theme.muted
                            font.pixelSize: 9
                        }
                    }
                    LabelText {
                        text: "It becomes one mark. You can add more marks, masks, or textures around it."
                        color: Theme.mid
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    LabelText {
                        text: "Name"
                        color: Theme.mid
                        font.pixelSize: 10
                        Layout.topMargin: 4
                    }
                    EditField {
                        id: name
                        objectName: "tipName"
                        Layout.fillWidth: true
                        text: "Hand-drawn mark"
                        placeholderText: "Mark name"
                        Accessible.name: "Mark name"
                    }
                    CheckRow {
                        Layout.fillWidth: true
                        visible: studio.chosen.kind === "mark"
                        text: "Replace selected mark"
                        checked: root.replace
                        onToggled: root.replace = checked
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                    Command {
                        Layout.fillWidth: true
                        text: "Start Over"
                        quiet: true
                        onClicked: {
                            studio.newTip();
                            name.text = "Hand-drawn mark";
                        }
                    }
                    Command {
                        objectName: "tipAdd"
                        Layout.fillWidth: true
                        text: "Add as a Mark"
                        strong: true
                        enabled: studio.tipOptions.ready
                        onClicked: {
                            studio.acceptTip(name.text, root.target, root.replace ? studio.chosen.uid : "");
                            root.close();
                        }
                    }
                }
            }
        }
    }
}
