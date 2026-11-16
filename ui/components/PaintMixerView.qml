import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../themes"

Rectangle {
    id: root
    color: KP.paneBg

    readonly property var mixer: Workspace.mixer
    readonly property var active: mixer ? mixer.activeDeposit : ({})
    readonly property var depositList: mixer ? mixer.deposits : []
    readonly property var savedList: mixer ? mixer.savedMixtures : []
    readonly property bool isWatercolour: mixer && mixer.activeMediumId === "watercolour"

    property string pigmentSearch: ""
    property string selectedFamily: "All"
    property bool onlyCompatiblePigments: false
    property string mixTargetId: ""
    property real mixShare: 0.5

    function matchesPigmentFilter(p) {
        if (onlyCompatiblePigments && !p.compatibleWithActive)
            return false
        if (selectedFamily !== "All" && p.colorFamily !== selectedFamily)
            return false
        const q = pigmentSearch.trim().toLowerCase()
        if (q.length > 0) {
            const hay = (p.id + " " + p.name + " " + p.colorFamily).toLowerCase()
            if (hay.indexOf(q) < 0) return false
        }
        return true
    }

    function firstOtherDepositId() {
        if (!mixer || !depositList || depositList.length < 2) return ""
        for (let i = 0; i < depositList.length; ++i) {
            if (depositList[i].id !== mixer.activeDepositId)
                return depositList[i].id
        }
        return ""
    }

    onDepositListChanged: {
        if (!mixer) return
        let valid = false
        for (let i = 0; i < depositList.length; ++i) {
            if (depositList[i].id === mixTargetId && depositList[i].id !== mixer.activeDepositId) {
                valid = true
                break
            }
        }
        if (!valid) mixTargetId = firstOtherDepositId()
    }

    Flickable {
        id: scroller
        objectName: "paintMixerScroller"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: Math.max(mainColumn.height, mainColumn.implicitHeight) + 2 * KP.panelInset
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: KpScrollBar {
            policy: scroller.contentHeight > scroller.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: mainColumn
            x: KP.panelInset
            y: KP.panelInset
            width: scroller.width - 2 * KP.panelInset
            spacing: KP.gapS

            // ---- Medium selector + Mixer Undo / Redo (decoupled from canvas)
            RowLayout {
                Layout.fillWidth: true
                spacing: KP.gapS

                SegmentedChoice {
                    id: mediumChoice
                    objectName: "mixerMediumChoice"
                    Layout.fillWidth: true
                    options: [qsTr("Gouache"), qsTr("Watercolour")]
                    currentIndex: root.isWatercolour ? 1 : 0
                    onChosen: function(index) {
                        if (root.mixer)
                            root.mixer.activeMediumId = index === 1 ? "watercolour" : "gouache"
                    }
                }

                KpButton {
                    objectName: "mixerUndoButton"
                    small: true
                    label: qsTr("Undo")
                    tip: root.mixer && root.mixer.canUndo
                         ? qsTr("Undo mixer edit: %1").arg(root.mixer.undoLabel)
                         : qsTr("Nothing to undo in Paint Mixer")
                    enabled: root.mixer && root.mixer.canUndo
                    onClicked: if (root.mixer) root.mixer.undo()
                }
                KpButton {
                    objectName: "mixerRedoButton"
                    small: true
                    label: qsTr("Redo")
                    tip: root.mixer && root.mixer.canRedo
                         ? qsTr("Redo mixer edit: %1").arg(root.mixer.redoLabel)
                         : qsTr("Nothing to redo in Paint Mixer")
                    enabled: root.mixer && root.mixer.canRedo
                    onClicked: if (root.mixer) root.mixer.redo()
                }
            }

            // ---- Status / Error banner
            Rectangle {
                visible: root.mixer && (root.mixer.statusNotice.length > 0 || root.mixer.lastError.length > 0)
                Layout.fillWidth: true
                implicitHeight: statusLabel.implicitHeight + 8
                radius: KP.radiusXS
                color: KP.panelInsetBg
                border.width: 1
                border.color: root.mixer && root.mixer.lastError.length > 0 ? KP.error : KP.hairline

                Text {
                    id: statusLabel
                    anchors.fill: parent
                    anchors.margins: 4
                    text: root.mixer
                          ? (root.mixer.lastError.length > 0 ? root.mixer.lastError : root.mixer.statusNotice)
                          : ""
                    color: root.mixer && root.mixer.lastError.length > 0 ? KP.error : KP.textMuted
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                    wrapMode: Text.WordWrap
                }
            }

            // ---- Isolated Mixing Surface (Non-Absorbent Palette)
            RowLayout {
                Layout.fillWidth: true
                spacing: KP.gapXS
                Text {
                    Layout.fillWidth: true
                    text: qsTr("MIXING SURFACE")
                    color: KP.textSectionLabel
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontMicro
                    font.letterSpacing: KP.trackingMicro
                }
                KpButton {
                    objectName: "mixerNewWellButton"
                    small: true
                    label: qsTr("+ Well")
                    tip: qsTr("Create an empty mixing well on the palette")
                    onClicked: if (root.mixer) root.mixer.createEmptyDeposit()
                }
                KpButton {
                    objectName: "mixerClearButton"
                    small: true
                    variant: "danger"
                    label: qsTr("Clear")
                    tip: qsTr("Clear all deposits from the mixing palette")
                    enabled: root.depositList.length > 0
                    onClicked: if (root.mixer) root.mixer.clearPalette()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Math.round(width * (160.0 / 256.0))
                radius: KP.radiusS
                border.width: 1
                border.color: KP.border
                color: "#FAF8F4"
                clip: true

                MixerSurfaceItem {
                    id: surfaceItem
                    objectName: "mixerSurface"
                    anchors.fill: parent
                    anchors.margins: 1
                    mixer: root.mixer
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 20
                    visible: root.depositList.length === 0
                    text: qsTr("Add pigments below, import a colour from the Color Wheel, or double-click to place a well.")
                    color: "#666666"
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontXS
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Click well to select · Drag well onto another to mix · Double-click palette to add well")
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontMicro
                wrapMode: Text.WordWrap
            }

            // ---- Deposit Chips
            Flow {
                Layout.fillWidth: true
                spacing: KP.gapXS
                visible: root.depositList.length > 0

                Repeater {
                    model: root.depositList
                    Rectangle {
                        id: depositChip
                        required property var modelData
                        readonly property bool isCurrent: root.mixer && modelData.id === root.mixer.activeDepositId
                        width: chipRow.implicitWidth + 10
                        height: 22
                        radius: KP.radiusXS
                        color: isCurrent ? KP.accentWash : KP.paneBgAlt
                        border.width: 1
                        border.color: isCurrent ? KP.accent : KP.border

                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 4
                            Rectangle {
                                width: 10
                                height: 10
                                radius: 5
                                anchors.verticalCenter: parent.verticalCenter
                                color: modelData.porcelainHex
                                border.width: 1
                                border.color: KP.border
                            }
                            Text {
                                text: modelData.name + (modelData.containsDigitalImport ? " [D]" : "")
                                color: isCurrent ? KP.textAccent : KP.text
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontXS
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: if (root.mixer) root.mixer.selectDeposit(modelData.id)
                        }
                    }
                }
            }

            // ---- Active Deposit Inspector & Brush Loading
            Rectangle {
                id: activeCard
                Layout.fillWidth: true
                visible: root.active && root.active.id !== undefined
                implicitHeight: activeCol.height + 12
                radius: KP.radiusS
                color: KP.panelInsetBg
                border.width: 1
                border.color: KP.hairline

                ColumnLayout {
                    id: activeCol
                    x: 6
                    y: 6
                    width: activeCard.width - 12
                    spacing: KP.gapS

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapM

                        // Split optical swatch over light porcelain + dark ground to reveal hiding power vs transparency
                        Rectangle {
                            id: splitSwatch
                            Layout.preferredWidth: 52
                            Layout.preferredHeight: 34
                            radius: KP.radiusXS
                            border.width: 1
                            border.color: KP.border
                            clip: true

                            Rectangle {
                                x: 0; y: 0
                                width: parent.width / 2
                                height: parent.height
                                color: root.active && root.active.porcelainHex ? root.active.porcelainHex : "#FAF8F4"
                            }
                            Rectangle {
                                x: parent.width / 2; y: 0
                                width: parent.width - x
                                height: parent.height
                                color: root.active && root.active.darkGroundHex ? root.active.darkGroundHex : "#282A2E"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: root.active && root.active.name ? root.active.name : ""
                                color: KP.text
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontS
                                font.weight: KP.weightBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: {
                                    if (!root.active || !root.active.id) return ""
                                    const med = root.active.mediumId === "watercolour" ? qsTr("Watercolour") : qsTr("Gouache")
                                    const opPct = Math.round((root.active.alpha || 0) * 100)
                                    return "%1 · %2 · %3% opacity".arg(root.active.porcelainHex || "").arg(med).arg(opPct)
                                }
                                color: KP.textMuted
                                font.family: KP.fontMono
                                font.pixelSize: KP.fontMicro
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: root.active && root.active.containsDigitalImport === true
                                text: qsTr("Contains Digital / Imported RGB colour (not a real-world pigment)")
                                color: KP.warning
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontMicro
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Primary Brush & Color Wheel transfer actions
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapS
                        KpButton {
                            objectName: "mixerLoadBrushButton"
                            Layout.fillWidth: true
                            variant: "solid"
                            label: qsTr("Load into Brush")
                            tip: qsTr("Load this pigment composition and medium into the traditional brush (auto-reload ON by default)")
                            enabled: root.active && root.active.id !== undefined
                            onClicked: if (root.mixer) root.mixer.loadActiveIntoBrush()
                        }
                        KpButton {
                            objectName: "mixerToColorWheelButton"
                            small: true
                            label: qsTr("To Wheel")
                            tip: qsTr("Set the active drawing colour in the Color Wheel to this mixture's result")
                            enabled: root.active && root.active.hasComponents === true
                            onClicked: if (root.mixer) root.mixer.sendActiveColorToWheel()
                        }
                    }

                    // Composition components
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("COMPOSITION")
                        color: KP.textSectionLabel
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontMicro
                        font.letterSpacing: KP.trackingMicro
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: !root.active || !root.active.components || root.active.components.length === 0
                        text: qsTr("Clear medium/water well — add pigments from the library below.")
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontXS
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.active && root.active.components ? root.active.components : []
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: compRow.implicitHeight + 6
                            radius: KP.radiusXS
                            color: KP.paneBgAlt
                            border.width: 1
                            border.color: modelData.isDigital ? KP.warning : KP.hairline

                            RowLayout {
                                id: compRow
                                anchors.fill: parent
                                anchors.margins: 3
                                spacing: 4

                                Rectangle {
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    radius: modelData.isDigital ? 2 : 6
                                    color: modelData.swatchHex || "#888888"
                                    border.width: 1
                                    border.color: KP.border
                                }
                                Column {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        width: parent.width
                                        text: modelData.label
                                        color: KP.text
                                        font.family: KP.fontUi
                                        font.pixelSize: KP.fontXS
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: "%1% · %2".arg(Math.round(modelData.sharePercent || 0))
                                            .arg(modelData.isDigital ? qsTr("Digital/Imported") : qsTr("Pigment"))
                                        color: modelData.isDigital ? KP.warning : KP.textSubtle
                                        font.family: KP.fontUi
                                        font.pixelSize: KP.fontMicro
                                        elide: Text.ElideRight
                                    }
                                }
                                KpButton {
                                    objectName: "compMinus_" + modelData.index
                                    small: true
                                    label: "-"
                                    tip: qsTr("Reduce quantity")
                                    onClicked: if (root.mixer) root.mixer.setComponentMass(modelData.index, Math.max(0.05, modelData.mass - 0.25))
                                }
                                KpButton {
                                    objectName: "compPlus_" + modelData.index
                                    small: true
                                    label: "+"
                                    tip: qsTr("Add more of this colour")
                                    onClicked: if (root.mixer) root.mixer.setComponentMass(modelData.index, modelData.mass + 0.25)
                                }
                                KpButton {
                                    objectName: "compRemove_" + modelData.index
                                    small: true
                                    label: "×"
                                    tip: qsTr("Remove from mixture")
                                    onClicked: if (root.mixer) root.mixer.removeComponent(modelData.index)
                                }
                            }
                        }
                    }

                    // Water / Medium / Body sliders (Medium-aware!)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapXS
                        KpButton {
                            objectName: "mixerAddWaterButton"
                            small: true
                            Layout.fillWidth: true
                            label: root.isWatercolour ? qsTr("+ Water (Dilute)") : qsTr("+ Water")
                            onClicked: if (root.mixer) root.mixer.addWater(0.45)
                        }
                        KpButton {
                            objectName: "mixerAddPaintButton"
                            small: true
                            Layout.fillWidth: true
                            label: qsTr("+ Paint Body")
                            enabled: root.active && root.active.hasComponents === true
                            onClicked: if (root.mixer) root.mixer.addPaintBody(0.35)
                        }
                    }

                    PropertySlider {
                        objectName: "mixerWaterSlider"
                        Layout.fillWidth: true
                        label: root.isWatercolour ? qsTr("Water dilution") : qsTr("Water")
                        unit: ""
                        from: 0
                        to: 600
                        value: root.active ? Math.round((root.active.water || 0) * 100) : 0
                        onEdited: function(v) { if (root.mixer) root.mixer.setWater(v / 100.0) }
                    }

                    PropertySlider {
                        objectName: "mixerBinderSlider"
                        Layout.fillWidth: true
                        label: root.isWatercolour ? qsTr("Gum arabic binder") : qsTr("Gouache binder")
                        unit: ""
                        from: 0
                        to: 200
                        value: root.active ? Math.round((root.active.binder || 0) * 100) : 0
                        onEdited: function(v) { if (root.mixer) root.mixer.setBinder(v / 100.0) }
                    }

                    PropertySlider {
                        objectName: "mixerFillerSlider"
                        visible: !root.isWatercolour
                        Layout.fillWidth: true
                        label: qsTr("Hiding filler / body")
                        unit: ""
                        from: 0
                        to: 200
                        value: root.active ? Math.round((root.active.filler || 0) * 100) : 0
                        onEdited: function(v) { if (root.mixer) root.mixer.setFiller(v / 100.0) }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.isWatercolour && root.active && root.active.id !== undefined
                        text: {
                            if (!root.active || !root.active.id) return ""
                            const conc = Math.round((root.active.pigmentShare || 0) * 100)
                            const water = Math.round((root.active.waterShare || 0) * 100)
                            return qsTr("Watercolour concentration: %1% pigment · %2% water (transparent wash optics)").arg(conc).arg(water)
                        }
                        color: KP.textMuted
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontMicro
                        wrapMode: Text.WordWrap
                    }

                    // Mix two deposits on the palette
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root.depositList.length >= 2
                        spacing: KP.gapXS

                        Text {
                            text: qsTr("MIX WITH ANOTHER DEPOSIT")
                            color: KP.textSectionLabel
                            font.family: KP.fontUi
                            font.pixelSize: KP.fontMicro
                            font.letterSpacing: KP.trackingMicro
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: KP.gapXS

                            ComboBox {
                                id: mixCombo
                                objectName: "mixerTargetCombo"
                                Layout.fillWidth: true
                                model: {
                                    const out = []
                                    for (let i = 0; i < root.depositList.length; ++i) {
                                        const d = root.depositList[i]
                                        if (!root.mixer || d.id !== root.mixer.activeDepositId)
                                            out.push({ value: d.id, text: d.name })
                                    }
                                    return out
                                }
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: Math.max(0, indexOfValue(root.mixTargetId))
                                onActivated: root.mixTargetId = currentValue
                            }
                            KpButton {
                                objectName: "mixerCombineButton"
                                small: true
                                label: qsTr("Mix")
                                tip: qsTr("Mix the selected deposit into the active deposit")
                                enabled: root.mixTargetId.length > 0
                                onClicked: if (root.mixer) root.mixer.mixDeposits(root.mixTargetId, root.mixer.activeDepositId, root.mixShare, false)
                            }
                            KpButton {
                                objectName: "mixerCombineNewButton"
                                small: true
                                label: qsTr("As New")
                                tip: qsTr("Create a new mixed deposit from both")
                                enabled: root.mixTargetId.length > 0
                                onClicked: if (root.mixer) root.mixer.mixDeposits(root.mixTargetId, root.mixer.activeDepositId, root.mixShare, true)
                            }
                        }
                        PropertySlider {
                            objectName: "mixerBlendRatio"
                            Layout.fillWidth: true
                            label: qsTr("Added share")
                            unit: "%"
                            from: 5
                            to: 95
                            value: Math.round(root.mixShare * 100)
                            onEdited: function(v) { root.mixShare = v / 100.0 }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapXS
                        KpButton {
                            objectName: "mixerResetWellButton"
                            small: true
                            Layout.fillWidth: true
                            label: qsTr("Reset Well")
                            onClicked: if (root.mixer) root.mixer.resetActiveDeposit()
                        }
                        KpButton {
                            objectName: "mixerRemoveWellButton"
                            small: true
                            variant: "danger"
                            Layout.fillWidth: true
                            label: qsTr("Remove Well")
                            onClicked: if (root.mixer && root.active.id) root.mixer.removeDeposit(root.active.id)
                        }
                    }
                }
            }

            // ---- Pigment Library
            PanelSection {
                Layout.fillWidth: true
                title: qsTr("Pigment Library")
                hint: qsTr("estimated optical data")
                showRule: true
                contentTopMargin: KP.gapS

                ColumnLayout {
                    width: parent.width
                    spacing: KP.gapS

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapXS
                        KpTextField {
                            objectName: "pigmentSearchField"
                            Layout.fillWidth: true
                            height: KP.controlHeightSmall + 2
                            placeholder: qsTr("Search pigment ID or name…")
                            text: root.pigmentSearch
                            onTextChanged: root.pigmentSearch = text
                        }
                        KpButton {
                            objectName: "pigmentCompatFilter"
                            small: true
                            toggled: root.onlyCompatiblePigments
                            label: qsTr("Compat")
                            tip: qsTr("Filter to pigments recommended for the active medium")
                            onClicked: root.onlyCompatiblePigments = !root.onlyCompatiblePigments
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: KP.gapXS
                        Repeater {
                            model: ["All", "Yellows", "Reds", "Blues", "Greens", "Earths", "Neutrals"]
                            KpButton {
                                required property string modelData
                                small: true
                                toggled: root.selectedFamily === modelData
                                label: modelData
                                onClicked: root.selectedFamily = modelData
                            }
                        }
                    }

                    Repeater {
                        model: root.mixer ? root.mixer.pigmentLibrary : []
                        Rectangle {
                            required property var modelData
                            visible: root.matchesPigmentFilter(modelData)
                            Layout.fillWidth: true
                            implicitHeight: visible ? pigRow.implicitHeight + 8 : 0
                            radius: KP.radiusXS
                            color: KP.paneBgAlt
                            border.width: 1
                            border.color: KP.hairline
                            opacity: modelData.compatibleWithActive ? 1.0 : 0.72

                            RowLayout {
                                id: pigRow
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: KP.gapS

                                Rectangle {
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 24
                                    radius: KP.radiusXS
                                    color: modelData.swatchHex || "#888888"
                                    border.width: 1
                                    border.color: KP.border
                                }
                                Column {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        width: parent.width
                                        text: "%1 · %2".arg(modelData.id).arg(modelData.name)
                                        color: KP.text
                                        font.family: KP.fontUi
                                        font.pixelSize: KP.fontXS
                                        font.weight: KP.weightMedium
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: {
                                            const transp = Math.round((modelData.transparency || 0) * 100)
                                            const gran = Math.round((modelData.granulation || 0) * 100)
                                            const compat = modelData.compatibleWithActive
                                                ? (root.isWatercolour ? qsTr("WC") : qsTr("Gouache"))
                                                : qsTr("Gouache-only")
                                            return "%1 · Transp %2% · Gran %3%".arg(compat).arg(transp).arg(gran)
                                        }
                                        color: KP.textMuted
                                        font.family: KP.fontUi
                                        font.pixelSize: KP.fontMicro
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: modelData.provenance || qsTr("Estimated optical data")
                                        color: KP.textSubtle
                                        font.family: KP.fontUi
                                        font.pixelSize: KP.fontMicro
                                        elide: Text.ElideRight
                                    }
                                }
                                KpButton {
                                    objectName: "pigmentAdd_" + modelData.id
                                    small: true
                                    label: qsTr("+ Mix")
                                    tip: qsTr("Add %1 to the active mixing deposit").arg(modelData.name)
                                    onClicked: if (root.mixer) root.mixer.addPigmentToActive(modelData.id, 1.0)
                                }
                                KpButton {
                                    objectName: "pigmentNew_" + modelData.id
                                    small: true
                                    label: qsTr("New")
                                    tip: qsTr("Place %1 as a new deposit on the mixing palette").arg(modelData.name)
                                    onClicked: if (root.mixer) root.mixer.addPigmentAsNewDeposit(modelData.id, 1.0)
                                }
                            }
                        }
                    }
                }
            }

            // ---- Saved Mixtures (independent of .koma documents)
            PanelSection {
                Layout.fillWidth: true
                title: qsTr("Saved Mixtures")
                hint: root.savedList.length === 1 ? qsTr("1 saved") : qsTr("%1 saved").arg(root.savedList.length)
                showRule: true
                contentTopMargin: KP.gapS

                ColumnLayout {
                    width: parent.width
                    spacing: KP.gapS

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KP.gapXS
                        KpTextField {
                            id: saveNameField
                            objectName: "mixerSaveNameField"
                            Layout.fillWidth: true
                            height: KP.controlHeightSmall + 2
                            placeholder: qsTr("Name current mixture…")
                        }
                        KpButton {
                            objectName: "mixerSaveButton"
                            small: true
                            variant: "solid"
                            label: qsTr("Save")
                            enabled: root.active && root.active.id !== undefined
                            onClicked: {
                                if (!root.mixer) return
                                const id = root.mixer.saveActiveMixture(saveNameField.text)
                                if (id.length > 0) saveNameField.text = ""
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.savedList.length === 0
                        text: qsTr("Saved mixtures are stored in your palette library across projects.")
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontXS
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: root.savedList
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: savedCol.implicitHeight + 8
                            radius: KP.radiusXS
                            color: KP.paneBgAlt
                            border.width: 1
                            border.color: KP.hairline

                            ColumnLayout {
                                id: savedCol
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 3

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: KP.gapS
                                    Rectangle {
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        radius: KP.radiusXS
                                        color: modelData.porcelainHex || "#888888"
                                        border.width: 1
                                        border.color: KP.border
                                    }
                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Text {
                                            width: parent.width
                                            text: modelData.name
                                            color: KP.text
                                            font.family: KP.fontUi
                                            font.pixelSize: KP.fontXS
                                            font.weight: KP.weightMedium
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            text: "%1 · %2".arg(modelData.mediumId === "watercolour" ? qsTr("Watercolour") : qsTr("Gouache"))
                                                .arg(modelData.summary)
                                            color: KP.textMuted
                                            font.family: KP.fontUi
                                            font.pixelSize: KP.fontMicro
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: KP.gapXS
                                    KpButton {
                                        objectName: "savedOpen_" + modelData.id
                                        small: true
                                        label: qsTr("Open")
                                        tip: qsTr("Open onto mixing palette as an editable deposit")
                                        onClicked: if (root.mixer) root.mixer.openSavedMixture(modelData.id)
                                    }
                                    KpButton {
                                        objectName: "savedBrush_" + modelData.id
                                        small: true
                                        label: qsTr("Brush")
                                        tip: qsTr("Load directly into the traditional brush")
                                        onClicked: if (root.mixer) root.mixer.loadSavedIntoBrush(modelData.id)
                                    }
                                    KpButton {
                                        objectName: "savedDup_" + modelData.id
                                        small: true
                                        label: qsTr("Dup")
                                        tip: qsTr("Duplicate saved mixture")
                                        onClicked: if (root.mixer) root.mixer.duplicateSavedMixture(modelData.id)
                                    }
                                    Item { Layout.fillWidth: true }
                                    KpButton {
                                        objectName: "savedDelete_" + modelData.id
                                        small: true
                                        variant: "danger"
                                        label: "×"
                                        tip: qsTr("Delete saved mixture")
                                        onClicked: if (root.mixer) root.mixer.deleteSavedMixture(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: KP.gapS }
        }
    }
}
