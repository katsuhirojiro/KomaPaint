import QtQuick
import QtQuick.Controls.Basic as C

C.Menu {
    width: Math.max(204, implicitContentWidth + leftPadding + rightPadding)
    padding: 4
    margins: 6
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 2 }
}
