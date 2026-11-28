import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

// Brush Studio -> Texture. A short, artist-facing stack of independent texture
// layers. The ordinary path is: add a starter or import an image, drag
// Strength and Scale, draw in the Live Preview, save. Rotation, contrast,
// position, variation and blending stay behind "More".
Item {
    id: root
    required property BrushStudioModel draft
    signal importRequested()

    readonly property var selected: draft.selectedTextureLayer
    readonly property bool hasSelection: !!selected.id
    readonly property bool compact: width < 520
    property bool moreOpen: false

    function applySetting(key, value) {
        if (hasSelection) draft.setTextureLayerSetting(selected.id, key, value)
    }
    function beginEdit(label) {
        if (hasSelection) draft.beginStep(label)
    }
    function finishEdit() {
        draft.commitStep()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: KP.gapM
        spacing: KP.gapS

        RowLayout {
            Layout.fillWidth: true
            spacing: KP.gapS
            Text {
                Layout.fillWidth: true
                text: qsTr("TEXTURE LAYERS")
                color: KP.text
                font.family: KP.fontMono
                font.pixelSize: KP.fontXS
            }
            KpButton {
                objectName: "brushStudioTextureAdd"
                small: true
                Layout.preferredHeight: 32
                Layout.minimumHeight: 32
                enabled: draft.textureEditingSupported
                label: qsTr("Add texture")
                glyph: "plus"
                tip: qsTr("Add a texture layer from a starter grain or an image. Each layer is independent and can be removed without changing the tip.")
                onClicked: addMenu.open()
            }
            KpButton {
                objectName: "brushStudioTextureImport"
                small: true
                Layout.preferredHeight: 32
                Layout.minimumHeight: 32
                enabled: draft.textureEditingSupported
                label: qsTr("Import...")
                tip: qsTr("Use an image from this device as a texture. Its brightness becomes the grain and its transparency is kept.")
                onClicked: root.importRequested()
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Texture breaks paint up like paper tooth. Start with one layer, adjust Strength and Scale, then draw in the Live Preview below.")
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: draft.engine === "realistic"
            text: qsTr("This brush uses Realistic painting, which follows its physical paper instead of an image texture. Your texture layers are kept and will paint when the brush uses Digital.")
            color: KP.warning
            font.family: KP.fontUi
            font.pixelSize: KP.fontS
            wrapMode: Text.WordWrap
        }

        // A clean list first; only the selected layer shows its controls.
        ListView {
            id: layerList
            objectName: "brushStudioTextureList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(200, Math.max(56, count * 40))
            visible: count > 0
            clip: true
            spacing: 2
            model: draft.textureLayers
            enabled: draft.textureEditingSupported
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded; opacity: size < 1 ? 1 : 0 }
            delegate: ItemDelegate {
                id: textureRow
                required property var modelData
                objectName: "brushStudioTextureRow_" + modelData.index
                width: layerList.width
                height: 40
                padding: 0
                hoverEnabled: true
                Accessible.name: modelData.name
                Accessible.description: qsTr("%1. %2. Strength %3 percent.")
                    .arg(modelData.placementLabel).arg(modelData.blendLabel)
                    .arg(Math.round(modelData.strength * 100))
                ToolTip.text: Accessible.description
                ToolTip.visible: hovered || activeFocus
                ToolTip.delay: 500
                onClicked: draft.selectTextureLayer(modelData.id)
                background: Rectangle {
                    color: draft.selectedTextureId === modelData.id ? KP.rowSelected
                         : textureRow.hovered ? KP.hoverOverlay : "transparent"
                    border.width: textureRow.activeFocus ? KP.focusWidth : 0
                    border.color: KP.focusRing
                }
                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: KP.gapS
                    anchors.rightMargin: KP.gapS
                    spacing: KP.gapS
                    CheckBox {
                        objectName: "brushStudioTextureEnabled_" + modelData.index
                        checked: modelData.enabled
                        Layout.alignment: Qt.AlignVCenter
                        Accessible.name: qsTr("Use this texture layer")
                        Accessible.description: qsTr("Turn this texture on or off without deleting it.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: hovered || activeFocus || pressed
                        ToolTip.delay: 500
                        onToggled: draft.setTextureLayerEnabled(modelData.id, checked)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        elide: Text.ElideRight
                        color: modelData.enabled ? KP.text : KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        text: modelData.placementLabel
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontXS
                        elide: Text.ElideRight
                        Layout.maximumWidth: 150
                    }
                }
            }
        }

        KpButton {
            objectName: "brushStudioTextureRemove"
            visible: hasSelection
            small: true
            Layout.preferredHeight: 32
            Layout.minimumHeight: 32
            label: qsTr("Remove layer")
            variant: "danger"
            tip: qsTr("Remove this texture layer from the brush. The image stays available in this brush and can be added again.")
            onClicked: draft.removeTextureLayer(selected.id)
        }

        Text {
            Layout.fillWidth: true
            visible: !hasSelection
            text: draft.textureLayerCount === 0
                  ? qsTr("No texture yet. Add a starter grain or import an image — the brush keeps painting normally until you add one.")
                  : qsTr("Select a texture layer to change its strength, scale and placement.")
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            wrapMode: Text.WordWrap
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: hasSelection
            contentWidth: width
            contentHeight: controls.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded; opacity: size < 1 ? 1 : 0 }
            ColumnLayout {
                id: controls
                width: parent.width
                spacing: KP.gapS

                // A user drag removes QML's value binding. Reassert the live
                // document value when the layer, selection or undo changes,
                // but never while the artist is holding the control.
                Connections {
                    target: draft
                    function onTextureChanged() {
                        if (!strengthRail.pressed) strengthRail.value = Math.round((root.selected.strength || 0) * 100)
                        if (!scaleRail.pressed) scaleRail.value = Math.round((root.selected.scale || 1) * 100)
                        if (!rotationRail.pressed) rotationRail.value = Math.round(root.selected.rotation || 0)
                        if (!contrastRail.pressed) contrastRail.value = Math.round((root.selected.contrast || 1) * 100)
                        if (!offsetXRail.pressed) offsetXRail.value = Math.round((root.selected.offsetX || 0) * 100)
                        if (!offsetYRail.pressed) offsetYRail.value = Math.round((root.selected.offsetY || 0) * 100)
                        if (!variationRail.pressed) variationRail.value = Math.round((root.selected.variation || 0) * 100)
                        pressureBox.checked = !!root.selected.pressure
                        invertBox.checked = !!root.selected.invert
                        const candidates = draft.textureCandidates
                        const candidateAt = candidates.findIndex(entry => entry.id === root.selected.resourceId)
                        if (!textureChoice.popup.visible) textureChoice.currentIndex = candidateAt
                        const blendAt = blendChoice.model.findIndex(entry => entry.value === root.selected.blend)
                        if (!blendChoice.popup.visible) blendChoice.currentIndex = blendAt
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: KP.gapS
                    KpButton {
                        objectName: "brushStudioTextureMoveUp"
                        small: true
                        glyph: "chevron-up"
                        tip: qsTr("Move this texture layer up in the stack.")
                        enabled: root.selected.index < draft.textureLayerCount - 1
                        onClicked: draft.moveTextureLayer(root.selected.id, root.selected.index + 1)
                    }
                    KpButton {
                        objectName: "brushStudioTextureMoveDown"
                        small: true
                        glyph: "chevron-down"
                        tip: qsTr("Move this texture layer down in the stack.")
                        enabled: root.selected.index > 0
                        onClicked: draft.moveTextureLayer(root.selected.id, root.selected.index - 1)
                    }
                    Text {
                        text: qsTr("Texture")
                        color: KP.textMuted
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        Layout.preferredWidth: root.compact ? 72 : 82
                    }
                    StudioChoice {
                        id: textureChoice
                        objectName: "brushStudioTextureChoice"
                        Layout.fillWidth: true
                        enabled: draft.textureEditingSupported
                        model: draft.textureCandidates
                        textRole: "name"
                        valueRole: "id"
                        Accessible.name: qsTr("Texture image")
                        Accessible.description: qsTr("Choose an image already in this brush, or import a new one.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.delay: 500
                        currentIndex: {
                            const at = model.findIndex(entry => entry.id === root.selected.resourceId)
                            return at < 0 ? -1 : at
                        }
                        onActivated: function(index) {
                            const entry = model[index]
                            if (entry) draft.setTextureLayerResource(root.selected.id, entry.id)
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.compact ? 1 : 2
                    columnSpacing: KP.gapM
                    rowSpacing: KP.gapXS

                    Text { text: qsTr("Strength"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    Slider {
                        id: strengthRail
                        objectName: "brushStudioTextureStrength"
                        Layout.fillWidth: true
                        enabled: draft.textureEditingSupported
                        from: 0; to: 100; stepSize: 1
                        value: Math.round(root.selected.strength * 100)
                        Accessible.name: qsTr("Texture strength")
                        Accessible.description: qsTr("How much the texture hides paint. Small amounts add paper tooth; high amounts make a broken, dry mark.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: pressed || activeFocus
                        ToolTip.delay: 500
                        onPressedChanged: {
                            if (pressed) root.beginEdit(qsTr("Texture strength"))
                            else root.finishEdit()
                        }
                        onMoved: root.applySetting("strength", value / 100)
                    }

                    Text { text: qsTr("Scale"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    Slider {
                        id: scaleRail
                        objectName: "brushStudioTextureScale"
                        Layout.fillWidth: true
                        enabled: draft.textureEditingSupported
                        from: 10; to: 800; stepSize: 1
                        value: Math.round(root.selected.scale * 100)
                        Accessible.name: qsTr("Texture scale")
                        Accessible.description: qsTr("How large the grain appears. Raise it for broad paper texture and lower it for fine tooth.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: pressed || activeFocus
                        ToolTip.delay: 500
                        onPressedChanged: {
                            if (pressed) root.beginEdit(qsTr("Texture scale"))
                            else root.finishEdit()
                        }
                        onMoved: root.applySetting("scale", value / 100)
                    }
                }

                Text { text: qsTr("Where the texture follows the brush"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                SegmentedChoice {
                    objectName: "brushStudioTexturePlacement"
                    Layout.fillWidth: true
                    enabled: draft.textureEditingSupported
                    options: [qsTr("Tip"), qsTr("Stroke"), qsTr("Canvas")]
                    explanations: [
                        qsTr("The grain repeats on every stamp and moves with the tip. Best for a textured pencil or dry brush."),
                        qsTr("The grain moves with the line and turns with its direction. Best for a dragged, streaky mark."),
                        qsTr("The grain stays fixed on the canvas while the brush moves over it. Best for paper and canvas tooth.")
                    ]
                    currentIndex: root.selected.placement !== undefined ? root.selected.placement : 0
                    onChosen: function(index) { draft.setTextureLayerPlacement(root.selected.id, index) }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.selected.placementLabel || ""
                    color: KP.textSubtle
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                }

                KpButton {
                    objectName: "brushStudioTextureMore"
                    small: true
                    enabled: draft.textureEditingSupported
                    label: root.moreOpen ? qsTr("Hide extra texture controls") : qsTr("More texture controls")
                    glyph: root.moreOpen ? "chevron-up" : "chevron-down"
                    tip: qsTr("Rotation, contrast, inversion, position, variation and blending. Most brushes do not need these.")
                    onClicked: root.moreOpen = !root.moreOpen
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.moreOpen
                    enabled: draft.textureEditingSupported
                    spacing: KP.gapXS

                    Text { text: qsTr("Rotation"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    Slider {
                        id: rotationRail
                        objectName: "brushStudioTextureRotation"
                        Layout.fillWidth: true
                        from: -180; to: 180; stepSize: 1
                        value: Math.round(root.selected.rotation || 0)
                        Accessible.name: qsTr("Texture rotation")
                        Accessible.description: qsTr("Turns the grain. Use it to follow the direction of a surface or to break up repetition.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: pressed || activeFocus
                        ToolTip.delay: 500
                        onPressedChanged: {
                            if (pressed) root.beginEdit(qsTr("Texture rotation"))
                            else root.finishEdit()
                        }
                        onMoved: root.applySetting("rotation", value)
                    }

                    Text { text: qsTr("Contrast"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    Slider {
                        id: contrastRail
                        objectName: "brushStudioTextureContrast"
                        Layout.fillWidth: true
                        from: 0; to: 200; stepSize: 1
                        value: Math.round((root.selected.contrast || 1) * 100)
                        Accessible.name: qsTr("Texture contrast")
                        Accessible.description: qsTr("Makes the difference between textured and clear areas sharper or softer.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: pressed || activeFocus
                        ToolTip.delay: 500
                        onPressedChanged: {
                            if (pressed) root.beginEdit(qsTr("Texture contrast"))
                            else root.finishEdit()
                        }
                        onMoved: root.applySetting("contrast", value / 100)
                    }

                    Text { text: qsTr("Position"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapS
                        Slider {
                            id: offsetXRail
                            objectName: "brushStudioTextureOffsetX"
                            Layout.fillWidth: true
                            from: -200; to: 200; stepSize: 1
                            value: Math.round((root.selected.offsetX || 0) * 100)
                            Accessible.name: qsTr("Texture position across")
                            Accessible.description: qsTr("Slides the grain sideways without moving the brush.")
                            ToolTip.text: Accessible.description
                            ToolTip.visible: pressed || activeFocus
                            ToolTip.delay: 500
                            onPressedChanged: { if (pressed) root.beginEdit(qsTr("Texture position")); else root.finishEdit() }
                            onMoved: root.applySetting("offsetX", value / 100)
                        }
                        Slider {
                            id: offsetYRail
                            objectName: "brushStudioTextureOffsetY"
                            Layout.fillWidth: true
                            from: -200; to: 200; stepSize: 1
                            value: Math.round((root.selected.offsetY || 0) * 100)
                            Accessible.name: qsTr("Texture position up and down")
                            Accessible.description: qsTr("Slides the grain up or down without moving the brush.")
                            ToolTip.text: Accessible.description
                            ToolTip.visible: pressed || activeFocus
                            ToolTip.delay: 500
                            onPressedChanged: { if (pressed) root.beginEdit(qsTr("Texture position")); else root.finishEdit() }
                            onMoved: root.applySetting("offsetY", value / 100)
                        }
                    }

                    Text { text: qsTr("Variation"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    Slider {
                        id: variationRail
                        objectName: "brushStudioTextureVariation"
                        Layout.fillWidth: true
                        from: 0; to: 100; stepSize: 1
                        value: Math.round((root.selected.variation || 0) * 100)
                        Accessible.name: qsTr("Texture variation")
                        Accessible.description: qsTr("Moves the grain a little on each stamp so a repeated texture does not look stamped.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: pressed || activeFocus
                        ToolTip.delay: 500
                        onPressedChanged: { if (pressed) root.beginEdit(qsTr("Texture variation")); else root.finishEdit() }
                        onMoved: root.applySetting("variation", value / 100)
                    }

                    CheckBox {
                        id: pressureBox
                        objectName: "brushStudioTexturePressure"
                        text: qsTr("Pressure deepens grain")
                        checked: !!root.selected.pressure
                        enabled: draft.textureEditingSupported
                        Accessible.name: text
                        Accessible.description: qsTr("Only pressure-sensitive pens change the grain with pressure. Mouse, touch and plain stylus keep the grain steady.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: hovered || activeFocus || pressed
                        ToolTip.delay: 500
                        onToggled: draft.setTextureLayerPressure(root.selected.id, checked)
                    }

                    CheckBox {
                        id: invertBox
                        objectName: "brushStudioTextureInvert"
                        text: qsTr("Invert grain")
                        checked: !!root.selected.invert
                        Accessible.name: text
                        Accessible.description: qsTr("Swap light and dark grain. Use it when a texture removes paint in the wrong places.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: hovered || activeFocus || pressed
                        ToolTip.delay: 500
                        onToggled: draft.setTextureLayerInvert(root.selected.id, checked)
                    }

                    Text { text: qsTr("Grain blending"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontS }
                    StudioChoice {
                        id: blendChoice
                        objectName: "brushStudioTextureBlend"
                        Layout.fillWidth: true
                        model: [
                            { value: 0, label: qsTr("Soft grain") },
                            { value: 1, label: qsTr("Dark grain breaks paint") },
                            { value: 2, label: qsTr("Light grain breaks paint") },
                            { value: 3, label: qsTr("Crisp grain") }
                        ]
                        textRole: "label"
                        valueRole: "value"
                        Accessible.name: qsTr("Texture grain blending")
                        Accessible.description: qsTr("Choose which parts of the texture break the paint. Dark grain is the usual paper-tooth choice.")
                        ToolTip.text: Accessible.description
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.delay: 500
                        currentIndex: {
                            const at = model.findIndex(entry => entry.value === root.selected.blend)
                            return at < 0 ? 1 : at
                        }
                        onActivated: function(index) {
                            const entry = model[index]
                            if (entry) draft.setTextureLayerBlend(root.selected.id, entry.value)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: addMenu
        objectName: "brushStudioTextureAddMenu"
        parent: root
        x: Math.max(KP.gapM, root.width - width - KP.gapM)
        y: KP.controlHeight + KP.gapM
        width: 236
        padding: KP.gapS
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: KP.floatingBg; border.width: 1; border.color: KP.border; radius: KP.radiusS }
        contentItem: ColumnLayout {
            spacing: KP.gapXS
            Text { text: qsTr("Starter textures"); color: KP.textMuted; font.family: KP.fontUi; font.pixelSize: KP.fontXS }
            Repeater {
                model: [
                    { kind: "paper", label: qsTr("Paper grain") },
                    { kind: "canvas", label: qsTr("Canvas weave") },
                    { kind: "speckle", label: qsTr("Dry speckle") },
                    { kind: "mottle", label: qsTr("Soft mottle") }
                ]
                KpButton {
                    required property var modelData
                    objectName: "brushStudioTextureAdd_" + modelData.kind
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    label: modelData.label
                    small: true
                    tip: qsTr("Add this starter texture as a new independent layer.")
                    onClicked: { draft.addBuiltInTexture(modelData.kind); addMenu.close() }
                }
            }
            KpButton {
                objectName: "brushStudioTextureAddPicture"
                Layout.fillWidth: true
                label: qsTr("Choose an image...")
                small: true
                tip: qsTr("Use an existing image file as a texture layer.")
                onClicked: { addMenu.close(); root.importRequested() }
            }
        }
    }
}
