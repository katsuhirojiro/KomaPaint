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
    readonly property color windowBg: isDark ? "#313439" : "#e7e9ec"    // outer frame
    readonly property color menubarBg: isDark ? "#2c2f34" : "#dfe2e6"   // application menu
    readonly property color toolbarBg: isDark ? "#34373c" : "#eaecf0"   // command bar
    readonly property color paneBg: isDark ? "#3b3e44" : "#f5f6f8"      // docked panel body
    readonly property color paneBgAlt: isDark ? "#36393e" : "#eef0f3"   // rows, stripes, wells
    readonly property color paneHeaderBg: isDark ? "#32353a" : "#e8eaee" // tab strip
    readonly property color panelInsetBg: isDark ? "#31343a" : "#eceef2" // grouped area inside a panel
    readonly property color canvasBg: isDark ? "#4d5157" : "#ccd0d5"    // desk around the artwork
    readonly property color canvasPaper: "#ffffff"
    readonly property color canvasEdge: isDark ? "#1b1d20" : "#8b9096"  // paper outline
    readonly property color previewPaper: isDark ? "#dde1e6" : "#ffffff"
    readonly property color inputBg: isDark ? "#2f3237" : "#ffffff"     // fields, value boxes
    readonly property color inputBgHover: isDark ? "#353940" : "#f4f6f8"
    readonly property color buttonBg: isDark ? "#3f434a" : "#e2e5ea"
    readonly property color buttonBgHover: isDark ? "#484d55" : "#d8dcE2"
    readonly property color buttonBgPress: isDark ? "#545a63" : "#cbd0d7"
    readonly property color toolRailBg: isDark ? "#2f3237" : "#e2e5ea"  // left tool strip
    readonly property color statusbarBg: isDark ? "#2c2f34" : "#dfe2e6"
    readonly property color floatingBg: isDark ? "#3b3e44" : "#f7f8fa"
    readonly property color toolActive: isDark ? "#43628c" : "#c6d9ee"
    readonly property color rowSelected: isDark ? "#3d4754" : "#dbe7f4"

    // The canvas paints its own transparency checkerboard with these.
    readonly property color transparencyLight: isDark ? "#7e828a" : "#ffffff"
    readonly property color transparencyDark: isDark ? "#63676e" : "#e6e8eb"

    // --- Structure ----------------------------------------------------------
    // `hairline` separates parts of the same surface; `border` outlines a
    // control or a floating window. `outline` is the strongest.
    readonly property color hairline: isDark ? "#2a2d31" : "#dcdfe4"
    readonly property color border: isDark ? "#24272b" : "#c8ccd2"
    readonly property color borderLight: isDark ? "#4a4e55" : "#c2c7ce"
    readonly property color outline: isDark ? "#1c1e22" : "#a9aeb5"
    readonly property color separator: hairline
    readonly property int hairlineWidth: 1
    readonly property int focusWidth: 2

    // --- Text ---------------------------------------------------------------
    readonly property color text: isDark ? "#e2e4e8" : "#23262b"
    readonly property color textMuted: isDark ? "#a3a7ae" : "#565b63"
    readonly property color textSubtle: isDark ? "#7b8087" : "#83888f"
    readonly property color textInverted: isDark ? "#101114" : "#ffffff"
    readonly property color textAccent: isDark ? "#8fb2e0" : "#1d5e97"
    readonly property color textEmber: isDark ? "#e8b878" : "#8c5a1d"
    readonly property color textOnActive: isDark ? "#ffffff" : "#16334f"
    readonly property color textOnActiveMuted: isDark ? "#dbe4f0" : "#37587a"
    // The dark theme's accent is light enough to fill a button, so text on it
    // has to be ink rather than white; both themes keep > 4.5:1 against accent.
    readonly property color textOnAccent: isDark ? "#10161c" : "#ffffff"
    // Micro labels (section headings) sit a step below muted text.
    readonly property color textSectionLabel: isDark ? "#8b9098" : "#6d737b"

    // --- Accents ------------------------------------------------------------
    // Ink: structure, selection, focus. Ember: the painting instrument.
    readonly property color accent: isDark ? "#6f9bd4" : "#1d5e97"
    readonly property color accentSoft: isDark ? "#4a6a92" : "#a8c6e4"
    readonly property color accentWash: isDark ? "#2b3a4d" : "#dce9f7"
    readonly property color ember: isDark ? "#d99a5b" : "#b0701f"
    readonly property color emberSoft: isDark ? "#7d5c38" : "#e8c79a"
    readonly property color selection: isDark ? "#3f6ea8" : "#2f6ea8"
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
    readonly property int paletteWidth: 2 * toolButtonSize + paletteGap + 2 * 7
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

    readonly property color iconNormalDefault: isDark ? "#c2c5ca" : "#4a4e55"
    readonly property color iconHoverDefault: isDark ? "#eef0f2" : "#24272b"
    // In the light theme a selected icon is the accent itself, so the two can
    // never drift apart; the dark theme needs a lighter tint to stay readable
    // on its selected background.
    readonly property color iconSelectedDefault: isDark ? "#d7e6f8" : root.accent
    readonly property color iconDisabledDefault: isDark ? "#787d85" : "#a2a6ac"

    readonly property color iconNormal: root.tint(iconNormalPref, iconNormalDefault)
    readonly property color iconHover: root.tint(iconHoverPref, iconHoverDefault)
    readonly property color iconSelected: root.tint(iconSelectedPref, iconSelectedDefault)
    readonly property color iconDisabled: root.tint(iconDisabledPref, iconDisabledDefault)
    readonly property color iconOff: isDark ? "#666b72" : "#9ea2a8"

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
