import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

Rectangle {
    id: inspector
    required property BrushStudioModel draft
    required property bool textureMode
    property int page: 0
    readonly property var readback: draft.settingState
    readonly property var sections: draft.settingSections
    // Only view order changes between modes. Registry keys, values and scopes
    // still describe the same document, and the entire inspector is scrollable.
    readonly property var orderedSections: textureMode
        ? sections.filter(group => group.textureFirst).concat(sections.filter(group => !group.textureFirst))
        : sections
    color: KP.paneBg

    onTextureModeChanged: if (settingsView) settingsView.contentY = 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        StudioHeader { Layout.fillWidth: true; title: qsTr("Brush Settings") }
        SegmentedChoice {
            objectName: "brushStudioInspectorChoice"
            Layout.fillWidth: true
            Layout.margins: KP.gapM
            options: [qsTr("Settings"), qsTr("Creator exposure")]
            currentIndex: inspector.page
            onChosen: function(index) { inspector.page = index }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: inspector.page
            Flickable {
                id: settingsView
                objectName: "brushStudioSettingsScroll"
                // A viewport layer keeps software Shape nodes inside the scroll clip.
                layer.enabled: GraphicsInfo.api === GraphicsInfo.Software
                contentWidth: width
                contentHeight: form.implicitHeight + 2 * KP.panelInset
                clip: true
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: KpScrollBar { opacity: size < 1 ? 1 : 0 }

                Rectangle { parent: settingsView; anchors.fill: parent; z: -1; color: KP.paneBg }

                ColumnLayout {
                    id: form
                    x: KP.panelInset
                    y: KP.panelInset
                    width: Math.max(0, settingsView.width - 2 * KP.panelInset)
                    spacing: KP.gapXS
                    Text {
                        Layout.fillWidth: true
                        Layout.bottomMargin: KP.gapS
                        text: qsTr("Draft values only. These edits don’t change the brush in the painting workspace.")
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        objectName: "brushStudioUnsupportedNotice"
                        Layout.fillWidth: true
                        Layout.bottomMargin: KP.gapS
                        visible: inspector.readback.hasUnsupported
                        text: qsTr("Unavailable controls are kept in the brush file, but the Digital renderer does not use them.")
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontS
                        wrapMode: Text.WordWrap
                    }
                    PanelSection {
                        id: componentSection
                        objectName: "brushStudioSelectedComponentSection"
                        Layout.fillWidth: true
                        title: qsTr("Selected component")
                        hint: inspector.draft.selectedComponent.kindLabel || qsTr("None")
                        headerHeight: KP.controlHeight
                        collapsible: true
                        expanded: false
                        animateExpansion: false
                        contentHeight: componentForm.implicitHeight
                        ColumnLayout {
                            id: componentForm
                            width: parent.width
                            height: implicitHeight
                            spacing: KP.gapS
                            enabled: inspector.draft.selectedIndex >= 0
                            KpTextField {
                                id: componentName
                                objectName: "brushStudioSelectedComponentName"
                                Layout.fillWidth: true
                                text: inspector.draft.selectedComponent.name || ""
                                placeholder: qsTr("Component name")
                                Accessible.name: placeholder
                                onSubmitted: function(typed) {
                                    const selected = inspector.draft.selectedComponent
                                    if (selected.id && typed !== selected.name) inspector.draft.setComponentName(selected.id, typed)
                                    componentName.text = Qt.binding(function() { return inspector.draft.selectedComponent.name || "" })
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    color: KP.textMuted
                                    font.family: KP.fontUi
                                    font.pixelSize: KP.fontS
                                    text: qsTr("Blend")
                                }
                                KpButton {
                                    small: true
                                    glyph: "info"
                                    variant: "quiet"
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    tip: qsTr("How this mark combines with paint already on the layer. Normal lays new colour over the old; Multiply, Screen and Overlay mix the colours like a layer blend mode.")
                                }
                                StudioChoice {
                                    id: blendChoice
                                    Layout.fillWidth: true
                                    objectName: "brushStudioComponentBlend"
                                    Accessible.name: qsTr("Component blend mode")
                                    Accessible.description: qsTr("How this mark combines with paint already on the layer.")
                                    model: inspector.draft.componentBlends
                                    textRole: "label"
                                    valueRole: "value"
                                    enabled: inspector.draft.selectedComponent.supported
                                        && inspector.draft.selectedComponent.blendSupported
                                    function fromSelection() { return findValueIndex(inspector.draft.selectedComponent.blend) }
                                    function findValueIndex(value) { return model.findIndex(entry => entry.value === value) }
                                    currentIndex: fromSelection()
                                    onActivated: function(index) {
                                        inspector.draft.setComponentBlend(inspector.draft.selectedId, valueAt(index))
                                        currentIndex = Qt.binding(function() { return blendChoice.fromSelection() })
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    color: KP.textMuted
                                    font.family: KP.fontUi
                                    font.pixelSize: KP.fontS
                                    text: qsTr("Mask")
                                }
                                KpButton {
                                    small: true
                                    glyph: "info"
                                    variant: "quiet"
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    tip: qsTr("A texture, pattern or grayscale image that hides parts of this mark. The mask is sampled inside the tip, so texture scale and rotation change the grain without moving the mark.")
                                }
                                StudioChoice {
                                    id: maskChoice
                                    Layout.fillWidth: true
                                    objectName: "brushStudioComponentMask"
                                    Accessible.name: qsTr("Component mask")
                                    Accessible.description: qsTr("Choose a texture or grayscale image that hides parts of this mark.")
                                    model: [{ id: "", name: qsTr("None") }].concat(inspector.draft.maskCandidates)
                                    textRole: "name"
                                    valueRole: "id"
                                    enabled: inspector.draft.selectedIndex >= 0
                                    currentIndex: {
                                        const id = inspector.draft.selectedComponent.maskId || ""
                                        if (!id) return 0
                                        const at = inspector.draft.maskCandidates.findIndex(entry => entry.id === id)
                                        return at < 0 ? 0 : at + 1
                                    }
                                    onActivated: function(index) {
                                        inspector.draft.setComponentMask(inspector.draft.selectedId,
                                                                         index === 0 ? "" : model[index].id)
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    color: KP.textMuted
                                    font.family: KP.fontUi
                                    font.pixelSize: KP.fontS
                                    text: qsTr("Group")
                                }
                                KpButton {
                                    small: true
                                    glyph: "info"
                                    variant: "quiet"
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    tip: qsTr("The reusable group this mark belongs to. A group can choose one mark, a random mark, a weighted mix, a sequence or all of them for each stamp.")
                                }
                                StudioChoice {
                                    id: groupChoice
                                    Layout.fillWidth: true
                                    objectName: "brushStudioComponentGroup"
                                    Accessible.name: qsTr("Component group")
                                    Accessible.description: qsTr("Choose the group this mark belongs to.")
                                    model: [{ id: "", name: qsTr("No group") }].concat(inspector.draft.groups)
                                    textRole: "name"
                                    valueRole: "id"
                                    enabled: inspector.draft.selectedIndex >= 0
                                    currentIndex: {
                                        const id = inspector.draft.selectedComponent.groupId || ""
                                        if (!id) return 0
                                        const at = inspector.draft.groups.findIndex(entry => entry.id === id)
                                        return at < 0 ? 0 : at + 1
                                    }
                                    onActivated: function(index) {
                                        inspector.draft.assignToGroup(inspector.draft.selectedId,
                                                                      index === 0 ? "" : model[index].id)
                                    }
                                }
                            }
                            Text {
                                objectName: "brushStudioComponentSupport"
                                Layout.fillWidth: true
                                visible: inspector.draft.selectedIndex >= 0
                                    && (inspector.draft.selectedComponent.supportReason.length > 0
                                        || inspector.draft.selectedComponent.blendReason.length > 0)
                                text: inspector.draft.selectedComponent.supportReason.length > 0
                                    ? inspector.draft.selectedComponent.supportReason
                                    : inspector.draft.selectedComponent.blendReason
                                color: KP.textSubtle
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontXS
                                wrapMode: Text.WordWrap
                            }
                            Item { Layout.preferredHeight: KP.gapS }
                        }
                    }
                    PanelSection {
                        id: groupsSection
                        objectName: "brushStudioGroupsSection"
                        Layout.fillWidth: true
                        title: qsTr("Marks and groups")
                        hint: qsTr("%1 groups").arg(inspector.draft.groups.length)
                        headerHeight: KP.controlHeight
                        collapsible: true
                        expanded: false
                        animateExpansion: false
                        contentHeight: groupsForm.implicitHeight
                        ColumnLayout {
                            id: groupsForm
                            width: parent.width
                            height: implicitHeight
                            spacing: KP.gapS
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Groups reuse marks and choose how many marks one stamp draws.")
                                color: KP.textSubtle
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontXS
                                wrapMode: Text.WordWrap
                                Accessible.description: text
                            }
                            Repeater {
                                model: inspector.draft.groups
                                delegate: ColumnLayout {
                                    id: groupRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: KP.gapXS
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: KP.gapS
                                        KpTextField {
                                            Layout.fillWidth: true
                                            objectName: "brushStudioGroupName_" + groupRow.modelData.id
                                            text: groupRow.modelData.name
                                            Accessible.name: qsTr("Group name")
                                            Accessible.description: qsTr("A reusable name for this collection of marks.")
                                            onSubmitted: function(typed) {
                                                if (typed.length > 0 && typed !== groupRow.modelData.name)
                                                    inspector.draft.setGroupName(groupRow.modelData.id, typed)
                                            }
                                        }
                                        Text {
                                            text: qsTr("%1 marks").arg(groupRow.modelData.memberCount)
                                            color: KP.textSubtle
                                            font.family: KP.fontUi
                                            font.pixelSize: KP.fontXS
                                        }
                                        KpButton {
                                            small: true
                                            glyph: "trash"
                                            variant: "danger"
                                            tip: qsTr("Remove this group; its marks stay in the brush")
                                            onClicked: inspector.draft.removeGroup(groupRow.modelData.id)
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: KP.gapS
                                        Text {
                                            text: qsTr("Choice")
                                            color: KP.textMuted
                                            font.family: KP.fontUi
                                            font.pixelSize: KP.fontXS
                                        }
                                        KpButton {
                                            small: true
                                            glyph: "info"
                                            variant: "quiet"
                                            Layout.preferredWidth: 18
                                            Layout.preferredHeight: 18
                                            tip: qsTr("How a stamp chooses from this group: all marks, one random mark, a weighted pick, a repeating sequence, alternating marks or a combination.")
                                        }
                                        StudioChoice {
                                            Layout.fillWidth: true
                                            objectName: "brushStudioGroupMode_" + groupRow.modelData.id
                                            Accessible.name: qsTr("Group choice")
                                            Accessible.description: qsTr("How a stamp chooses marks from this group.")
                                            model: inspector.draft.selectionModes
                                            textRole: "label"
                                            valueRole: "value"
                                            currentIndex: model.findIndex(entry => entry.value === groupRow.modelData.mode)
                                            onActivated: function(index) {
                                                inspector.draft.setGroupMode(groupRow.modelData.id, valueAt(index))
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: KP.gapS
                                        Text {
                                            text: qsTr("Inside")
                                            color: KP.textMuted
                                            font.family: KP.fontUi
                                            font.pixelSize: KP.fontXS
                                        }
                                        KpButton {
                                            small: true
                                            glyph: "info"
                                            variant: "quiet"
                                            Layout.preferredWidth: 18
                                            Layout.preferredHeight: 18
                                            tip: qsTr("Place this group inside another group. Nested groups can choose marks in layers, like branches made of leaf groups.")
                                        }
                                        StudioChoice {
                                            Layout.fillWidth: true
                                            objectName: "brushStudioGroupParent_" + groupRow.modelData.id
                                            Accessible.name: qsTr("Parent group")
                                            Accessible.description: qsTr("Place this group inside another group.")
                                            model: [{ id: "", name: qsTr("Top level") }].concat(
                                                inspector.draft.groups.filter(entry => entry.id !== groupRow.modelData.id))
                                            textRole: "name"
                                            valueRole: "id"
                                            currentIndex: {
                                                const id = groupRow.modelData.parentId || ""
                                                if (!id) return 0
                                                const at = model.findIndex(entry => entry.id === id)
                                                return at < 0 ? 0 : at
                                            }
                                            onActivated: function(index) {
                                                inspector.draft.setGroupParent(groupRow.modelData.id,
                                                                               index === 0 ? "" : model[index].id)
                                            }
                                        }
                                    }
                                }
                            }
                            KpButton {
                                objectName: "brushStudioNewGroup"
                                small: true
                                glyph: "plus"
                                label: qsTr("New group")
                                tip: qsTr("Create a reusable group. Select a mark afterwards to place it inside.")
                                onClicked: inspector.draft.createGroupInParent(qsTr("New group"), "")
                            }
                            Item { Layout.preferredHeight: KP.gapS }
                        }
                    }
                    Repeater {
                        model: inspector.orderedSections
                        delegate: PanelSection {
                            id: group
                            required property var modelData
                            objectName: "brushStudioSection_" + modelData.title
                            Layout.fillWidth: true
                            title: modelData.title
                            headerHeight: KP.controlHeight
                            collapsible: true
                            expanded: inspector.textureMode ? modelData.textureFirst : modelData.tipFirst
                            animateExpansion: false
                            contentHeight: fields.height
                            hint: {
                                if (expanded || modelData.rows.length === 0) return ""
                                const first = modelData.rows[0]
                                const current = inspector.readback.values[first.key]
                                return current.available ? (first.editor === "choice" ? first.choices[current.value] : current.text + (first.unit.length > 0 ? " " + first.unit : "")) : ""
                            }
                            Loader {
                                id: fields
                                width: parent.width
                                height: item ? item.implicitHeight : 0
                                active: group.expanded
                                sourceComponent: ColumnLayout {
                                    spacing: KP.gapS
                                    Repeater {
                                        model: group.modelData.rows
                                        delegate: StudioParameter {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            draft: inspector.draft
                                            spec: modelData
                                            readback: inspector.readback.values[spec.key]
                                        }
                                    }
                                    Item { Layout.preferredHeight: KP.gapS }
                                }
                            }
                        }
                    }
                }
            }
            StudioExposure {
                objectName: "brushStudioExposure"
                draft: inspector.draft
            }
        }
        StudioHeader {
            Layout.fillWidth: true
            title: inspector.page === 0
                ? qsTr("%1 values differ from defaults").arg(inspector.readback.changedCount)
                : qsTr("%1 exposed by creator").arg(inspector.draft.exposedProperties.length)
        }
    }
}
