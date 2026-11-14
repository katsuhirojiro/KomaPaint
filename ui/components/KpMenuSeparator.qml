import QtQuick
import QtQuick.Controls
import "../themes"

// A group break in a menu: a hairline that does not span the full width, so it
// reads as a pause rather than a table rule.
MenuSeparator {
    id: separator
    padding: 0
    leftPadding: KP.gapM
    rightPadding: KP.gapM
    implicitHeight: KP.gapM + 1

    contentItem: Rectangle {
        implicitHeight: 1
        height: 1
        color: KP.hairline
    }
}
