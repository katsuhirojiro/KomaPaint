import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// The application-level Speed Dynamics controls. Kept in one component so the
// normal brush panel and the native .kb brush panel cannot drift apart.
PanelSection {
    id: root

    property var inputSettings: null
    property string objectPrefix: ""

    // The default panel keeps the historical object names; the native panel
    // gets a native* prefix with the same capitalisation.
    function prefixed(name) {
        return root.objectPrefix.length > 0
            ? root.objectPrefix + name.charAt(0).toUpperCase() + name.slice(1) : name
    }

    objectName: root.prefixed("speedDynamicsSection")
    title: qsTr("Speed Dynamics")
    hint: root.inputSettings && root.inputSettings.speedDynamics !== 0
          ? qsTr("active") : qsTr("neutral")
    contentTopMargin: KP.gapS
    contentBottomMargin: KP.gapS

    // Coalesce slider drags and checkbox clicks into one settings write.
    Timer {
        id: speedSaveTimer
        interval: 350
        repeat: false
        onTriggered: if (root.inputSettings) root.inputSettings.saveToSettings()
    }

    Item {
        width: parent.width
        height: speedDynamicsColumn.height

        Column {
            id: speedDynamicsColumn
            width: parent.width
            spacing: KP.gapXS

            PropertySlider {
                objectName: root.prefixed("speedDynamicsSlider")
                width: parent.width
                label: qsTr("Speed influence")
                from: -100
                to: 100
                value: root.inputSettings ? root.inputSettings.speedDynamics : 0
                resetValue: 0
                help: qsTr("Moves the link between how fast you draw and how the brush behaves. Centre means speed changes nothing. Move left for slow strokes to come out thicker and stronger, or right for fast strokes to come out thicker and stronger. Press and hold Shift for fine tuning; double-click to return to neutral.")
                onEdited: function(v) {
                    if (!root.inputSettings) return
                    root.inputSettings.speedDynamics = v
                    speedSaveTimer.restart()
                }
            }

            Text {
                objectName: root.prefixed("speedDynamicsSummary")
                width: parent.width
                text: root.inputSettings ? root.inputSettings.speedDynamicsSummary : ""
                color: KP.textMuted
                font.pixelSize: KP.fontXS
                font.family: KP.fontUi
                wrapMode: Text.Wrap
                lineHeight: 1.15
            }

            Text {
                width: parent.width
                text: qsTr("Your pressure-sensitive pen still uses its real pressure. Speed is added on top and never replaces it.")
                color: KP.textSubtle
                font.pixelSize: KP.fontXS
                font.family: KP.fontUi
                wrapMode: Text.Wrap
            }

            HelpCheckBox {
                objectName: root.prefixed("speedAffectsSize")
                width: parent.width
                text: qsTr("Let speed change size")
                help: qsTr("When on, the Speed influence slider can make slow or fast strokes wider or narrower. Turn it off to keep this brush at one width no matter how fast you draw.")
                checked: root.inputSettings ? root.inputSettings.speedAffectsSize : true
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedAffectsSize = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedInvertSize")
                width: parent.width
                visible: root.inputSettings && root.inputSettings.speedAffectsSize
                text: qsTr("Size: use the opposite direction")
                help: qsTr("Flip only the size response. With the master on the slow-strong side this makes slow strokes thinner while the other chosen properties still get stronger.")
                checked: root.inputSettings ? root.inputSettings.speedInvertSize : false
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedInvertSize = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedAffectsOpacity")
                width: parent.width
                text: qsTr("Let speed change opacity")
                help: qsTr("When on, speed can make a stroke more or less solid. Turn it off when the paint should keep the same opacity from the first mark to the last.")
                checked: root.inputSettings ? root.inputSettings.speedAffectsOpacity : true
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedAffectsOpacity = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedInvertOpacity")
                width: parent.width
                visible: root.inputSettings && root.inputSettings.speedAffectsOpacity
                text: qsTr("Opacity: use the opposite direction")
                help: qsTr("Flip only the opacity response, so a stroke can become more solid as it speeds up while its size still follows the master direction.")
                checked: root.inputSettings ? root.inputSettings.speedInvertOpacity : false
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedInvertOpacity = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedAffectsFlow")
                width: parent.width
                text: qsTr("Let speed change flow")
                help: qsTr("Flow is how much paint each mark releases. Turn this on for a dry brush that loads up or runs dry as your speed changes.")
                checked: root.inputSettings ? root.inputSettings.speedAffectsFlow : false
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedAffectsFlow = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedInvertFlow")
                width: parent.width
                visible: root.inputSettings && root.inputSettings.speedAffectsFlow
                text: qsTr("Flow: use the opposite direction")
                help: qsTr("Flip only the flow response without changing size or opacity.")
                checked: root.inputSettings ? root.inputSettings.speedInvertFlow : false
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedInvertFlow = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedAffectsSpacing")
                width: parent.width
                text: qsTr("Let speed change spacing")
                help: qsTr("Spacing is the distance between the brush's marks. Turn this on for a stroke that becomes denser or more broken up with speed. It changes the texture, not the brush width.")
                checked: root.inputSettings ? root.inputSettings.speedAffectsSpacing : false
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedAffectsSpacing = checked
                    speedSaveTimer.restart()
                }
            }
            HelpCheckBox {
                objectName: root.prefixed("speedInvertSpacing")
                width: parent.width
                visible: root.inputSettings && root.inputSettings.speedAffectsSpacing
                text: qsTr("Spacing: use the opposite direction")
                help: qsTr("Flip only the spacing response, so slower strokes can break up while faster strokes lay marks closer together.")
                checked: root.inputSettings ? root.inputSettings.speedInvertSpacing : false
                onToggled: {
                    if (!root.inputSettings) return
                    root.inputSettings.speedInvertSpacing = checked
                    speedSaveTimer.restart()
                }
            }
        }
    }
}
