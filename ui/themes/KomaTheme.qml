pragma Singleton
import QtQuick

// KomaPaint visual design tokens.
//
// Every colour in the interface comes from here: surfaces, borders, text, the
// accent and the icon tints. Two complete themes are defined — "dark" (the
// default) and "light" — and `mode` selects one. Because every token is a
// binding on `isDark`, switching the theme re-colours the whole window at
// once, and MainWindow.qml persists the choice and restores it at start-up.
//
// Avoid introducing new colour literals outside of this file — always
// reference KP.paneBg, KP.textMuted, etc. so the entire application can be
// restyled from one place. The palette is deliberately restrained: a narrow
// band of near-black charcoals (or near-white greys in the light theme) with
// a single accent and muted functional colours. Pure #ffffff is reserved for
// things that are white by definition (paper, the inside of a selection);
// text and icons sit a little below it so nothing glares.
QtObject {
    id: root

    // --- Theme selection ----------------------------------------------------
    // "dark" or "light". Written by the Preferences dialog and by
    // MainWindow.qml when it restores the stored preference.
    property string mode: "dark"
    readonly property bool isDark: mode !== "light"
    readonly property var modeNames: ["dark", "light"]

    // --- Surfaces -----------------------------------------------------------
    readonly property color windowBg: isDark ? "#1f2124" : "#e8eaed"   // outermost frame
    readonly property color menubarBg: isDark ? "#2a2d31" : "#dfe2e6"  // top menu bar
    readonly property color toolbarBg: isDark ? "#2d3035" : "#e3e6e9"  // main toolbar
    readonly property color paneBg: isDark ? "#31343a" : "#eef0f2"     // dock panels
    readonly property color paneBgAlt: isDark ? "#2a2d31" : "#e2e5e9"  // panel inner list rows
    readonly property color paneHeaderBg: isDark ? "#272a2e" : "#d9dde1" // panel title strip
    readonly property color canvasBg: isDark ? "#4a4d52" : "#b6babe"   // desk area behind document
    readonly property color canvasPaper: "#ffffff"                     // paper colour when non-transparent
    // Backing behind the project-type previews in the New Project dialog.
    // That artwork is drawn untinted, so it needs a light backing in both
    // themes; a slightly greyed paper keeps four bright cards from glaring in
    // the dark theme.
    readonly property color previewPaper: isDark ? "#dfe3e8" : "#ffffff"
    readonly property color inputBg: isDark ? "#26282c" : "#ffffff"    // text fields, dropdowns
    readonly property color buttonBg: isDark ? "#35383d" : "#d9dde1"
    readonly property color buttonBgHover: isDark ? "#3d4147" : "#cbcfd4"
    readonly property color buttonBgPress: isDark ? "#4a4e55" : "#bdc2c8"
    // The canvas paints the transparency checkerboard itself; these are its
    // two colours (light squares, dark squares).
    readonly property color transparencyLight: isDark ? "#767a81" : "#ffffff"
    readonly property color transparencyDark: isDark ? "#5a5d63" : "#e4e6e9"
    // Selected swatch / row. The dark theme deepens it a little so the light
    // icon tint on top keeps a comfortable 4.7:1 contrast; the light theme
    // keeps a pale blue tint under dark text.
    readonly property color toolActive: isDark ? "#42608f" : "#c3d7ec"

    // --- Borders ------------------------------------------------------------
    readonly property color border: isDark ? "#15171a" : "#a9aeb4"
    readonly property color borderLight: isDark ? "#3a3d42" : "#c9ced3"
    readonly property color separator: isDark ? "#1b1d20" : "#bfc4c9"

    // --- Text ---------------------------------------------------------------
    readonly property color text: isDark ? "#d9dadd" : "#26292d"
    readonly property color textMuted: isDark ? "#92959a" : "#565a60"
    readonly property color textSubtle: isDark ? "#6d7075" : "#7c8086"
    readonly property color textInverted: isDark ? "#101114" : "#ffffff"
    readonly property color textAccent: isDark ? "#7ea4d6" : "#1f5c96"
    // Text drawn on top of the accent / a selected row. In the dark theme the
    // selection is a saturated blue, in the light theme a pale blue tint, so
    // "one white for everything" would be unreadable in one of them.
    readonly property color textOnActive: isDark ? "#ffffff" : "#14324f"
    // Secondary text on the same background (counts, sizes, percentages).
    readonly property color textOnActiveMuted: isDark ? "#e4e9f2" : "#3c5a78"
    readonly property color textOnAccent: "#ffffff"

    // --- Semantic / accent --------------------------------------------------
    readonly property color accent: isDark ? "#6b9bd1" : "#1f5c93"
    readonly property color accentSoft: isDark ? "#476992" : "#9fbedd"
    readonly property color selection: isDark ? "#3e6aa3" : "#2f6ea8"
    readonly property color warning: isDark ? "#c8913a" : "#8a5c10"
    readonly property color error: isDark ? "#b84f4f" : "#9c3232"

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
    readonly property int toolButtonSize: 34   // one cell of the tool palette
    readonly property int toolIconSize: 22
    readonly property int paletteGap: 2
    readonly property int paletteWidth: 2 * toolButtonSize + paletteGap + 2 * 5
    readonly property int leftDockWidth: 236   // Sub Tool / Tool Property / Brush Size column
    readonly property int controlHeight: 24    // value fields and small buttons inside panels

    // --- Icon tints ---------------------------------------------------------
    // The icons in ui/assets/icons are single-colour artwork on transparency:
    // only their shape is used and the interface supplies the colour, which is
    // how one file serves the normal, hover, selected and disabled states.
    // (Full-colour artwork is never tinted — see KpIcon.qml.)
    //
    // Each state has a per-theme default, below, and an optional override that
    // the user can set in Preferences. An override is stored as a hex string;
    // an empty string means "use the theme default". Keeping the defaults in
    // this file (rather than in the preferences dialog) means both themes stay
    // coherent with the rest of their palette.
    property string iconNormalPref: ""
    property string iconHoverPref: ""
    property string iconSelectedPref: ""
    property string iconDisabledPref: ""

    // Dark theme: a soft grey for rest, one step brighter under the pointer,
    // a light shade of the accent for the selected item (the selected cell is
    // itself a saturated blue, so the accent at full strength would sink into
    // it), and a muted grey for things that cannot be used yet.
    readonly property color iconNormalDefault: isDark ? "#b9bcc1" : "#4a4d52"
    readonly property color iconHoverDefault: isDark ? "#dcdde0" : "#24272b"
    readonly property color iconSelectedDefault: isDark ? "#cfe0f6" : "#1f5c93"
    readonly property color iconDisabledDefault: isDark ? "#6b6e74" : "#9ea2a7"

    readonly property color iconNormal: root.tint(iconNormalPref, iconNormalDefault)
    readonly property color iconHover: root.tint(iconHoverPref, iconHoverDefault)
    readonly property color iconSelected: root.tint(iconSelectedPref, iconSelectedDefault)
    readonly property color iconDisabled: root.tint(iconDisabledPref, iconDisabledDefault)
    // A toggle that is switched off (e.g. the eye of a hidden layer). Not a
    // preference: it is a state of the icon, derived from the theme's greys.
    readonly property color iconOff: isDark ? "#5b5e64" : "#9a9da2"

    readonly property bool iconNormalCustom: root.validColor(iconNormalPref)
    readonly property bool iconHoverCustom: root.validColor(iconHoverPref)
    readonly property bool iconSelectedCustom: root.validColor(iconSelectedPref)
    readonly property bool iconDisabledCustom: root.validColor(iconDisabledPref)
    readonly property bool iconColoursCustom:
        iconNormalCustom || iconHoverCustom || iconSelectedCustom || iconDisabledCustom

    // The four defaults as one object, for the Preferences dialog's reset rows.
    readonly property var iconDefaults: ({
        normal: iconNormalDefault,
        hover: iconHoverDefault,
        selected: iconSelectedDefault,
        disabled: iconDisabledDefault
    })

    function validColor(value) {
        return /^#[0-9a-fA-F]{6}$/.test(value) || /^#[0-9a-fA-F]{8}$/.test(value)
    }

    function tint(custom, fallback) {
        return validColor(custom) ? custom : fallback
    }

    // Clears all four overrides, so the icons follow the active theme again.
    function resetIconColours() {
        iconNormalPref = ""
        iconHoverPref = ""
        iconSelectedPref = ""
        iconDisabledPref = ""
    }
}
