import QtQuick
import QtQuick.Shapes
import "../themes"

// KomaPaint's interface glyphs, drawn as vectors so they stay crisp at every
// size and take their colour from the theme like the PNG tool icons do.
//
// One family, one weight: a 20-unit grid, round caps, a consistent 1.7-unit
// stroke, and no mixing of filled and outlined shapes within a group. Icons for
// *tools* and *layers* are the authored PNGs in ui/assets/icons (KpIcon); this
// file covers the interface chrome, which needs shapes that scale cleanly with
// the interface density and follow the icon-tint preferences.
//
// `name` picks the glyph; `size` is the box it is drawn in; `color` defaults to
// the theme's normal icon tint so plain use follows the theme and the user's
// overrides automatically.
Item {
    id: root

    property string name: ""
    property int size: KP.iconM
    property color color: KP.iconNormal
    // Filled glyphs (play, stop, bullets) ignore the stroke.
    property real weight: 1.7

    implicitWidth: size
    implicitHeight: size
    objectName: "kpGlyph_" + name

    readonly property var glyph: root.table[root.name] !== undefined ? root.table[root.name] : null
    readonly property bool filled: root.glyph !== null && root.glyph.fill === true
    readonly property string path: root.glyph !== null ? root.glyph.d : ""

    // Path data on a 20 × 20 grid. `fill: true` marks solid shapes.
    readonly property var table: ({
        "new":        { d: "M6 3.5 H11.8 L14.5 6.2 V11.5 M6 3.5 V16.5 H10.5 M11.8 3.5 V6.2 H14.5" },
        "open":       { d: "M3.2 15.8 V5.6 H8 L9.6 7.6 H16.8 V15.8 Z" },
        "save":       { d: "M4.4 12.4 V15.6 H15.6 V12.4 M10 4.2 V11.4 M6.9 8.3 L10 11.4 L13.1 8.3" },
        "undo":       { d: "M7.2 7.6 H12.4 A3.4 3.4 0 0 1 12.4 14.4 H8.4 M9.8 4.6 L6.8 7.6 L9.8 10.6" },
        "redo":       { d: "M12.8 7.6 H7.6 A3.4 3.4 0 0 0 7.6 14.4 H11.6 M10.2 4.6 L13.2 7.6 L10.2 10.6" },
        "zoom-in": { d: "M4 8.6 A4.6 4.6 0 1 1 13.2 8.6 A4.6 4.6 0 1 1 4 8.6 M12 12 L16.2 16.2 M6.7 8.6 H10.5 M8.6 6.7 V10.5" },
        "zoom-out": { d: "M4 8.6 A4.6 4.6 0 1 1 13.2 8.6 A4.6 4.6 0 1 1 4 8.6 M12 12 L16.2 16.2 M6.7 8.6 H10.5" },
        "zoom-fit":   { d: "M4 7.6 V4 H7.6 M12.4 4 H16 V7.6 M16 12.4 V16 H12.4 M7.6 16 H4 V12.4" },
        "actual": { d: "M5.6 5.6 H14.4 V14.4 H5.6 Z M8.9 10 A1.1 1.1 0 1 1 11.1 10 A1.1 1.1 0 1 1 8.9 10" },
        "grid":       { d: "M4 4 H8.4 V8.4 H4 Z M11.6 4 H16 V8.4 H11.6 Z M4 11.6 H8.4 V16 H4 Z M11.6 11.6 H16 V16 H11.6 Z" },
        "settings": { d: "M7.3 10 A2.7 2.7 0 1 1 12.7 10 A2.7 2.7 0 1 1 7.3 10 M10 2.6 V4.6 M10 15.4 V17.4 M2.6 10 H4.6 M15.4 10 H17.4 M4.8 4.8 L6.2 6.2 M13.8 13.8 L15.2 15.2 M15.2 4.8 L13.8 6.2 M6.2 13.8 L4.8 15.2" },
        "chevron-down":  { d: "M6.2 8.2 L10 12 L13.8 8.2" },
        "chevron-up":    { d: "M6.2 11.8 L10 8 L13.8 11.8" },
        "chevron-left":  { d: "M11.8 6.2 L8 10 L11.8 13.8" },
        "chevron-right": { d: "M8.2 6.2 L12 10 L8.2 13.8" },
        "close":      { d: "M6.2 6.2 L13.8 13.8 M13.8 6.2 L6.2 13.8" },
        "plus":       { d: "M10 5 V15 M5 10 H15" },
        "minus":      { d: "M5 10 H15" },
        "dots-h": { d: "M4.6 10 A1.3 1.3 0 1 1 7.2 10 A1.3 1.3 0 1 1 4.6 10 M8.7 10 A1.3 1.3 0 1 1 11.3 10 A1.3 1.3 0 1 1 8.7 10 M12.8 10 A1.3 1.3 0 1 1 15.4 10 A1.3 1.3 0 1 1 12.8 10", fill: true },
        "dots-v": { d: "M10 4.6 A1.3 1.3 0 1 1 10 7.2 A1.3 1.3 0 1 1 10 4.6 M10 8.7 A1.3 1.3 0 1 1 10 11.3 A1.3 1.3 0 1 1 10 8.7 M10 12.8 A1.3 1.3 0 1 1 10 15.4 A1.3 1.3 0 1 1 10 12.8", fill: true },
        "grip":       { d: "M6.5 8 H13.5 M6.5 12 H13.5" },
        "swap":       { d: "M4.6 7.6 H14.6 M12.2 5.2 L14.6 7.6 L12.2 10 M15.4 12.4 H5.4 M7.8 10 L5.4 12.4 L7.8 14.8" },
        "check":      { d: "M5.2 10.4 L8.6 13.8 L14.8 6.2" },
        "play":       { d: "M7.2 4.6 L15.2 10 L7.2 15.4 Z", fill: true },
        "pause":      { d: "M7.4 5 V15 M12.6 5 V15" },
        "stop":       { d: "M6.6 6.6 H13.4 V13.4 H6.6 Z", fill: true },
        "skip-start": { d: "M6.4 5 V15 M14.6 5.4 L8.8 10 L14.6 14.6 Z" },
        "skip-end": { d: "M13.6 5 V15 M5.4 5.4 L11.2 10 L5.4 14.6 Z" },
        "eye": { d: "M2.8 10 C5.4 5.8 14.6 5.8 17.2 10 C14.6 14.2 5.4 14.2 2.8 10 Z M8.2 10 A1.8 1.8 0 1 1 11.8 10 A1.8 1.8 0 1 1 8.2 10" },
        "eye-off":    { d: "M2.8 10 C5.4 5.8 14.6 5.8 17.2 10 C14.6 14.2 5.4 14.2 2.8 10 Z M4.4 16.2 L15.6 3.8" },
        "lock":       { d: "M6.6 9.2 V6.9 A3.4 3.4 0 0 1 13.4 6.9 V9.2 M5.3 9.2 H14.7 V16 H5.3 Z" },
        "unlock":     { d: "M6.6 9.2 V6.9 A3.4 3.4 0 0 1 13.4 6.9 M5.3 9.2 H14.7 V16 H5.3 Z" },
        "trash":      { d: "M5.4 6.4 H14.6 M8 6.4 V4.4 H12 V6.4 M6.9 6.4 L7.7 16 H12.3 L13.1 6.4" },
        "duplicate":  { d: "M7.2 4.4 H15 V12.2 M5 6.8 H12.4 V16 H5 Z" },
        "folder":     { d: "M3.2 15.8 V5.6 H8 L9.6 7.6 H16.8 V15.8 Z M3.2 8.6 H16.8" },
        "layer-up":   { d: "M10 15.4 V4.6 M6.6 8 L10 4.6 L13.4 8" },
        "layer-down": { d: "M10 4.6 V15.4 M6.6 12 L10 15.4 L13.4 12" },
        "merge":      { d: "M10 3.6 V11.2 M6.8 8 L10 11.2 L13.2 8 M4.6 16.2 H15.4" },
        "brush":      { d: "M5.2 14.8 C7.2 15.9 9 14.4 10.1 12.8 L15.2 5.6 C15.6 4.6 14.6 4.2 13.9 5 L6.6 10.9 C5.4 11.8 4.4 13.4 5.2 14.8 Z" },
        "opacity":    { d: "M10 3.4 C12.6 6.9 14.6 9.2 14.6 11.5 A4.6 4.6 0 0 1 5.4 11.5 C5.4 9.2 7.4 6.9 10 3.4 Z" },
        "size": { d: "M4.6 10 A5.4 5.4 0 1 1 15.4 10 A5.4 5.4 0 1 1 4.6 10" },
        "reset":      { d: "M15.2 10 A5.2 5.2 0 1 1 13.5 6.1 M15.6 3.8 V7.6 H11.8" },
        "palette":    { d: "M10 3.6 A6.4 6.4 0 1 0 10 16.4 C11.2 16.4 11.6 15.4 11 14.4 C10.4 13.2 11.2 12.4 12.4 12.4 H14 A2.4 2.4 0 0 0 16.4 10 A6.4 6.4 0 0 0 10 3.6 Z" },
        "history": { d: "M3.8 10 A6.2 6.2 0 1 1 16.2 10 A6.2 6.2 0 0 1 10 16.2 M10 6.6 V10.2 L12.4 11.8" },
        "window":     { d: "M4.4 6.4 H15.6 V15.6 H4.4 Z M4.4 9.4 H15.6" },
        "info": { d: "M3.8 10 A6.2 6.2 0 1 1 16.2 10 A6.2 6.2 0 1 1 3.8 10 M10 9.2 V14 M10 6.5 V7.1" },
        "warning":    { d: "M10 4 L16.4 15.6 H3.6 Z M10 8.4 V11.6 M10 13.4 V13.9" }
    })

    Shape {
        anchors.fill: parent
        visible: root.path.length > 0
        preferredRendererType: Shape.CurveRenderer
        transform: Scale {
            origin.x: 0
            origin.y: 0
            xScale: root.size / 20
            yScale: root.size / 20
        }
        ShapePath {
            strokeColor: root.filled ? "transparent" : root.color
            strokeWidth: root.filled ? 0 : root.weight
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.path }
        }
    }
}
