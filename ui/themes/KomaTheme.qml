pragma Singleton
import QtQuick

// KomaPaint visual design tokens.
// The palette is deliberately restrained: a narrow band of near-black
// charcoals with a single accent and muted functional colors. Avoid
// introducing new color literals outside of this file — always reference
// KP.pane, KP.textMuted, etc. so the entire application can be restyled
// from one place.
QtObject {
    id: root

    // --- Surfaces -----------------------------------------------------------
    readonly property color windowBg: "#1f2124"   // outermost frame
    readonly property color menubarBg: "#2a2d31"   // top menu bar
    readonly property color toolbarBg: "#2d3035"   // main toolbar
    readonly property color paneBg: "#31343a"   // dock panels
    readonly property color paneBgAlt: "#2a2d31"   // panel inner list rows
    readonly property color paneHeaderBg: "#272a2e"   // panel title strip
    readonly property color canvasBg: "#4a4d52"   // desk area behind document
    readonly property color canvasPaper: "#ffffff"   // paper color when non-transparent
    readonly property color inputBg: "#26282c"   // text fields, dropdowns
    readonly property color buttonBg: "#35383d"
    readonly property color buttonBgHover: "#3d4147"
    readonly property color buttonBgPress: "#4a4e55"
    readonly property color toolActive: "#4d6b9c"   // selected tool swatch

    // --- Borders ------------------------------------------------------------
    readonly property color border: "#15171a"
    readonly property color borderLight: "#3a3d42"
    readonly property color separator: "#1b1d20"

    // --- Text ---------------------------------------------------------------
    readonly property color text: "#d9dadd"
    readonly property color textMuted: "#92959a"
    readonly property color textSubtle: "#6d7075"
    readonly property color textInverted: "#101114"
    readonly property color textAccent: "#7ea4d6"

    // --- Semantic / accent --------------------------------------------------
    readonly property color accent: "#6b9bd1"
    readonly property color accentSoft: "#476992"
    readonly property color selection: "#3e6aa3"
    readonly property color warning: "#c8913a"
    readonly property color error: "#b84f4f"

    // --- Fonts --------------------------------------------------------------
    readonly property string fontUi: "Segoe UI, Inter, Noto Sans, Ubuntu, Arial, sans-serif"
    readonly property string fontMono: "Consolas, SF Mono, JetBrains Mono, Menlo, monospace"

    readonly property int fontXS: 10
    readonly property int fontS: 11
    readonly property int fontM: 12
    readonly property int fontL: 13
    readonly property int fontXL: 15

    // --- Spacing ------------------------------------------------------------
    readonly property int gapXS: 2
    readonly property int gapS: 4
    readonly property int gapM: 8
    readonly property int gapL: 12
    readonly property int gapXL: 16

    // --- Radii (kept tight on purpose, this is not a rounded-card app) -----
    readonly property int radiusS: 2
    readonly property int radiusM: 3

    // --- Sizing rails -------------------------------------------------------
    readonly property int toolbarHeight: 36
    readonly property int menubarHeight: 28
    readonly property int statusbarHeight: 22
    readonly property int panelHeaderH: 24
    readonly property int toolButtonSize: 30
    readonly property int paletteWidth: 52
}
