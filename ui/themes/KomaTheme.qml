pragma Singleton
import QtQuick

// KomaPaint design tokens — "ink and ember".
//
// KomaPaint's identity comes from a graphite desk, a single cool ink accent for
// structure and selection, and one warm ember accent reserved for the things an
// artist touches while painting (the active brush, the size ring, the colour
// instrument). Neither theme is a tint of the other: both are designed
// palettes, and the light theme uses warm paper greys rather than white.
//
// Principles this file encodes:
//  * Surfaces form a shallow ladder (desk → chrome → panel → field), so depth
//    is read from tone, not from borders or drop shadows.
//  * Separation is a hairline (one pixel at very low contrast) used only where
//    structure would otherwise be ambiguous. Panels do not each get a box.
//  * Type has a fixed scale, controls have fixed heights, and spacing is on a
//    4 px grid; nothing sizes itself ad hoc.
//  * Every state (hover, selected, focus, disabled) has a token, so components
//    cannot invent their own colours.
//
// Colour literals belong here. A component that needs a new shade should add a
// token instead of writing a literal, so both themes stay coherent.
QtObject {
    id: root

    // --- Theme selection ----------------------------------------------------
    property string mode: "dark"
    readonly property bool isDark: mode !== "light"
    readonly property var modeNames: ["dark", "light"]
    // Switched by the preference; components animate colours over this.
    readonly property int themeTransition: 160

    // --- Surface ladder -----------------------------------------------------
    // Dark: a grey graphite, not black. The desk sits lighter than the chrome
    // so the artwork floats above the interface instead of sinking into it.
    readonly property color windowBg: isDark ? "#2c2c2c" : "#eeeeee"    // outer frame
    readonly property color menubarBg: isDark ? "#2c2c2c" : "#eeeeee"   // application menu
    readonly property color toolbarBg: isDark ? "#2c2c2c" : "#eeeeee"   // command bar
    readonly property color paneBg: isDark ? "#383838" : "#f4f4f4"      // docked panel body
    readonly property color paneBgAlt: isDark ? "#333333" : "#eeeeee"   // rows, stripes, wells
    readonly property color paneHeaderBg: isDark ? "#2c2c2c" : "#eeeeee" // tab strip
    readonly property color panelInsetBg: isDark ? "#292929" : "#eeeeee" // grouped area inside a panel
    readonly property color canvasBg: isDark ? "#454545" : "#e8e8e8"    // desk around the artwork
    readonly property color canvasPaper: "#ffffff"
    readonly property color canvasEdge: isDark ? "#1b1d20" : "#8b9096"  // paper outline
    readonly property color previewPaper: isDark ? "#dde1e6" : "#ffffff"
    readonly property color inputBg: isDark ? "#292929" : "#ffffff"     // fields, value boxes
    readonly property color inputBgHover: isDark ? "#343434" : "#f7f7f7"
    readonly property color buttonBg: isDark ? "#404040" : "#e4e4e4"
    readonly property color buttonBgHover: isDark ? "#4a4a4a" : "#dadada"
    readonly property color buttonBgPress: isDark ? "#5a5a5a" : "#c9c9c9"
    readonly property color toolRailBg: isDark ? "#292929" : "#e4e4e4"  // left tool strip
    readonly property color statusbarBg: isDark ? "#2c2c2c" : "#eeeeee"
    readonly property color floatingBg: isDark ? "#383838" : "#f4f4f4"
    readonly property color toolActive: isDark ? "#505050" : "#d5d5d5"
    readonly property color rowSelected: isDark ? "#505050" : "#d5d5d5"

    // The canvas paints its own transparency checkerboard with these.
    readonly property color transparencyLight: isDark ? "#828282" : "#ffffff"
    readonly property color transparencyDark: isDark ? "#666666" : "#e8e8e8"

    // --- Structure ----------------------------------------------------------
    // `hairline` separates parts of the same surface; `border` outlines a
    // control or a floating window. `outline` is the strongest.
    readonly property color hairline: isDark ? "#5c5c5c" : "#cccccc"
    readonly property color border: isDark ? "#666666" : "#aaaaaa"
    readonly property color borderLight: isDark ? "#888888" : "#888888"
    readonly property color outline: isDark ? "#888888" : "#888888"
    readonly property color separator: hairline
    readonly property int hairlineWidth: 1
    readonly property int focusWidth: 2

    // --- Text ---------------------------------------------------------------
    readonly property color text: isDark ? "#ededed" : "#222222"
    readonly property color textMuted: isDark ? "#c6c6c6" : "#555555"
    readonly property color textSubtle: isDark ? "#909090" : "#808080"
    readonly property color textInverted: isDark ? "#101114" : "#ffffff"
    readonly property color textAccent: isDark ? "#ededed" : "#222222"
    readonly property color textEmber: isDark ? "#ededed" : "#222222"
    readonly property color textOnActive: isDark ? "#ffffff" : "#222222"
    readonly property color textOnActiveMuted: isDark ? "#ededed" : "#333333"
    // The dark theme's accent is light enough to fill a button, so text on it
    // has to be ink rather than white; both themes keep > 4.5:1 against accent.
    readonly property color textOnAccent: isDark ? "#222222" : "#ffffff"
    // Micro labels (section headings) sit a step below muted text.
    readonly property color textSectionLabel: isDark ? "#bdbdbd" : "#666666"

    // --- Accents ------------------------------------------------------------
    // Ink: structure, selection, focus. Ember: the painting instrument.
    readonly property color accent: isDark ? "#c8c8c8" : "#222222"
    readonly property color accentSoft: isDark ? "#888888" : "#bbbbbb"
    readonly property color accentWash: isDark ? "#505050" : "#d5d5d5"
    readonly property color ember: isDark ? "#c8c8c8" : "#555555"
    readonly property color emberSoft: isDark ? "#777777" : "#bbbbbb"
    readonly property color selection: isDark ? "#707070" : "#555555"
    readonly property color warning: isDark ? "#d2a054" : "#8a5c10"
    readonly property color error: isDark ? "#cf6f6f" : "#a33232"
    readonly property color success: isDark ? "#7fb27f" : "#2f7a3f"

    // --- Interaction states -------------------------------------------------
    // Overlays keep hover/selection consistent on any surface: a component
    // paints state as an overlay instead of guessing a new colour.
    readonly property color hoverOverlay: isDark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.05)
    readonly property color hoverOverlayStrong: isDark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color pressOverlay: isDark ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(0, 0, 0, 0.12)
    readonly property real disabledOpacity: 0.42
    readonly property real mutedOpacity: 0.66
    readonly property color focusRing: accent

    // --- Motion -------------------------------------------------------------
    readonly property int durationFast: 90    // hover, press
    readonly property int durationBase: 140   // expansion, fades
    readonly property int durationSlow: 220    // panel reveals
    readonly property int easingCurve: Easing.OutCubic

    // --- Typography ---------------------------------------------------------
    // One family per token. A face that is not installed falls back to the
    // platform's default through Qt's font substitution, so the interface keeps
    // its proportions on any system instead of falling back to a serif.
    readonly property string fontUi: "Inter"
    readonly property string fontDisplay: "Inter"
    readonly property string fontMono: "JetBrains Mono"

    readonly property int fontMicro: 9      // uppercase section labels, badges
    readonly property int fontXS: 10
    readonly property int fontS: 11
    readonly property int fontM: 12
    readonly property int fontL: 13
    readonly property int fontXL: 15
    readonly property int fontDisplay1: 18

    readonly property int weightRegular: Font.Normal
    readonly property int weightMedium: Font.Medium
    readonly property int weightBold: Font.DemiBold
    // Tracking for the micro caps used above sections and in the status bar.
    readonly property real trackingMicro: 0.9

    // --- Spacing (4 px grid) ------------------------------------------------
    readonly property int gapXS: 2
    readonly property int gapS: 4
    readonly property int gapM: 8
    readonly property int gapL: 12
    readonly property int gapXL: 16
    readonly property int gapXXL: 24
    // Gutter between a panel's edge and its content.
    readonly property int panelInset: 10

    // --- Shape --------------------------------------------------------------
    // Small radii on controls, one step more on popovers, and the artwork's
    // own corners. Nothing is a pill; this is a tool, not a toy.
    readonly property int radiusXS: 2
    readonly property int radiusS: 3
    readonly property int radiusM: 5
    readonly property int radiusL: 8
    readonly property int radiusPanel: 4

    // --- Sizing rails -------------------------------------------------------
    readonly property int menubarHeight: 30
    readonly property int commandBarHeight: 40
    // Kept as a name for the command bar's height (older components use it).
    readonly property int toolbarHeight: commandBarHeight
    readonly property int tabStripHeight: 32
    readonly property int statusbarHeight: 24
    readonly property int panelHeaderH: 30
    readonly property int controlHeight: 24
    readonly property int controlHeightSmall: 20
    readonly property int fieldWidth: 58
    readonly property int iconS: 14
    readonly property int iconM: 18
    readonly property int iconL: 22
    readonly property int toolButtonSize: 34
    readonly property int toolIconSize: 21
    readonly property int paletteGap: 2
    readonly property int paletteWidth: toolButtonSize + 8
    readonly property int leftDockWidth: 248
    readonly property int splitterHit: 7      // grab area around a dock divider
    readonly property int splitterVisual: 5   // how much of the divider is drawn

    // --- Elevation ----------------------------------------------------------
    // Only floating windows and the popovers cast anything; docked panels read
    // as part of the same surface.
    readonly property color shadow: Qt.rgba(0, 0, 0, isDark ? 0.42 : 0.20)
    readonly property color shadowSoft: Qt.rgba(0, 0, 0, isDark ? 0.24 : 0.12)
    readonly property int floatingHeaderHeight: 30

    // --- Icon tints ---------------------------------------------------------
    // The PNG icons under ui/assets/icons are single-colour artwork; only their
    // shape is used and the interface supplies the colour, so one file serves
    // every state. Full-colour artwork (the project-type illustrations) is
    // never tinted.
    property string iconNormalPref: ""
    property string iconHoverPref: ""
    property string iconSelectedPref: ""
    property string iconDisabledPref: ""

    readonly property color iconNormalDefault: isDark ? "#dedede" : "#555555"
    readonly property color iconHoverDefault: isDark ? "#f4f4f4" : "#333333"
    // In the light theme a selected icon is the accent itself, so the two can
    // never drift apart; the dark theme needs a lighter tint to stay readable
    // on its selected background.
    readonly property color iconSelectedDefault: isDark ? "#ededed" : root.accent
    readonly property color iconDisabledDefault: isDark ? "#858585" : "#999999"

    readonly property color iconNormal: root.tint(iconNormalPref, iconNormalDefault)
    readonly property color iconHover: root.tint(iconHoverPref, iconHoverDefault)
    readonly property color iconSelected: root.tint(iconSelectedPref, iconSelectedDefault)
    readonly property color iconDisabled: root.tint(iconDisabledPref, iconDisabledDefault)
    readonly property color iconOff: isDark ? "#a0a0a0" : "#888888"

    readonly property bool iconNormalCustom: root.validColor(iconNormalPref)
    readonly property bool iconHoverCustom: root.validColor(iconHoverPref)
    readonly property bool iconSelectedCustom: root.validColor(iconSelectedPref)
    readonly property bool iconDisabledCustom: root.validColor(iconDisabledPref)
    readonly property bool iconColoursCustom:
        iconNormalCustom || iconHoverCustom || iconSelectedCustom || iconDisabledCustom

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

    function resetIconColours() {
        iconNormalPref = ""
        iconHoverPref = ""
        iconSelectedPref = ""
        iconDisabledPref = ""
    }

    // The colour an icon takes in a given visual state, so components and the
    // vector glyphs agree without repeating the rule.
    function iconColour(state, dimmed) {
        if (state === "selected") return iconSelected
        if (state === "hover") return iconHover
        if (state === "disabled") return iconDisabled
        if (state === "off" || dimmed) return iconOff
        return iconNormal
    }

    // Micro caps label: uppercase, tracked, small. Used for section headings
    // and the status bar's group labels.
    function capsLabel(component) {
        component.font.capitalization = Font.AllUppercase
        component.font.letterSpacing = trackingMicro
    }
}
