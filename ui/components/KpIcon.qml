import QtQuick
import QtQuick.Controls.impl
import "../themes"

// One icon from ui/assets/icons.
//
// Most of the PNGs are single-colour artwork on a transparent canvas: only
// their shape is used, and the interface supplies the colour, which is how one
// file serves the normal, hover, selected, disabled and off states. Those are
// drawn through IconImage, which keeps the alpha channel (transparency and the
// antialiased edges survive) and multiplies it by `color`.
//
// Artwork that is already full-colour — the project-type illustrations — is
// never tinted: set `tinted: false` and it is drawn exactly as authored.
//
// `name` is "<category>/<file>" without the extension, e.g. "tools/brush".
// The artwork is already centred and scaled inside its canvas, so the icon is
// simply shown at `size` × `size`; no per-icon offsets are applied here.
Item {
    id: root
    property string name: ""
    property int size: 20
    property color color: KP.iconNormal
    // false: draw the PNG in its own colours, exactly as authored.
    property bool tinted: true

    readonly property url imageSource: root.name.length > 0 ? "qrc:/icons/" + root.name + ".png" : ""
    // Tracking name for the UI tests, e.g. "tool_brushIcon" for the icon of
    // the tool palette button called "tool_brush".
    readonly property string tag: root.name.length > 0 ? root.name : "none"

    implicitWidth: size
    implicitHeight: size
    objectName: root.tinted ? "kpIcon_" + root.tag : "kpArt_" + root.tag

    IconImage {
        anchors.fill: parent
        visible: root.tinted
        source: root.imageSource
        sourceSize: Qt.size(Math.max(1, root.size), Math.max(1, root.size))
        color: root.color
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        // The colour is only meaningful while tinting; when the artwork keeps
        // its own colours this instance is hidden and never drawn.
    }

    Image {
        anchors.fill: parent
        visible: !root.tinted
        source: root.imageSource
        sourceSize: Qt.size(Math.max(1, root.size), Math.max(1, root.size))
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }
}
