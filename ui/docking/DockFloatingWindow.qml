import QtQuick
import QtQuick.Window
import "../themes"

// A panel floated out of the workspace: an ordinary top-level window, so the
// operating system gives it a title bar, moving, resizing and stacking for
// free. Its geometry is stored in the layout model and restored on the next
// start; docking it back is a single call.
Window {
    id: window

    property var workspaceRoot: null
    property var entry: ({})
    readonly property int floatingId: entry.id !== undefined ? entry.id : -1
    readonly property var panels: entry.panels !== undefined ? entry.panels : []
    readonly property var model: workspaceRoot ? workspaceRoot.model : null

    objectName: "dockFloatingWindow" + floatingId
    title: model && panels.length > 0 ? model.title(panels[0]) : qsTr("Panel")
    flags: Qt.Window
    color: KP.windowBg
    visible: true
    width: entry.width !== undefined ? entry.width : 330
    height: entry.height !== undefined ? entry.height : 280
    x: entry.x !== undefined ? entry.x : 120
    y: entry.y !== undefined ? entry.y : 120
    minimumWidth: 220
    minimumHeight: 140

    property bool geometryReady: false
    Component.onCompleted: geometryReady = true

    function storeGeometry() {
        if (!geometryReady || !model || floatingId < 0) return
        model.setFloatingGeometry(floatingId, Math.round(window.x), Math.round(window.y),
                                  Math.round(window.width), Math.round(window.height))
    }
    onXChanged: storeGeometry()
    onYChanged: storeGeometry()
    onWidthChanged: storeGeometry()
    onHeightChanged: storeGeometry()
    onClosing: function (close) {
        // Closing the OS window puts the panels back in the workspace instead
        // of destroying them: a panel can never be lost this way.
        close.accepted = false
        if (model && floatingId >= 0) model.dockBack(floatingId)
    }

    // The operating system draws the title bar and the resize edges; this is the
    // single hairline that makes the panel's own surface read as a window body.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: KP.border
        z: 10
        // Clicks belong to the panel underneath, except on the edge itself.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            hoverEnabled: false
        }
    }

    DockGroup {
        anchors.fill: parent
        anchors.margins: 1
        workspaceRoot: window.workspaceRoot
        floatingId: window.floatingId
        node: ({ "kind": "tabs", "panels": window.panels,
                 "active": window.entry.active !== undefined ? window.entry.active : 0,
                 "collapsed": false })
    }
}
