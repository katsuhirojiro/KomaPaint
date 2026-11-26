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
                                StudioChoice {
                                    id: blendChoice
                                    Layout.fillWidth: true
                                    objectName: "brushStudioComponentBlend"
                                    Accessible.name: qsTr("Component blend mode")
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
