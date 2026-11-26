import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../themes"

// The one button of the interface.
//
// Variants keep the hierarchy readable at a glance:
//   ghost    — the default; no fill until you hover it
//   solid    — a filled control for the main action of a small group
//   primary  — the single affirmative action of a dialog
//   quiet    — icon-only or bare text, for toolbars and row commands
//   danger   — destructive actions
//
// `glyph` comes from KpGlyph (vector chrome) and `iconName` from KpIcon (the
// authored PNG tool/layer artwork); a button may use either or a text label.
// Buttons are focusable and activate on Space/Enter like any platform button,
// and the focus ring is drawn inside the padding so it never shifts layout.
AbstractButton {
    id: btn

    property string label: ""
    property string glyph: ""
    property string iconName: ""
    property int iconSize: KP.iconS
    property string tip: ""
    // ghost | solid | primary | quiet | danger
    property string variant: "ghost"
    property bool small: false
    // Selected state for toggles (e.g. the active tool).
    property bool toggled: false
    property bool busy: false

    readonly property bool primaryVariant: variant === "primary"
    readonly property bool dangerVariant: variant === "danger"
    readonly property bool quietVariant: variant === "quiet"
    readonly property bool solidVariant: variant === "solid" || primaryVariant || dangerVariant
    readonly property int pad: small ? KP.gapM : KP.gapL
    readonly property bool labelled: label.length > 0

    implicitHeight: small ? KP.controlHeightSmall : KP.controlHeight
    implicitWidth: {
        if (!labelled) return implicitHeight
        return Math.max(implicitHeight, metrics.advanceWidth + pad * 2 + (glyph.length > 0 || iconName.length > 0 ? iconSize + KP.gapS : 0))
    }
    padding: 0
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : KP.disabledOpacity
    Accessible.name: label.length > 0 ? label : tip
    Accessible.description: tip

    // Explanations must not depend on a mouse: focus and press expose the same
    // text for keyboard and touch users.
    ToolTip.text: tip
    ToolTip.visible: (hovered || activeFocus || pressed) && tip.length > 0
    ToolTip.delay: hovered ? 500 : 0

    TextMetrics {
        id: metrics
        font.family: btn.font.family
        font.pixelSize: btn.font.pixelSize
        font.weight: btn.font.weight
        text: btn.label
    }

    background: Rectangle {
        radius: KP.radiusS
        color: btn.toggled ? KP.accentWash
             : btn.primaryVariant ? (btn.down ? Qt.darker(KP.accent, 1.15) : (btn.hovered ? Qt.lighter(KP.accent, 1.08) : KP.accent))
             : btn.dangerVariant ? (btn.down || btn.hovered ? Qt.rgba(0.78, 0.30, 0.30, btn.down ? 0.30 : 0.18) : "transparent")
             : btn.solidVariant ? (btn.down ? KP.buttonBgPress : (btn.hovered ? KP.buttonBgHover : KP.buttonBg))
             : btn.quietVariant ? (btn.down ? KP.pressOverlay : (btn.hovered || btn.toggled ? KP.hoverOverlayStrong : "transparent"))
             : (btn.down ? KP.pressOverlay : (btn.hovered ? KP.hoverOverlay : "transparent"))
        border.width: btn.activeFocus ? KP.focusWidth : (btn.solidVariant || btn.toggled ? 1 : 0)
        border.color: btn.activeFocus ? (btn.primaryVariant ? KP.textOnAccent : KP.focusRing)
                    : btn.toggled ? KP.accent
                    : btn.solidVariant ? KP.border : "transparent"
        Behavior on color { ColorAnimation { duration: KP.durationFast; easing.type: KP.easingCurve } }
        Behavior on border.color { ColorAnimation { duration: KP.durationFast } }
    }

    contentItem: Item {
        implicitWidth: layout.implicitWidth
        implicitHeight: layout.implicitHeight

        RowLayout {
            id: layout
            anchors.centerIn: parent
            spacing: (glyph.length > 0 || iconName.length > 0) && btn.labelled ? KP.gapS : 0

            KpGlyph {
                Layout.alignment: Qt.AlignVCenter
                visible: btn.glyph.length > 0
                name: btn.glyph
                size: btn.iconSize
                color: btn.primaryVariant ? KP.textOnAccent
                     : btn.toggled ? KP.iconSelected
                     : btn.dangerVariant ? KP.error
                     : !btn.enabled ? KP.iconDisabled
                     : btn.hovered ? KP.iconHover : KP.iconNormal
            }
            KpIcon {
                Layout.alignment: Qt.AlignVCenter
                visible: btn.iconName.length > 0
                name: btn.iconName
                size: btn.iconSize
                color: btn.primaryVariant ? KP.textOnAccent
                     : btn.toggled ? KP.iconSelected
                     : !btn.enabled ? KP.iconDisabled
                     : btn.hovered ? KP.iconHover : KP.iconNormal
            }
            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: btn.labelled
                text: btn.label
                color: btn.primaryVariant ? KP.textOnAccent
                     : btn.toggled ? KP.textAccent
                     : btn.dangerVariant ? KP.error
                     : KP.text
                font.pixelSize: btn.font.pixelSize
                font.weight: btn.font.weight
                font.family: btn.font.family
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
