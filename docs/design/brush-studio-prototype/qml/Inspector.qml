import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

Rectangle {
    id: settings
    property bool inTabs: false
    property string section: "Brush"
    property bool advanced: false
    property bool addedResponse: false
    property bool peekOriginal: false
    property string signalIn: "Pressure"
    property string signalOut: "Size"
    readonly property string kind: studio.entry.kind || ""
    readonly property string itemLabel: kind === "texture" ? "Texture" : kind === "group" ? "Group" : kind === "brush" ? (width < 320 ? "Added" : "Added brush") : kind === "mask" ? "Mask" : "Mark"
    signal sectionChangedByUser(string next)
    signal openPicker(string mode)
    signal openPersonal()
    signal renameEntry()
    function fresh() { addedResponse = false; peekOriginal = false; signalIn = "Pressure"; signalOut = "Size"; scroller.contentItem.contentY = 0 }
    onSectionChanged: if (scroller.contentItem) scroller.contentItem.contentY = 0
    Connections { target: studio; function onSelectionChanged() { if (settings.section === "Selected") scroller.contentItem.contentY = 0 } }
    color: Desk.panel; radius: 9; border.color: Desk.line; clip: true

    ColumnLayout {
        anchors.fill: parent; anchors.margins: settings.inTabs ? 9 : 12
        spacing: settings.inTabs ? 6 : 10
        RowLayout {
            visible: !settings.inTabs
            Layout.fillWidth: true; Layout.leftMargin: 3; Layout.rightMargin: 2; Layout.topMargin: 2
            Type { text: "Settings"; font.pixelSize: 17; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Tag { text: settings.advanced ? "ADVANCED" : "BASIC"; color: settings.advanced ? Desk.accent : Desk.muted; font.pixelSize: 9; font.letterSpacing: .8 }
        }
        Segments {
            Layout.fillWidth: true
            choices: [{key:"Brush",label:"Brush"},{key:"Selected",label:settings.itemLabel},{key:"Dynamics",label:"Dynamics"}]
            current: settings.section
            onChosen: key => settings.sectionChangedByUser(key)
        }
        ScrollView {
            id: scroller
            objectName: "settingsScroll"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 48
            contentWidth: availableWidth; contentHeight: inside.implicitHeight + 8; clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: DeskScrollBar {}
            ColumnLayout {
                id: inside
                width: scroller.availableWidth - 13; x: 2; spacing: 12
                Loader {
                    Layout.fillWidth: true
                    sourceComponent: settings.section === "Brush" ? brushPage : settings.section === "Dynamics" ? dynamicsPage
                        : settings.kind === "mark" ? markPage : settings.kind === "texture" || settings.kind === "mask" ? texturePage
                        : settings.kind === "brush" || settings.kind === "group" ? groupPage : emptyPage
                }
            }
        }
    }

    readonly property var brushInk: [
        ["size","Brush size",10,250,"%"], ["opacity","Opacity",0,100,"%","Lighter or darker, without changing the width."],
        ["flow","Flow",0,100,"%","How much paint each mark lays down."]
    ]
    readonly property var placement: [["spacing","Spacing",4,140,"%","Distance between marks along the stroke."],
        ["scatter","Scatter",0,160,"%","How far marks wander across the stroke."]]
    readonly property var variation: [["sizeJitter","Size variation",0,100,"%"], ["opacityJitter","Opacity variation",0,100,"%"], ["angleJitter","Rotation variation",0,180,"°"]]
    readonly property var paint: [["wetness","Dilution",0,100,"%"],["edge","Soft halo",0,100,"%"],
        ["buildup","Build-up",0,100,"%"],["colorDrift","Color shift",0,100,"%"]]
    readonly property var blends: ["Normal","Multiply","Screen","Soft Light"]

    Component {
        id: brushPage
        ColumnLayout {
            spacing: 13
            Rectangle {
                visible: settings.advanced; Layout.fillWidth: true; implicitHeight: 40; color: Desk.active; radius: 6
                Type { anchors.fill: parent; anchors.margins: 8; font.pixelSize: 11; color: Desk.accent; wrapMode: Text.WordWrap; text: "More placement and paint controls below. Curves are in Dynamics." }
            }
            MarkOrder { visible: studio.markCount > 1; Layout.fillWidth: true; mode: studio.brush.order; picks: studio.picks; onEdited: key => studio.edit("order",key) }
            Section { Layout.fillWidth: true; ruled: studio.markCount > 1; heading: "Size & ink"; detail: "Adjust the whole brush, not the pad tools." }
            ParamList { Layout.fillWidth: true; values: studio.brush; specs: settings.brushInk; onEdited: (key,value) => studio.edit(key,value) }
            Section { Layout.fillWidth: true; heading: "Mark placement" }
            ParamList { Layout.fillWidth: true; values: studio.brush; specs: settings.placement; onEdited: (key,value) => studio.edit(key,value) }
            MarkOrder { visible: studio.markCount <= 1; Layout.fillWidth: true; mode: studio.brush.order; picks: studio.picks; onEdited: key => studio.edit("order",key) }
            Section { visible: settings.advanced; Layout.fillWidth: true; heading: "More ways to shape the stroke" }
            Disclosure {
                visible: settings.advanced; Layout.fillWidth: true; title: "Placement & variation"; description: "Offset, rotation and variation"
                inner: Component {
                    ColumnLayout {
                        spacing: 8
                        ParamList { Layout.fillWidth: true; values: studio.brush; specs: [["width","Scatter width",10,200,"%"],["offset","Side offset",-100,100,"%"], ...settings.variation]; onEdited: (key,value) => studio.edit(key,value) }
                        Toggle { Layout.fillWidth: true; text: "Turn marks along the stroke"; checked: studio.brush.follow; onToggled: studio.edit("follow",checked) }
                    }
                }
            }
            Disclosure {
                visible: settings.advanced; Layout.fillWidth: true; title: "Paint behavior"; description: "Dilution, edges, build-up and blend"; iconName: "brush"
                inner: Component {
                    ColumnLayout {
                        spacing: 8
                        ParamList { Layout.fillWidth: true; values: studio.brush; specs: [...settings.paint,["taper","Taper at the ends",0,100,"%"]]; onEdited: (key,value) => studio.edit(key,value) }
                        Section { Layout.fillWidth: true; heading: "Brush blending"; detail: "How test paint mixes with the pad or earlier paint." }
                        Selector { Layout.fillWidth: true; current: studio.brush.blend; options: settings.blends; onChosen: key => studio.edit("blend",key) }
                    }
                }
            }
            DeskButton { visible: settings.advanced; Layout.fillWidth: true; iconName: "mask"; text: "Add a Mask"; onClicked: settings.openPicker("mask") }
            DeskButton { visible: settings.advanced; Layout.fillWidth: true; iconName: "pin"; text: "Pin to My Controls"; style: "quiet"; onClicked: settings.openPersonal() }
        }
    }
    Component {
        id: markPage
        ColumnLayout {
            spacing: 11
            RowLayout {
                Layout.fillWidth: true
                Type { Layout.fillWidth: true; text: studio.entry.name || "Mark"; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                DeskButton { iconName: "draw"; accessible: "Rename mark"; style: "quiet"; size: 37; onClicked: settings.renameEntry() }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 100; color: Desk.paper; radius: 6; clip: true
                ArtSheet { anchors.fill: parent; anchors.leftMargin: 27; kind: "imprint" }
                Tag { anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 9; text: "SINGLE IMPRINT"; color: "#605348"; font.pixelSize: 8; font.letterSpacing: .8 }
            }
            DeskButton { objectName: "chooseMark"; Layout.fillWidth: true; text: "Choose a different mark"; rightIcon: "right"; onClicked: settings.openPicker("replaceMark") }
            Section { Layout.fillWidth: true; heading: "This mark only" }
            ParamList {
                Layout.fillWidth: true; keyPrefix: "mark_"; values: studio.entry
                specs: [["size","Mark size",10,250,"%"],["angle","Rotation",-180,180,"°"],["opacity","Mark opacity",0,100,"%"]]
                onEdited: (key,value) => studio.editEntry(key,value)
            }
            ParamList {
                visible: studio.entry.parentOrder === "Weighted" || settings.advanced
                Layout.fillWidth: true; keyPrefix: "mark_"; values: studio.entry
                specs: [["weight","Frequency",1,12,"","Used by Weighted at this mark’s level."]]
                onEdited: (key,value) => studio.editEntry(key,value)
            }
            Toggle { Layout.fillWidth: true; text: "Turn along the stroke"; checked: !!studio.entry.follow; onToggled: studio.editEntry("follow",checked) }
            Toggle { Layout.fillWidth: true; text: "Flip left to right"; checked: !!studio.entry.flipX; onToggled: studio.editEntry("flipX",checked) }
            Toggle { Layout.fillWidth: true; text: "Flip top to bottom"; checked: !!studio.entry.flipY; onToggled: studio.editEntry("flipY",checked) }
            Disclosure {
                visible: settings.advanced; Layout.fillWidth: true; title: "Variation for this mark"; description: "Use a range of sizes, angles and opacity"
                inner: Component { ParamList { values: studio.entry; keyPrefix: "mark_"; specs: settings.variation; onEdited: (key,value) => studio.editEntry(key,value) } }
            }
            Disclosure {
                visible: settings.advanced; Layout.fillWidth: true; title: "Placement for this mark"; description: "Fine-tune spacing and scatter"
                inner: Component { ParamList { values: studio.entry; keyPrefix: "mark_"; specs: [["spacing","Spacing after this mark",4,140,"%"],["scatter","Extra scatter",0,160,"%"]]; onEdited: (key,value) => studio.editEntry(key,value) } }
            }
        }
    }
    Component {
        id: texturePage
        ColumnLayout {
            spacing: 11
            RowLayout {
                Layout.fillWidth: true
                Type { Layout.fillWidth: true; text: studio.entry.name || "Texture"; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                DeskButton { iconName: "draw"; accessible: "Rename this entry"; style: "quiet"; size: 37; onClicked: settings.renameEntry() }
            }
            Well { Layout.fillWidth: true; Layout.preferredHeight: 84; asset: studio.entry.asset || ""; kind: settings.kind }
            DeskButton { visible: settings.kind === "texture"; objectName: "chooseTexture"; Layout.fillWidth: true; text: "Choose texture"; rightIcon: "right"; onClicked: settings.openPicker("replaceTexture") }
            Section { Layout.fillWidth: true; heading: settings.kind === "mask" ? "Shape the cutout" : "Texture on this brush"; ruled: false }
            ParamList {
                Layout.fillWidth: true; keyPrefix: "texture_"; values: studio.entry
                specs: [["strength","Strength",0,100,"%"],["scale","Scale",15,300,"%"],["angle","Rotation",-180,180,"°"]]
                onEdited: (key,value) => studio.editEntry(key,value)
            }
            Section { Layout.fillWidth: true; heading: "Where the pattern stays" }
            Segments {
                Layout.fillWidth: true; choices: [{key:"Canvas",label:"Canvas-fixed"},{key:"Stroke",label:"Moves with stroke"}]
                current: studio.entry.anchor || "Canvas"; onChosen: key => studio.editEntry("anchor",key)
            }
            Type { Layout.fillWidth: true; text: studio.entry.anchor === "Canvas" ? "The pattern stays put under every stroke." : "The pattern travels with each new stroke."; wrapMode: Text.WordWrap; color: Desk.muted; font.pixelSize: 12 }
            Section { visible: settings.kind === "texture"; Layout.fillWidth: true; heading: "How it’s applied" }
            Segments {
                visible: settings.kind === "texture"; Layout.fillWidth: true; choices: ["Whole stroke", "Per mark"]
                current: studio.entry.textureMode || "Whole stroke"; onChosen: key => studio.editEntry("textureMode",key)
            }
            Type {
                visible: settings.kind === "texture"; Layout.fillWidth: true
                text: studio.entry.textureMode === "Per mark" ? "Texture each mark before the marks overlap." : "Texture the stroke after the marks overlap."
                wrapMode: Text.WordWrap; color: Desk.muted; font.pixelSize: 12
            }
            Toggle { Layout.fillWidth: true; text: settings.kind === "mask" ? "Reverse cutout" : "Reverse light and dark"; checked: !!studio.entry.invert; onToggled: studio.editEntry("invert",checked) }
        }
    }
    Component {
        id: groupPage
        ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Glyph { name: settings.kind === "brush" ? "brush" : "group"; tint: Desk.accent }
                Type { text: studio.entry.name || settings.itemLabel; Layout.fillWidth: true; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                DeskButton { iconName: "draw"; accessible: "Rename " + settings.itemLabel; style: "quiet"; size: 37; onClicked: settings.renameEntry() }
            }
            Type {
                Layout.fillWidth: true; color: Desk.muted; font.pixelSize: 12; wrapMode: Text.WordWrap
                text: settings.kind === "brush" ? "This copy paints alongside the rest of the brush. Expand it in Brush Marks to change its own marks and textures."
                    : "Expand this group to work on its contents. Add new marks or other groups while it’s selected."
            }
            ParamList {
                Layout.fillWidth: true; keyPrefix: "part_"; values: studio.entry
                specs: [["contribution","Contribution",0,100,"%"],["size","Relative size",10,250,"%"],["opacity","Opacity",0,100,"%"]]
                onEdited: (key,value) => studio.editEntry(key,value)
            }
            Section { Layout.fillWidth: true; heading: "Blend with the other marks" }
            Selector { Layout.fillWidth: true; current: studio.entry.blend || "Normal"; options: [...settings.blends,"Subtract"]; onChosen: key => studio.editEntry("blend",key) }
            MarkOrder { Layout.fillWidth: true; mode: studio.entry.order || "In Order"; picks: studio.entryPicks; onEdited: key => studio.editEntry("order",key) }
            Section { Layout.fillWidth: true; heading: "Placement for this part" }
            ParamList { Layout.fillWidth: true; keyPrefix: "part_"; values: studio.entry; specs: settings.placement; onEdited: (key,value) => studio.editEntry(key,value) }
            DeskButton { visible: settings.kind === "brush"; Layout.fillWidth: true; text: "Edit this brush’s dynamics"; rightIcon: "right"; onClicked: { settings.addedResponse = true; settings.sectionChangedByUser("Dynamics") } }
            Disclosure {
                visible: settings.advanced && settings.kind === "brush"; Layout.fillWidth: true; title: "Paint for this brush"; description: "Change how this copy lays down paint"; iconName: "brush"
                inner: Component { ParamList { values: studio.entry; keyPrefix: "part_"; specs: [["flow","Flow",0,100,"%"],...settings.paint]; onEdited: (key,value) => studio.editEntry(key,value) } }
            }
            DeskButton { Layout.fillWidth: true; text: "Keep group for reuse"; iconName: "copy"; onClicked: studio.act(studio.selected,"reuse") }
        }
    }
    Component {
        id: dynamicsPage
        ResponsePanel {
            advanced: settings.advanced
            selectedBrush: settings.addedResponse; original: settings.peekOriginal
            inputName: settings.signalIn; outputName: settings.signalOut
            onSelectedBrushChanged: settings.addedResponse = selectedBrush
            onOriginalChanged: settings.peekOriginal = original
            onInputNameChanged: settings.signalIn = inputName
            onOutputNameChanged: settings.signalOut = outputName
        }
    }
    Component {
        id: emptyPage
        Section { heading: "Choose an entry"; detail: "Select a mark or texture in Brush Marks to edit it on its own."; ruled: false }
    }
}
