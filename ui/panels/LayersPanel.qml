import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../themes"

// The live raster stack. Unsupported layer types and compositing controls
// remain disabled until the document model can represent them safely.
Rectangle {
    id: panel
    color: KP.paneBg

    readonly property int active: Workspace.layers.activeLayerIndex
    readonly property var activeLayer: layerList.count > 0 ? Workspace.layers.layerAt(active) : null
    readonly property int barHeight: 28

    property int thumbnailSize: 32
    property bool thumbnailWheelEnabled: true
    readonly property var thumbnailPresets: [24, 32, 48]
    readonly property var labelColors: ["transparent", "#ba9871", "#a591aa", "#8aa18f", "#ad8c82", "#8198a0", "#c0b38e", "#9b9b9b"]
    readonly property var labelNames: [qsTr("No label"), qsTr("Ochre"), qsTr("Violet"), qsTr("Green"), qsTr("Rose"), qsTr("Blue"), qsTr("Sand"), qsTr("Grey")]
    function setThumbnailSize(size) {
        if (thumbnailPresets.indexOf(size) < 0 || thumbnailSize === size) return
        const first = layerList.indexAt(2, layerList.contentY + 2)
        const offset = first >= 0 ? layerList.contentY - first * (thumbnailSize + 8) : 0
        thumbnailSize = size
        Workspace.setSessionValue("ui.layers.thumbnailSize", size)
        if (first >= 0) Qt.callLater(function() {
            layerList.contentY = Math.max(0, Math.min(first * (thumbnailSize + 8) + offset,
                                                     layerList.contentHeight - layerList.height))
        })
    }
    function setThumbnailWheelEnabled(enabled) {
        thumbnailWheelEnabled = enabled
        Workspace.setSessionValue("ui.layers.thumbnailWheel", enabled)
    }
    Component.onCompleted: {
        const size = Number(Workspace.sessionValue("ui.layers.thumbnailSize", 32))
        if (thumbnailPresets.indexOf(size) >= 0) thumbnailSize = size
        const enabled = Workspace.sessionValue("ui.layers.thumbnailWheel", true)
        thumbnailWheelEnabled = enabled === true || enabled === "true" || enabled === 1 || enabled === "1"
    }

    component BarRule: Rectangle {
        Layout.leftMargin: KP.gapXS
        Layout.rightMargin: KP.gapXS
        implicitWidth: 1
        implicitHeight: 14
        color: KP.hairline
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Attributes of the selected layer
        Item {
            Layout.fillWidth: true
            implicitHeight: panel.barHeight

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapS
                spacing: 0

                ComboBox {
                    id: blendMode
                    objectName: "layerBlendMode"
                    Layout.preferredWidth: 94
                    Layout.preferredHeight: 24
                    model: [qsTr("Normal"), qsTr("Multiply"), qsTr("Screen"), qsTr("Overlay")]
                    currentIndex: panel.activeLayer ? panel.activeLayer.blendMode : 0
                    enabled: panel.activeLayer !== null
                    Accessible.name: qsTr("Layer blend mode")
                    onActivated: function(index) { if (panel.activeLayer) panel.activeLayer.blendMode = index }
                    contentItem: Text {
                        leftPadding: KP.gapS
                        rightPadding: 16
                        text: blendMode.displayText
                        color: KP.text
                        font.pixelSize: KP.fontS
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: KP.inputBg
                        border.width: 1
                        border.color: blendMode.activeFocus ? KP.focusRing : KP.border
                    }
                    indicator: KpGlyph {
                        x: blendMode.width - width - 5
                        y: (blendMode.height - height) / 2
                        name: "chevron-down"
                        size: 10
                        color: KP.iconNormal
                    }
                }
                IconButton {
                    objectName: "layerLockButton"
                    cell: 22
                    iconSize: 16
                    iconName: "layers/lock"
                    tooltip: selected ? qsTr("Unlock layer") : qsTr("Lock layer")
                    enabled: panel.activeLayer !== null
                    selected: panel.activeLayer !== null && panel.activeLayer.locked
                    accentBar: true
                    onActivated: panel.activeLayer.locked = !panel.activeLayer.locked
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/lock-transparency"
                    tooltip: qsTr("Lock transparent pixels")
                    available: false
                }
                Item { Layout.fillWidth: true }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: KP.hairline
            }
        }

        // The slider always shows the selected layer's opacity; it is not brush opacity.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.panelInset
            Layout.preferredHeight: 32
            spacing: KP.gapM
            Slider {
                id: layerOpacitySlider
                objectName: "layerOpacitySlider"
                Layout.fillWidth: true
                from: 0; to: 100; stepSize: 1
                value: panel.activeLayer ? Math.round(panel.activeLayer.opacity * 100) : 0
                enabled: panel.activeLayer !== null
                Accessible.name: qsTr("Layer opacity")
                ToolTip.text: qsTr("Layer opacity")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                property var editingLayer: null
                onPressedChanged: if (pressed) editingLayer = panel.activeLayer
                onMoved: {
                    // Keyboard moves have no press; a drag must stay on the layer
                    // it started on even if selection changes while it is held.
                    const target = pressed ? editingLayer : panel.activeLayer
                    if (target && target === panel.activeLayer) target.opacity = value / 100
                }
                background: Rectangle {
                    x: layerOpacitySlider.leftPadding
                    y: layerOpacitySlider.topPadding + layerOpacitySlider.availableHeight / 2 - height / 2
                    width: layerOpacitySlider.availableWidth; height: 3
                    color: KP.borderLight
                    Rectangle { width: layerOpacitySlider.visualPosition * parent.width; height: parent.height; color: KP.text }
                }
                handle: Rectangle {
                    x: layerOpacitySlider.leftPadding + layerOpacitySlider.visualPosition * (layerOpacitySlider.availableWidth - width)
                    y: layerOpacitySlider.topPadding + layerOpacitySlider.availableHeight / 2 - height / 2
                    width: 10; height: 10; radius: 5
                    color: layerOpacitySlider.enabled ? KP.text : KP.textSubtle
                    border.width: layerOpacitySlider.activeFocus ? 2 : 0
                    border.color: KP.focusRing
                }
            }
            KpNumberField {
                id: layerOpacityField
                objectName: "layerOpacityField"
                Layout.preferredWidth: 58
                Layout.preferredHeight: 24
                enabled: panel.activeLayer !== null
                value: panel.activeLayer ? Math.round(panel.activeLayer.opacity * 100) : 0
                from: 0; to: 100; unit: "%"
                Accessible.name: qsTr("Layer opacity percentage")
                property var editingLayer: null
                onEditingChanged: if (editing) editingLayer = panel.activeLayer
                onEdited: function(v) {
                    if (editingLayer && editingLayer === panel.activeLayer) editingLayer.opacity = v / 100
                }
            }
        }

        // ---- The stack
        ListView {
            id: layerList
            objectName: "layerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Workspace.layers
            clip: true
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: Workspace.layers.activeLayerIndex
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded }

            delegate: AbstractButton {
                id: row
                required property int index
                readonly property bool current: index === layerList.currentIndex
                // The row is one Layer object, read through the model: role
                // names would collide with Item's own `visible` and `opacity`,
                // and the Layer notifies for each of these, so the row follows
                // renames, hiding, locking and opacity without a refresh.
                readonly property var layerObject: Workspace.layers.layerAt(index)
                readonly property string layerName: layerObject ? layerObject.name : ""
                readonly property bool layerVisible: layerObject ? layerObject.visible : true
                readonly property bool layerLocked: layerObject ? layerObject.locked : false
                readonly property int labelIndex: layerObject ? layerObject.colorLabel : 0
                readonly property real layerOpacity: layerObject ? layerObject.opacity : 1
                property bool renaming: false
                function startRename() {
                    Workspace.layers.setActiveLayerIndex(index)
                    renaming = true
                    renameField.text = layerName
                    Qt.callLater(function() {
                        if (!row.renaming) return
                        renameField.forceActiveFocus()
                        renameField.selectAll()
                    })
                }
                function finishRename(commit) {
                    if (!renaming) return
                    const name = renameField.text.trim()
                    renaming = false
                    if (commit && name.length > 0 && layerObject) layerObject.name = name
                }

                width: layerList.width
                height: panel.thumbnailSize + 8
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: row.layerName
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_F2) { row.startRename(); event.accepted = true }
                }
                onClicked: Workspace.layers.setActiveLayerIndex(row.index)

                background: Rectangle {
                    color: row.current ? KP.rowSelected
                         : row.hovered ? KP.hoverOverlay : (row.index % 2 === 0 ? "transparent" : KP.hoverOverlay)
                    Rectangle {
                        visible: row.current
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 2
                        color: KP.ember
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: KP.hairline
                        opacity: 0.55
                    }
                }

                contentItem: Item {
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: KP.panelInset
                        anchors.rightMargin: KP.gapS
                        spacing: KP.gapS

                        AbstractButton {
                            id: labelButton
                            objectName: "layerLabel" + row.index
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: Math.max(24, panel.thumbnailSize)
                            padding: 0
                            Accessible.name: qsTr("Layer colour label: %1").arg(panel.labelNames[row.labelIndex])
                            ToolTip.text: Accessible.name
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            onClicked: labelMenu.open()
                            background: Rectangle {
                                x: 3; y: 2; width: 8; height: parent.height - 4
                                color: row.labelIndex ? panel.labelColors[row.labelIndex] : KP.paneBgAlt
                                border.width: labelButton.activeFocus ? 1 : 0
                                border.color: KP.focusRing
                            }
                            contentItem: Item { }
                            Popup {
                                id: labelMenu
                                x: labelButton.width
                                y: 0
                                padding: KP.gapS
                                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                                background: Rectangle { color: KP.floatingBg; border.color: KP.border }
                                contentItem: Row {
                                    spacing: KP.gapXS
                                    Repeater {
                                        model: panel.labelColors.length
                                        delegate: AbstractButton {
                                            required property int index
                                            width: 26; height: 26
                                            Accessible.name: panel.labelNames[index]
                                            ToolTip.text: panel.labelNames[index]
                                            ToolTip.visible: hovered
                                            ToolTip.delay: 500
                                            onClicked: {
                                                if (row.layerObject) row.layerObject.colorLabel = index
                                                labelMenu.close()
                                            }
                                            background: Rectangle {
                                                anchors.fill: parent
                                                anchors.margins: 3
                                                color: index === 0 ? KP.paneBgAlt : panel.labelColors[index]
                                                border.width: row.labelIndex === index || parent.activeFocus ? 2 : 1
                                                border.color: row.labelIndex === index || parent.activeFocus ? KP.text : KP.border
                                            }
                                            contentItem: Item { }
                                        }
                                    }
                                }
                            }
                        }
                        KpIcon {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            size: 18
                            name: "layers/new-raster"
                            color: row.layerVisible ? KP.iconNormal : KP.iconOff
                        }
                        Item {
                            id: thumbnail
                            objectName: "layerThumbnail" + row.index
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: panel.thumbnailSize
                            Layout.preferredHeight: panel.thumbnailSize
                            opacity: row.layerVisible ? 1 : 0.55
                            clip: true
                            // Neutral checkerboard denotes transparent pixels.
                            Grid {
                                anchors.fill: parent
                                columns: 4; rows: 4
                                Repeater {
                                    model: 16
                                    Rectangle {
                                        required property int index
                                        width: thumbnail.width / 4; height: thumbnail.height / 4
                                        color: (Math.floor(index / 4) + index % 4) % 2 ? "#bfbfbf" : "#eeeeee"
                                    }
                                }
                            }
                            Canvas {
                                id: thumbnailCanvas
                                renderStrategy: Canvas.Immediate
                                anchors.fill: parent
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    if (!row.layerObject) return
                                    const side = panel.thumbnailSize
                                    const pixels = row.layerObject.thumbnailPixels(side)
                                    if (pixels.length !== side * side) return
                                    const image = ctx.createImageData(side, side)
                                    for (let i = 0; i < pixels.length; ++i) {
                                        const pixel = pixels[i] >>> 0
                                        image.data[i * 4] = (pixel >>> 16) & 255
                                        image.data[i * 4 + 1] = (pixel >>> 8) & 255
                                        image.data[i * 4 + 2] = pixel & 255
                                        image.data[i * 4 + 3] = (pixel >>> 24) & 255
                                    }
                                    ctx.putImageData(image, 0, 0)
                                }
                            }
                            Rectangle { anchors.fill: parent; color: "transparent"; border.width: 1; border.color: KP.borderLight }
                            MouseArea {
                                id: thumbnailWheel
                                objectName: "thumbnailWheel" + row.index
                                anchors.fill: parent
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                                property real delta: 0
                                onExited: delta = 0
                                onWheel: function(wheel) {
                                    if (!panel.thumbnailWheelEnabled) { wheel.accepted = false; return }
                                    const amount = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                                    delta += amount
                                    while (Math.abs(delta) >= 120) {
                                        const direction = delta > 0 ? 1 : -1
                                        const i = panel.thumbnailPresets.indexOf(panel.thumbnailSize)
                                        const next = Math.max(0, Math.min(panel.thumbnailPresets.length - 1, i + direction))
                                        panel.setThumbnailSize(panel.thumbnailPresets[next])
                                        delta -= direction * 120
                                    }
                                    wheel.accepted = true
                                }
                                ToolTip.visible: containsMouse && panel.thumbnailWheelEnabled
                                ToolTip.delay: 700
                                ToolTip.text: qsTr("Wheel: thumbnail size")
                            }
                            Timer {
                                id: thumbnailTimer
                                interval: 250
                                repeat: false
                                onTriggered: thumbnailCanvas.requestPaint()
                            }
                            Connections {
                                target: row.layerObject
                                function onArtworkChanged() { if (!thumbnailTimer.running) thumbnailTimer.start() }
                            }
                            Connections {
                                target: Workspace.document
                                function onSizeChanged() { thumbnailCanvas.requestPaint() }
                            }
                            Connections {
                                target: row
                                function onLayerObjectChanged() { thumbnailCanvas.requestPaint() }
                            }
                        }
                        Item {
                            id: nameArea
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Text {
                                id: nameLabel
                                anchors.fill: parent
                                visible: !row.renaming
                                text: row.layerName
                                color: row.current ? KP.textAccent : row.layerVisible ? KP.text : KP.textSubtle
                                font.pixelSize: KP.fontS + 1
                                font.weight: row.current ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            MouseArea {
                                anchors.fill: parent
                                visible: !row.renaming
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                hoverEnabled: true
                                onClicked: function(mouse) {
                                    Workspace.layers.setActiveLayerIndex(row.index)
                                    if (mouse.button === Qt.RightButton) rowMenu.popup()
                                    else row.forceActiveFocus()
                                }
                                onDoubleClicked: function(mouse) {
                                    if (mouse.button === Qt.LeftButton) row.startRename()
                                }
                                ToolTip.text: row.layerName
                                ToolTip.visible: containsMouse && nameLabel.truncated
                                ToolTip.delay: 700
                            }
                            TextField {
                                id: renameField
                                objectName: "layerRename" + row.index
                                anchors.fill: parent
                                visible: row.renaming
                                selectByMouse: true
                                font.pixelSize: KP.fontS + 1
                                Accessible.name: qsTr("Layer name")
                                onEditingFinished: row.finishRename(true)
                                Keys.onEscapePressed: function(event) {
                                    row.finishRename(false)
                                    event.accepted = true
                                }
                            }
                            KpMenu {
                                id: rowMenu
                                KpMenuItem { text: qsTr("Rename"); onTriggered: row.startRename() }
                                KpMenuItem {
                                    text: row.layerVisible ? qsTr("Hide layer") : qsTr("Show layer")
                                    onTriggered: if (row.layerObject) row.layerObject.visible = !row.layerVisible
                                }
                                KpMenuItem {
                                    text: row.layerLocked ? qsTr("Unlock layer") : qsTr("Lock layer")
                                    onTriggered: if (row.layerObject) row.layerObject.locked = !row.layerLocked
                                }
                                KpMenuSeparator { }
                                KpMenuItem {
                                    text: qsTr("Move up")
                                    enabled: row.index > 0
                                    onTriggered: Workspace.layers.moveLayer(row.index, row.index - 1)
                                }
                                KpMenuItem {
                                    text: qsTr("Move down")
                                    enabled: row.index < layerList.count - 1
                                    onTriggered: Workspace.layers.moveLayer(row.index, row.index + 1)
                                }
                                KpMenuItem {
                                    text: qsTr("Delete layer")
                                    enabled: layerList.count > 1
                                    onTriggered: Workspace.layers.removeLayer(row.index)
                                }
                            }
                        }
                        Item {
                            id: toggles
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 24

                            IconButton {
                                objectName: "layerVisible" + row.index
                                x: 0
                                cell: 20
                                iconSize: 15
                                iconName: "layers/visible"
                                dimmed: !row.layerVisible
                                tooltip: row.layerVisible ? qsTr("Hide layer") : qsTr("Show layer")
                                opacity: row.layerVisible || row.hovered || row.current ? 1 : 0.45
                                onActivated: if (row.layerObject) row.layerObject.visible = !row.layerObject.visible
                            }
                            IconButton {
                                objectName: "layerLocked" + row.index
                                x: 22
                                cell: 20
                                iconSize: 15
                                iconName: "layers/lock"
                                dimmed: !row.layerLocked
                                tooltip: row.layerLocked ? qsTr("Unlock layer") : qsTr("Lock layer")
                                opacity: row.layerLocked || row.hovered || row.current ? 1 : 0.45
                                onActivated: if (row.layerObject) row.layerObject.locked = !row.layerObject.locked
                            }
                        }
                    }
                }
            }
        }

        // ---- Commands
        Item {
            Layout.fillWidth: true
            implicitHeight: panel.barHeight

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: KP.hairline
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KP.gapS
                anchors.rightMargin: KP.gapS
                spacing: 0

                IconButton {
                    objectName: "addLayerButton"
                    cell: 22
                    iconSize: 16
                    iconName: "layers/new-raster"
                    tooltip: qsTr("New raster layer")
                    onActivated: Workspace.layers.addLayer()
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/new-vector"
                    tooltip: qsTr("New vector layer")
                    available: false
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/folder"
                    tooltip: qsTr("New layer folder")
                    available: false
                }
                IconButton {
                    cell: 22
                    iconSize: 16
                    iconName: "layers/adjustment-layer"
                    tooltip: qsTr("New adjustment layer")
                    available: false
                }
                BarRule { }
                IconButton {
                    objectName: "layerUpButton"
                    cell: 22
                    iconSize: 13
                    glyph: "chevron-up"
                    tooltip: qsTr("Move layer up")
                    enabled: panel.active > 0
                    onActivated: Workspace.layers.moveLayer(panel.active, panel.active - 1)
                }
                IconButton {
                    objectName: "layerDownButton"
                    cell: 22
                    iconSize: 13
                    glyph: "chevron-down"
                    tooltip: qsTr("Move layer down")
                    enabled: panel.active >= 0 && panel.active < layerList.count - 1
                    onActivated: Workspace.layers.moveLayer(panel.active, panel.active + 1)
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    objectName: "deleteLayerButton"
                    cell: 22
                    iconSize: 16
                    iconName: "layers/delete"
                    tooltip: qsTr("Delete layer")
                    // The last remaining layer cannot be deleted.
                    enabled: layerList.count > 1
                    onActivated: Workspace.layers.removeLayer(panel.active)
                }
            }
        }
    }
}
