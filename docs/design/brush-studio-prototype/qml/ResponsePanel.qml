import QtQuick
import QtQuick.Layouts
import StudioMock 1.0

ColumnLayout {
    id: root
    property bool advanced: false
    property bool selectedBrush: false
    property bool original: false
    property bool selectedAvailable: studio.entry.kind === "brush"
    property string inputName: "Pressure"
    property string outputName: "Size"
    readonly property var draft: selectedBrush && selectedAvailable ? studio.entry : studio.brush
    readonly property string curveName: inputName + ":" + outputName
    readonly property var custom: draft.curves ? draft.curves[curveName] : null
    readonly property var line: custom || [0,.25,.5,.75,1]
    function setCurve(points) { studio.editCurve(curveName, points, selectedBrush && selectedAvailable) }
    function write(key, value) { if (selectedBrush && selectedAvailable) studio.editEntry(key, value); else studio.edit(key, value) }
    spacing: 9
    Section { Layout.fillWidth: true; ruled: false; heading: "Response"; detail: "Change how the brush reacts while you draw." }
    Segments {
        visible: root.selectedAvailable; Layout.fillWidth: true
        choices: [{key:"whole", label:"Whole brush"},{key:"entry", label:"Added brush"}]
        current: root.selectedBrush ? "entry" : "whole"; onChosen: key => root.selectedBrush = key === "entry"
    }
    Rectangle {
        Layout.fillWidth: true; implicitHeight: 76; radius: 6; color: Desk.paper; clip: true
        ArtSheet { anchors.fill: parent; anchors.topMargin: 10; kind: root.original ? "original" : "response" }
        Tag { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8; text: "RETAINED EXAMPLE"; color: "#605348"; font.pixelSize: 8; font.letterSpacing: .8 }
    }
    Segments { Layout.fillWidth: true; choices: ["Modified", "Original"]; current: root.original ? "Original" : "Modified"; onChosen: key => root.original = key === "Original" }
    Type { text: "Same stroke and inputs in both views."; color: Desk.muted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Section { visible: root.advanced; Layout.fillWidth: true; heading: "Detailed curves"; detail: "Pick an input and what it changes, then drag the points." }
    RowLayout {
        visible: root.advanced; Layout.fillWidth: true; spacing: 6
        Selector { objectName: "curveInput"; Layout.fillWidth: true; Layout.minimumWidth: 0; current: root.inputName; options: ["Pressure","Tilt","Speed","Direction","Time"]; onChosen: key => root.inputName = key }
        Type { text: "→"; color: Desk.muted; font.pixelSize: 13 }
        Selector { objectName: "curveOutput"; Layout.fillWidth: true; Layout.minimumWidth: 0; current: root.outputName; options: ["Size","Opacity","Flow","Rotation","Scatter"]; onChosen: key => root.outputName = key }
    }
    CurveGraph { visible: root.advanced; Layout.fillWidth: true; values: root.line; axis: root.inputName; target: root.outputName; onEdited: points => root.setCurve(points) }
    Flow {
        visible: root.advanced; Layout.fillWidth: true; spacing: 5
        DeskButton { text: "Linear"; style: "fill"; size: 38; leftPadding: 8; rightPadding: 8; textSize: 11; onClicked: root.setCurve([0,.25,.5,.75,1]) }
        DeskButton { text: "Soft"; style: "fill"; size: 38; leftPadding: 8; rightPadding: 8; textSize: 11; onClicked: root.setCurve([0,.48,.70,.88,1]) }
        DeskButton { text: "Firm"; style: "fill"; size: 38; leftPadding: 8; rightPadding: 8; textSize: 11; onClicked: root.setCurve([0,.09,.24,.57,1]) }
        DeskButton { text: "Off"; style: "quiet"; size: 38; leftPadding: 8; rightPadding: 8; textSize: 11; enabled: !!root.custom; onClicked: root.setCurve([]) }
    }
    Type { visible: root.advanced; text: root.custom ? "Custom curve is on." : "No custom curve. The strengths below are active."; color: root.custom ? Desk.accent : Desk.muted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Section { Layout.fillWidth: true; heading: "Quick response"; detail: "Zero turns off that influence. Size and darkness are independent." }
    ParamList {
        Layout.fillWidth: true
        keyPrefix: root.selectedBrush ? "added_" : ""
        values: root.draft
        specs: [["pressureSize","Pressure → size",0,100,"%"],["pressureOpacity","Pressure → opacity",0,100,"%"],
                ["pressureFlow","Pressure → flow",0,100,"%"],["tiltAmount","Tilt → width",0,100,"%"],["speedAmount","Speed → thinning",0,100,"%"]]
        onEdited: (key,value) => root.write(key,value)
    }
}
