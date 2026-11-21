import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

Rectangle {
    id: editor
    required property BrushStudioModel draft
    signal importImageRequested()
    property string modeOverride: ""
    readonly property var selected: draft.selectedComponent
    readonly property string mode: modeOverride || (selected.texture ? "texture" : "tip")
    readonly property bool matching: draft.selectedIndex >= 0 && (mode === "texture" ? !!selected.texture : mode === "tip" && !selected.texture)
    readonly property bool showZoom: width >= 350
    property real magnification: 1
    color: KP.canvasBg
    clip: true

    Connections {
        target: editor.draft
        function onSelectionChanged() { editor.modeOverride = ""; editor.magnification = 1 }
    }

    function selectMode(next) {
        // Choosing a Tip/Texture mode also selects a matching real component
        // when one exists. Nodes and empty modes keep the current selection.
        if (next !== "nodes" && draft.selectComponentInMode(next)) modeOverride = ""
        else modeOverride = next
        magnification = 1
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            visible: editor.width >= 320 && editor.height >= 190
            Layout.preferredWidth: KP.toolButtonSize
            Layout.fillHeight: true
            color: KP.toolRailBg
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: KP.gapS
                spacing: KP.gapS
                Repeater {
                    model: ["tools/brush", "tools/pencil", "tools/eraser", "tools/selection", "tools/transform"]
                    KpButton {
                        required property string modelData
                        iconName: modelData
                        enabled: false
                        tip: qsTr("Pixel editing tools are unavailable")
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: KP.tabStripHeight
                color: KP.paneHeaderBg
                RowLayout {
                    anchors.fill: parent
                    anchors.rightMargin: KP.gapS
                    spacing: KP.gapS
                    ListView {
                        id: tabs
                        objectName: "brushStudioComponentTabs"
                        // A viewport layer keeps software Shape nodes inside the scroll clip.
                        layer.enabled: GraphicsInfo.api === GraphicsInfo.Software
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: ListView.Horizontal
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        model: editor.draft.components
                        currentIndex: editor.draft.selectedIndex
                        highlightRangeMode: ListView.ApplyRange
                        preferredHighlightBegin: 0
                        preferredHighlightEnd: width
                        keyNavigationEnabled: false
                        ScrollBar.horizontal: KpScrollBar { policy: ScrollBar.AsNeeded; opacity: size < 1 ? 1 : 0 }
                        Rectangle { parent: tabs; anchors.fill: parent; z: -1; color: KP.paneHeaderBg }

                        delegate: ItemDelegate {
                            id: tab
                            required property var modelData
                            objectName: "brushStudioTab_" + modelData.id
                            height: ListView.view.height
                            width: Math.max(80, Math.min(170, implicitWidth))
                            padding: KP.gapM
                            hoverEnabled: true
                            readonly property bool selected: editor.draft.selectedId === modelData.id
                            Accessible.name: modelData.name
                            onClicked: {
                                editor.modeOverride = ""
                                editor.draft.selectedIndex = modelData.index
                            }
                            background: Rectangle {
                                color: tab.selected ? KP.paneBg : tab.hovered ? KP.hoverOverlay : KP.paneHeaderBg
                                border.color: KP.focusRing
                                border.width: tab.activeFocus ? KP.focusWidth : 0
                                Rectangle {
                                    visible: tab.selected
                                    width: parent.width
                                    height: 2
                                    color: KP.accent
                                }
                            }
                            contentItem: Text {
                                text: tab.modelData.name
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: tab.selected ? KP.text : KP.textMuted
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontM
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                    KpButton {
                        id: addTab
                        small: true
                        glyph: "plus"
                        tip: qsTr("Add component")
                        onClicked: tabKinds.popup(addTab, 0, height)
                        StudioComponentMenu { id: tabKinds; draft: editor.draft }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: KP.panelHeaderH + KP.gapXS
                color: KP.toolbarBg
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: KP.gapS
                    anchors.rightMargin: KP.gapS
                    spacing: KP.gapS
                    SegmentedChoice {
                        objectName: "brushStudioEditorModes"
                        Layout.preferredWidth: 152
                        options: [qsTr("Tip"), qsTr("Texture"), qsTr("Nodes")]
                        currentIndex: editor.mode === "texture" ? 1 : editor.mode === "nodes" ? 2 : 0
                        onChosen: function(index) { editor.selectMode(["tip", "texture", "nodes"][index]) }
                    }
                    Item { Layout.fillWidth: true }
                    KpButton {
                        objectName: "brushStudioUndo"
                        small: true
                        glyph: "undo"
                        tip: qsTr("Undo brush edit")
                        enabled: editor.draft.canUndo
                        onClicked: editor.draft.undo()
                    }
                    KpButton {
                        objectName: "brushStudioRedo"
                        small: true
                        glyph: "redo"
                        tip: qsTr("Redo brush edit")
                        enabled: editor.draft.canRedo
                        onClicked: editor.draft.redo()
                    }
                    KpButton {
                        visible: editor.showZoom
                        small: true
                        glyph: "zoom-out"
                        tip: qsTr("Zoom out of the resource sample")
                        enabled: asset.visible && editor.magnification > 0.5
                        onClicked: editor.magnification -= 0.25
                    }
                    KpButton {
                        objectName: "brushStudioZoomIn"
                        visible: editor.showZoom
                        small: true
                        glyph: "zoom-in"
                        tip: qsTr("Zoom into the resource sample")
                        enabled: asset.visible && editor.magnification < 3
                        onClicked: editor.magnification += 0.25
                    }
                    KpButton {
                        objectName: "brushStudioFit"
                        visible: editor.showZoom
                        small: true
                        glyph: "zoom-fit"
                        label: editor.width >= 444 ? qsTr("Fit") : ""
                        tip: qsTr("Fit the resource sample in the workspace")
                        enabled: asset.visible && editor.magnification !== 1
                        onClicked: editor.magnification = 1
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                BrushStudioAssetView {
                    id: asset
                    objectName: "brushStudioAsset"
                    anchors.fill: parent
                    anchors.topMargin: KP.panelHeaderH
                    draft: editor.draft
                    tiled: editor.mode === "texture"
                    magnification: editor.magnification
                    checkerLight: KP.transparencyLight
                    checkerDark: KP.transparencyDark
                    visible: hasImage && editor.matching
                }
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: KP.gapM
                    text: {
                        if (editor.mode === "nodes") return qsTr("NODES  /  Editor unavailable")
                        const heading = editor.mode === "texture" ? qsTr("TEXTURE") : qsTr("TIP")
                        if (!editor.matching) return heading
                        const row = editor.selected
                        const format = asset.hasImage ? qsTr(" · %1 × %2 · %3").arg(row.resourceWidth).arg(row.resourceHeight).arg(row.resourceFormat) : ""
                        return heading + "  /  " + row.name + format
                    }
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: KP.text
                    font.family: KP.fontMono
                    font.pixelSize: KP.fontXS
                }
                Text {
                    objectName: "brushStudioEditorNotice"
                    visible: !asset.visible
                    anchors.centerIn: parent
                    width: Math.max(0, Math.min(336, parent.width - 2 * KP.gapL))
                    text: editor.mode === "nodes" ? qsTr("Node graph editing is unavailable.")
                        : !editor.matching ? (editor.mode === "texture" ? qsTr("Add a texture component to view its stored tile here.") : qsTr("Select or add a tip component."))
                        : qsTr("This component has no stored pixels. Edit its available settings in the inspector.")
                    textFormat: Text.PlainText
                    color: KP.textMuted
                    font.family: KP.fontUi
                    font.pixelSize: KP.fontM
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: KP.controlHeightSmall
                color: KP.toolbarBg
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: KP.gapM
                    anchors.rightMargin: KP.gapS
                    Text {
                        Layout.fillWidth: true
                        text: asset.visible ? (editor.mode === "texture" ? qsTr("Stored resource · repeated tiles · read-only") : qsTr("Stored resource · read-only"))
                              : qsTr("Pixel and node tools unavailable")
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        color: KP.textSubtle
                        font.family: KP.fontUi
                        font.pixelSize: KP.fontXS
                    }
                    KpButton {
                        objectName: "brushStudioLoadTipImage"
                        visible: editor.matching && editor.draft.canImportTipImage
                        small: true
                        label: qsTr("Load image…")
                        tip: qsTr("Replace this component’s stored tip pixels")
                        onClicked: editor.importImageRequested()
                    }
                }
            }
        }
    }
}
