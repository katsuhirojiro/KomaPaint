import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import KomaPaint 1.0
import "../../components"
import "../../themes"

Rectangle {
    id: rail
    required property BrushStudioModel draft
    readonly property bool filtered: search.text.trim().length > 0
    color: KP.paneBg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        StudioHeader {
            Layout.fillWidth: true
            title: qsTr("Components")
            actions: [
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: rail.draft.componentCount
                    color: KP.textSubtle
                    font.family: KP.fontMono
                    font.pixelSize: KP.fontXS
                },
                KpButton {
                    small: true
                    glyph: "dots-h"
                    tip: qsTr("Selected component actions")
                    enabled: rail.draft.selectedIndex >= 0
                    onClicked: more.popup(this, 0, height)
                    KpMenu {
                        id: more
                        KpMenuItem {
                            text: qsTr("Duplicate component")
                            objectName: "brushStudioDuplicateComponent"
                            onTriggered: rail.draft.duplicateComponent(rail.draft.selectedId)
                        }
                    }
                }
            ]
        }
        KpTextField {
            id: search
            objectName: "brushStudioComponentSearch"
            Layout.fillWidth: true
            Layout.margins: KP.gapM
            placeholder: qsTr("Search components")
            Accessible.name: placeholder
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: KP.panelInset
            Layout.rightMargin: KP.panelInset
            visible: rail.height > 222
            text: rail.filtered ? qsTr("Clear search to reorder") : qsTr("Draft stack · in document order")
            elide: Text.ElideRight
            color: KP.textSubtle
            font.family: KP.fontUi
            font.pixelSize: KP.fontXS
        }
        ListView {
            id: stack
            objectName: "brushStudioComponentStack"
            // A viewport layer keeps software Shape nodes inside the scroll clip.
            layer.enabled: GraphicsInfo.api === GraphicsInfo.Software
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: KpScrollBar { policy: ScrollBar.AsNeeded; opacity: size < 1 ? 1 : 0 }
            model: {
                const rows = rail.draft.components
                if (!rail.filtered) return rows
                const query = search.text.trim().toLocaleLowerCase()
                return rows.filter(row => row.name.toLocaleLowerCase().includes(query) || row.kindLabel.toLocaleLowerCase().includes(query))
            }
            currentIndex: model.findIndex(row => row.index === rail.draft.selectedIndex)
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: 0
            preferredHighlightEnd: height
            keyNavigationEnabled: false

            Rectangle { parent: stack; anchors.fill: parent; z: -1; color: KP.paneBg }

            delegate: Item {
                id: row
                required property var modelData
                width: ListView.view.width - KP.gapXS
                height: 46
                readonly property bool selected: rail.draft.selectedId === modelData.id
                Rectangle {
                    anchors.fill: parent
                    color: row.selected ? KP.rowSelected : select.hovered ? KP.hoverOverlay : "transparent"
                    Rectangle { visible: row.selected; height: parent.height; width: 2; color: KP.accent }
                }
                ItemDelegate {
                    id: select
                    objectName: "brushStudioRow_" + row.modelData.id
                    anchors.left: parent.left
                    anchors.right: eye.left
                    height: parent.height
                    padding: KP.gapM
                    hoverEnabled: true
                    Accessible.name: row.modelData.name
                    onClicked: rail.draft.selectedIndex = row.modelData.index
                    background: Rectangle {
                        color: "transparent"
                        border.width: select.activeFocus ? KP.focusWidth : 0
                        border.color: KP.focusRing
                    }
                    contentItem: RowLayout {
                        spacing: KP.gapM
                        Rectangle {
                            implicitWidth: 28
                            implicitHeight: 28
                            radius: KP.radiusXS
                            color: KP.panelInsetBg
                            Layout.alignment: Qt.AlignVCenter
                            KpGlyph { anchors.centerIn: parent; name: row.modelData.texture ? "grid" : "brush" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: KP.gapXS
                            opacity: row.modelData.visible ? 1 : KP.disabledOpacity
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.name
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: KP.text
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontM
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.resourceWidth > 0
                                      ? qsTr("%1 · %2 × %3").arg(row.modelData.kindLabel).arg(row.modelData.resourceWidth).arg(row.modelData.resourceHeight)
                                      : row.modelData.kindLabel
                                elide: Text.ElideRight
                                color: KP.textMuted
                                font.family: KP.fontUi
                                font.pixelSize: KP.fontXS
                            }
                        }
                    }
                }
                KpButton {
                    id: eye
                    objectName: "brushStudioVisibility_" + row.modelData.id
                    anchors.right: parent.right
                    anchors.rightMargin: KP.gapS
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    glyph: row.modelData.visible ? "eye" : "eye-off"
                    tip: row.modelData.visible ? qsTr("Hide %1 in the draft").arg(row.modelData.name) : qsTr("Show %1 in the draft").arg(row.modelData.name)
                    onClicked: rail.draft.setComponentVisible(row.modelData.id, !row.modelData.visible)
                }
            }
            Text {
                visible: stack.count === 0
                anchors.top: parent.top
                anchors.topMargin: KP.gapL
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.max(0, parent.width - 2 * KP.panelInset)
                text: rail.filtered ? qsTr("No matching components") : qsTr("No components in this draft")
                color: KP.textSubtle
                font.family: KP.fontUi
                font.pixelSize: KP.fontS
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: KP.gapM
            spacing: KP.gapXS
            KpButton {
                id: add
                objectName: "brushStudioAddComponent"
                Layout.fillWidth: true
                small: true
                glyph: "plus"
                label: rail.width >= 226 ? qsTr("Add component") : ""
                variant: "solid"
                tip: qsTr("Add a built-in component. Importing image files is unavailable.")
                onClicked: addKinds.popup(add, 0, height)
                StudioComponentMenu { id: addKinds; objectName: "brushStudioAddMenu"; draft: rail.draft }
            }
            KpButton {
                objectName: "brushStudioComponentUp"
                small: true
                glyph: "layer-up"
                tip: qsTr("Move selected component one row up")
                enabled: !rail.filtered && rail.draft.selectedIndex > 0
                onClicked: rail.draft.moveComponent(rail.draft.selectedId, rail.draft.selectedIndex - 1)
            }
            KpButton {
                objectName: "brushStudioComponentDown"
                small: true
                glyph: "layer-down"
                tip: qsTr("Move selected component one row down")
                enabled: !rail.filtered && rail.draft.selectedIndex >= 0 && rail.draft.selectedIndex + 1 < rail.draft.componentCount
                onClicked: rail.draft.moveComponent(rail.draft.selectedId, rail.draft.selectedIndex + 1)
            }
            KpButton {
                objectName: "brushStudioRemoveComponent"
                small: true
                glyph: "trash"
                variant: "danger"
                tip: qsTr("Remove selected component (undoable)")
                enabled: rail.draft.selectedIndex >= 0
                onClicked: rail.draft.removeComponent(rail.draft.selectedId)
            }
        }
        StudioHeader {
            objectName: "brushStudioLibrary"
            Layout.fillWidth: true
            title: qsTr("Brush Library")
            actions: KpButton {
                objectName: "brushStudioBrowseLibrary"
                small: true
                enabled: false
                glyph: "folder"
                label: rail.width >= 212 ? qsTr("Unavailable") : ""
                tip: qsTr("Browsing and saving Studio brushes in the library are unavailable. Use local .kb files instead.")
            }
        }
    }
}
