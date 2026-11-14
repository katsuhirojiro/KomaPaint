import QtQuick
import QtQuick.Controls
import "../components"
import "../themes"

// A tab group: one header strip with the panel tabs, and the body of the
// active panel. Everything a panel needs to be part of the workspace — the
// title, the close button, collapsing, dragging and the panel menu — lives
// here, so a panel's own QML is only its content.
Item {
    id: group

    property var workspaceRoot: null
    property var node: ({})
    property int floatingId: -1

    readonly property var panels: node.panels !== undefined ? node.panels : []
    readonly property int groupId: node.id !== undefined ? node.id : -1
    readonly property bool collapsed: node.collapsed === true
    property int localActive: node.active !== undefined ? node.active : 0
    readonly property var model: group.workspaceRoot ? group.workspaceRoot.model : null
    readonly property bool registered: group.floatingId < 0 && group.groupId > 0
    readonly property int activeIndex:
        group.panels.length === 0 ? -1 : Math.max(0, Math.min(group.localActive, group.panels.length - 1))
    // Drop-target interface used by the workspace.
    readonly property bool canvasTarget: false
    readonly property int targetId: group.groupId
    readonly property string activePanelId: group.activeIndex >= 0 ? group.panels[group.activeIndex] : ""

    objectName: "dockGroup" + (group.registered ? group.groupId : "f" + group.floatingId)

    // How small this group may get before its content would be cut off. A
    // panel may publish its own preferredHeight; otherwise a small default
    // keeps the header and a usable strip of content visible.
    readonly property real contentPreferredHeight:
        (content.item && content.item.preferredHeight !== undefined) ? content.item.preferredHeight : 64
    readonly property real minimumBodyWidth: 118
    readonly property real minimumBodyHeight: group.collapsed ? KP.panelHeaderH : KP.panelHeaderH + group.contentPreferredHeight + 2

    function minimumOf(horizontal) {
        return horizontal ? group.minimumBodyWidth : group.minimumBodyHeight
    }

    // A panel's preferred size is only known once its content is loaded (and
    // changes when another tab becomes active or the group collapses), so the
    // layout has to be re-run instead of relying on bindings.
    function requestRelayout() {
        if (group.workspaceRoot && typeof group.workspaceRoot.relayoutTree === "function")
            group.workspaceRoot.relayoutTree()
    }

    onCollapsedChanged: group.requestRelayout()

    function titleOf(panelId) {
        return group.model ? group.model.title(panelId) : panelId
    }

    function activate(index) {
        if (index < 0 || index >= group.panels.length) return
        group.localActive = index
        if (group.model) group.model.setActive(group.panels[index])
    }

    function closePanel(panelId) {
        if (!group.model) return
        group.model.hidePanel(panelId)
    }

    function floatActivePanel() {
        if (!group.model || group.activeIndex < 0) return
        const at = body.mapToGlobal(24, 24)
        group.model.floatPanel(group.activePanelId, Math.round(at.x), Math.round(at.y), 330, 280)
    }

    function toggleCollapsed() {
        if (!group.model || group.activeIndex < 0) return
        group.model.setCollapsed(group.activePanelId, !group.collapsed)
    }

    function startDrag() {
        if (!group.workspaceRoot || group.activeIndex < 0) return
        group.workspaceRoot.beginDrag(group.activePanelId)
    }

    // Where a tab dropped at this x (in group coordinates) would be inserted.
    function tabDropIndexFor(x) {
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (!tab) continue
            if (x < tab.x + tab.width / 2) return i
        }
        return group.panels.length
    }

    function tabInsertionX(index) {
        if (tabRepeater.count === 0) return 1
        if (index >= tabRepeater.count) {
            const last = tabRepeater.itemAt(tabRepeater.count - 1)
            return last ? last.x + last.width : 1
        }
        const tab = tabRepeater.itemAt(index)
        return tab ? tab.x : 1
    }

    function rectInWorkspace() {
        if (!group.workspaceRoot) return Qt.rect(0, 0, 0, 0)
        const at = group.mapToItem(group.workspaceRoot, 0, 0)
        return Qt.rect(at.x, at.y, group.width, group.height)
    }

    function bodyRectInWorkspace() {
        if (!group.workspaceRoot) return Qt.rect(0, 0, 0, 0)
        const at = body.mapToItem(group.workspaceRoot, 0, 0)
        return Qt.rect(at.x, at.y, body.width, body.height)
    }

    Component.onCompleted: {
        if (group.registered && group.workspaceRoot) group.workspaceRoot.registerTarget(group)
    }
    Component.onDestruction: {
        if (group.registered && group.workspaceRoot) group.workspaceRoot.unregisterTarget(group)
    }
    onPanelsChanged: {
        if (group.localActive >= group.panels.length) group.localActive = Math.max(0, group.panels.length - 1)
    }
    Connections {
        target: group.model
        function onActiveTabChanged(panelId) {
            const index = group.panels.indexOf(panelId)
            if (index >= 0) group.localActive = index
        }
    }

    Column {
        anchors.fill: parent
        // No gap: an active tab has to sit flush on the panel body.
        spacing: 0

        // ---- header -------------------------------------------------------
        // Tabs, not a title bar. The active tab is filled with the panel's own
        // surface and marked by an ember rule along its top edge, so it reads as
        // the front sheet of a stack; the others are quiet text on the strip.
        // Pressing anywhere that is not a tab drags the panel, and a double click
        // collapses the group.
        Rectangle {
            id: header
            objectName: "dockHeader" + (group.registered ? group.groupId : "f" + group.floatingId)
            width: parent.width
            height: KP.panelHeaderH
            color: KP.paneHeaderBg
            clip: true

            // Drawn before the tabs so the active tab can cover it and merge
            // with the panel body below.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }

            // A press anywhere in the header that is not on a tab drags the
            // active panel; a double click collapses the group.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onPressed: {
                    const at = mapToGlobal(mouse.x, mouse.y)
                    headerPressX = at.x
                    headerPressY = at.y
                    headerPressed = true
                }
                property real headerPressX: 0
                property real headerPressY: 0
                property bool headerPressed: false
                property bool headerDragging: false
                onPositionChanged: {
                    if (!headerPressed) return
                    const at = mapToGlobal(mouse.x, mouse.y)
                    if (!headerDragging
                        && (Math.abs(at.x - headerPressX) > 6 || Math.abs(at.y - headerPressY) > 6)) {
                        headerDragging = true
                        group.startDrag()
                    }
                    if (headerDragging && group.workspaceRoot)
                        group.workspaceRoot.dragTo(at.x, at.y)
                }
                onReleased: {
                    headerPressed = false
                    if (headerDragging) {
                        headerDragging = false
                        if (group.workspaceRoot) group.workspaceRoot.endDrag()
                    }
                }
                onCanceled: {
                    headerPressed = false
                    if (headerDragging) {
                        headerDragging = false
                        if (group.workspaceRoot) group.workspaceRoot.cancelDrag()
                    }
                }
                onDoubleClicked: group.toggleCollapsed()
            }

            Row {
                id: tabRow
                anchors.left: parent.left
                anchors.leftMargin: KP.gapXS
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 0

                Repeater {
                    id: tabRepeater
                    model: group.panels
                    delegate: Rectangle {
                        id: tab
                        property int tabIndex: index
                        property bool tabActive: tab.tabIndex === group.activeIndex
                        property bool tabHovered: tabMouse.containsMouse

                        width: Math.min(180, Math.max(58, tabLabel.implicitWidth + (tab.tabActive ? 44 : 28)))
                        height: tabRow.height
                        color: tab.tabActive ? KP.paneBg
                             : tab.tabHovered ? KP.hoverOverlay
                             : "transparent"
                        objectName: "dockTab_" + modelData

                        // Ember rule: the active tab is the front sheet.
                        Rectangle {
                            visible: tab.tabActive
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 2
                            color: KP.ember
                        }
                        Rectangle {
                            visible: tab.tabActive
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: -1
                            height: 1
                            color: KP.paneBg
                        }

                        Text {
                            id: tabLabel
                            anchors.left: parent.left
                            anchors.leftMargin: tab.tabActive ? KP.gapM : KP.gapM - 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, 132)
                            text: group.titleOf(modelData)
                            color: tab.tabActive ? KP.text : tab.tabHovered ? KP.text : KP.textMuted
                            font.family: KP.fontUi
                            font.pixelSize: KP.fontS
                            font.weight: tab.tabActive ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                        }

                        // Close: always on the active tab, on hover otherwise.
                        Item {
                            anchors.right: parent.right
                            anchors.rightMargin: KP.gapS
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            // Present on every tab (so it is always a click
                            // target) but only drawn on the active or hovered
                            // one, which keeps the strip calm.
                            opacity: tab.tabActive || tab.tabHovered ? 1 : 0
                            // Above the tab's own mouse area, or the × is unreachable.
                            z: 1
                            objectName: "dockTabClose_" + modelData
                            KpGlyph {
                                anchors.centerIn: parent
                                name: "close"
                                size: 10
                                color: closeMouse.containsMouse ? KP.iconHover : KP.iconOff
                            }
                            MouseArea {
                                id: closeMouse
                                anchors.fill: parent
                                anchors.margins: -3
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: group.closePanel(modelData)
                            }
                            ToolTip.delay: 400
                            ToolTip.visible: closeMouse.containsMouse
                            ToolTip.text: qsTr("Hide %1").arg(group.titleOf(modelData))
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Hide %1").arg(group.titleOf(modelData))
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            property real pressX: 0
                            property real pressY: 0
                            property bool tabDragging: false

                            onPressed: function (mouse) {
                                pressX = mouse.x
                                pressY = mouse.y
                                tabDragging = false
                                group.activate(tab.tabIndex)
                                if (mouse.button === Qt.RightButton) {
                                    tabMenu.panel = modelData
                                    tabMenu.popup()
                                }
                            }
                            onPositionChanged: function (mouse) {
                                if (!pressed) return
                                if (!tabDragging
                                    && (Math.abs(mouse.x - pressX) > 6 || Math.abs(mouse.y - pressY) > 6)) {
                                    tabDragging = true
                                    group.startDrag()
                                }
                                if (tabDragging && group.workspaceRoot) {
                                    const at = mapToGlobal(mouse.x, mouse.y)
                                    group.workspaceRoot.dragTo(at.x, at.y)
                                }
                            }
                            onReleased: {
                                if (!tabDragging) return
                                tabDragging = false
                                if (group.workspaceRoot) group.workspaceRoot.endDrag()
                            }
                            onCanceled: {
                                if (!tabDragging) return
                                tabDragging = false
                                if (group.workspaceRoot) group.workspaceRoot.cancelDrag()
                            }
                        }
                    }
                }
            }

            // ---- header commands -------------------------------------------
            Row {
                anchors.right: parent.right
                anchors.rightMargin: KP.gapXS
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Item {
                    objectName: "dockCollapse" + (group.registered ? group.groupId : "f" + group.floatingId)
                    width: 20
                    height: KP.panelHeaderH
                    KpGlyph {
                        anchors.centerIn: parent
                        name: group.collapsed ? "chevron-right" : "chevron-down"
                        size: 11
                        color: collapseMouse.containsMouse ? KP.iconHover : KP.iconOff
                    }
                    ToolTip.delay: 400
                    ToolTip.visible: collapseMouse.containsMouse
                    ToolTip.text: group.collapsed
                                  ? qsTr("Expand %1").arg(group.titleOf(group.activePanelId))
                                  : qsTr("Collapse %1").arg(group.titleOf(group.activePanelId))
                    MouseArea {
                        id: collapseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: group.toggleCollapsed()
                    }
                }
            }
        }

        // ---- body ---------------------------------------------------------
        DockFrame {
            id: body
            objectName: "dockBody" + (group.registered ? group.groupId : "f" + group.floatingId)
            width: parent.width
            height: group.collapsed ? 0 : Math.max(0, parent.height - header.height - parent.spacing)
            visible: !group.collapsed
            // A docked panel is part of the workspace surface, not a box on it.
            framed: false

            Loader {
                id: content
                anchors.fill: parent
                objectName: "dockContent_" + group.activePanelId
                sourceComponent: group.workspaceRoot && group.activePanelId.length > 0
                                     ? group.workspaceRoot.componentFor(group.activePanelId)
                                     : null
                onItemChanged: group.requestRelayout()
            }

            // Bottom edge: a panel whose content is taller than its body is cut
            // against this rule, so the fold reads as the end of the panel
            // rather than as text running into the next group's header.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }
        }
    }

    KpMenu {
        id: tabMenu
        objectName: "dockPanelMenu"
        property string panel: ""

        KpMenuItem {
            text: qsTr("Float panel")
            enabled: tabMenu.panel.length > 0 && group.floatingId < 0
            onTriggered: {
                if (!group.model) return
                const at = group.mapToGlobal(24, KP.panelHeaderH + 8)
                group.model.floatPanel(tabMenu.panel, Math.round(at.x), Math.round(at.y), 330, 280)
            }
        }
        KpMenuSeparator { }
        KpMenuItem {
            text: qsTr("Dock left")
            enabled: tabMenu.panel.length > 0
            onTriggered: group.model.dockToWorkspace(tabMenu.panel, "left")
        }
        KpMenuItem {
            text: qsTr("Dock right")
            enabled: tabMenu.panel.length > 0
            onTriggered: group.model.dockToWorkspace(tabMenu.panel, "right")
        }
        KpMenuItem {
            text: qsTr("Dock bottom")
            enabled: tabMenu.panel.length > 0
            onTriggered: group.model.dockToWorkspace(tabMenu.panel, "bottom")
        }
        KpMenuSeparator { }
        KpMenuItem {
            text: qsTr("Dock into workspace")
            visible: group.floatingId >= 0
            onTriggered: if (group.model) group.model.dockBack(group.floatingId)
        }
        KpMenuItem {
            text: qsTr("Close panel")
            enabled: tabMenu.panel.length > 0
            onTriggered: group.closePanel(tabMenu.panel)
        }
    }
}
