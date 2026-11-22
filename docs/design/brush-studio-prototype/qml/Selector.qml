import QtQuick

DeskButton {
    id: root
    property var options: []
    property string current: ""
    property string prefix: ""
    signal chosen(string chosen)
    text: prefix + current
    rightIcon: "down"
    textSize: 12
    size: 39
    onClicked: menu.open()
    Flyout {
        id: menu
        parent: root; y: root.height + 4; width: Math.max(137, root.width)
        options: root.options.map(key=>({key:key,label:key,icon:key===root.current?"check":""}))
        onChosen: key => root.chosen(key)
    }
}
