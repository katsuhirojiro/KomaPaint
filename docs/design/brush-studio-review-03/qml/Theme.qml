pragma Singleton
import QtQuick

QtObject {
    readonly property color canvas: "#121214"
    readonly property color panel: "#1A1A1D"
    readonly property color elevated: "#242428"
    readonly property color raised: "#2D2D32"
    readonly property color hover: "#26262A"
    readonly property color pressed: "#303035"
    readonly property color text: "#EBEBEF"
    readonly property color secondary: "#8E8E93"
    readonly property color meta: "#5A5A60"
    readonly property color disabled: "#45454A"
    readonly property color hairline: "#0DFFFFFF"
    readonly property color innerHighlight: "#0FFFFFFF"
    readonly property color track: "#101012"
    readonly property color fill: "#A0A0A8"
    readonly property color thumb: "#FFFFFF"
    readonly property color toggleOff: "#141416"
    readonly property color toggleOn: "#E5E5EA"
    readonly property color toggleKnob: "#232326"
    readonly property color paper: "#E9E6DE"
    readonly property color ink: "#2B2927"
    readonly property color ghost: "#8C8880"

    readonly property int railWidth: 180
    readonly property int railCompact: 64
    readonly property int inspectorWidth: 320
    readonly property int headerHeight: 52
    readonly property int statusHeight: 24
    readonly property real radius: 6
    readonly property real radiusSmall: 3

    readonly property string ui: "Inter"
    readonly property int micro: 9
    readonly property int small: 10
    readonly property int body: 12
    readonly property int label: 13
    readonly property int title: 18
    readonly property int display: 22
}
