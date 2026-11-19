import QtQuick
import QtQuick.Controls
import "../../components"
import "../../themes"

ComboBox {
    id: box
    implicitHeight: KP.controlHeight
    padding: KP.gapM
    rightPadding: KP.gapM + 16
    font.family: KP.fontUi
    font.pixelSize: KP.fontS
    opacity: enabled ? 1 : KP.disabledOpacity
    contentItem: Text {
        text: box.displayText
        textFormat: Text.PlainText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        color: KP.text
        font: box.font
    }
    indicator: KpGlyph {
        x: box.width - width - KP.gapM
        y: (box.height - height) / 2
        name: "chevron-down"
        size: 12
    }
    background: Rectangle {
        radius: KP.radiusS
        color: box.down ? KP.inputBgHover : KP.inputBg
        border.width: 1
        border.color: box.activeFocus ? KP.focusRing : box.hovered ? KP.borderLight : KP.border
    }
    delegate: ItemDelegate {
        id: entry
        required property int index
        required property var modelData
        objectName: box.objectName + "_option_" + index
        width: ListView.view ? ListView.view.width : 0
        implicitHeight: KP.controlHeight
        highlighted: box.highlightedIndex === index
        contentItem: Text {
            text: box.textRole.length > 0 ? entry.modelData[box.textRole] : entry.modelData
            textFormat: Text.PlainText
            color: KP.text
            font: box.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { color: entry.highlighted ? KP.accentWash : "transparent" }
    }
    popup: Popup {
        y: box.height
        width: box.width
        padding: KP.gapXS
        topMargin: KP.gapXS
        bottomMargin: KP.gapXS
        // Size the popup before the visible list creates its delegates.
        implicitHeight: Math.min(280, box.count * KP.controlHeight + 2 * padding)
        background: Rectangle {
            color: KP.floatingBg
            border.width: KP.hairlineWidth
            border.color: KP.border
            radius: KP.radiusS
        }
        contentItem: ListView {
            id: list
            clip: true
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar { opacity: size < 1 ? 1 : 0 }
        }
    }
}
