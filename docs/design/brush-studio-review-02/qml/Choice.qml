import QtQuick
import QtQuick.Controls.Basic as C

C.ComboBox {
    id: control
    implicitHeight: 26
    implicitWidth: 126
    font.family: Theme.family; font.pixelSize: Theme.normal
    leftPadding: 8; rightPadding: 23
    opacity: enabled ? 1 : .5
    Accessible.name: currentText
    contentItem: LabelText { text: control.displayText; font: control.font }
    background: Rectangle {
        color: control.hovered ? Theme.hover : Theme.control; radius: 2
        border.color: control.visualFocus ? Theme.focus : Theme.border
    }
    indicator: Glyph {
        x: control.width - 22; anchors.verticalCenter: parent.verticalCenter
        width: 16; height: 16; name: "down"
    }
    delegate: C.ItemDelegate {
        id: row
        required property int index
        required property var modelData
        width: ListView.view.width; height: 29
        objectName: control.objectName + "Option" + index
        highlighted: control.highlightedIndex === index
        text: String(modelData)
        contentItem: LabelText { text: row.text }
        background: Rectangle { color: row.highlighted ? Theme.itemSelected : "transparent" }
    }
    popup: C.Popup {
        y: control.height; width: control.width
        implicitHeight: contentItem.implicitHeight + 2
        padding: 1
        margins: 8
        background: Rectangle { color: Theme.panel; border.color: Theme.border }
        contentItem: ListView {
            implicitHeight: contentHeight
            clip: true; model: control.delegateModel
            currentIndex: control.highlightedIndex
        }
    }
}
