import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import StudioMock 1.0

Popup {
    id: root
    property string mode: "mark"
    property string category: "All"
    property string picked: ""
    property string query: ""
    property string pickedName: ""
    signal accepted(string key)
    signal drawRequested()
    signal importRequested(string mode)
    readonly property bool brush: mode === "brush" || mode === "duplicate"
    readonly property bool texture: mode === "texture" || mode === "replaceTexture"
    readonly property bool groups: mode === "group"
    readonly property bool mark: !brush && !texture && !groups
    readonly property string title: mode === "duplicate" ? "Duplicate a Brush" : mode === "brush" ? "Add Another Brush"
        : groups ? "Group of Marks" : mode === "mask" ? "Add a Mask" : texture ? (mode === "texture" ? "Add Texture" : "Choose texture")
        : mode === "replaceMark" ? "Choose a mark" : "Add a Mark"
    readonly property var available: brush ? studio.brushes : texture ? studio.textures : groups ? studio.savedGroups : studio.marks
    readonly property var matched: available.filter(row => (category === "All" || row.sub === category) &&
        (row.title + " " + row.sub).toLowerCase().includes(query.toLowerCase().trim()))
    function show(next) {
        mode = next; category = "All"; query = ""; search.text = ""; picked = ""; pickedName = ""; open()
    }
    function use(key) { close(); accepted(key) }
    objectName: "assetPicker"
    anchors.centerIn: Overlay.overlay
    width: Math.min(725, Overlay.overlay ? Overlay.overlay.width - 26 : 725)
    height: Math.min(groups ? 490 : 646, Overlay.overlay ? Overlay.overlay.height - 24 : 646)
    modal: true; focus: true; padding: 19
    background: Rectangle { radius: 13; color: Desk.panel; border.color: Desk.line }
    Overlay.modal: Rectangle { color: "#a0111110" }
    contentItem: ColumnLayout {
        spacing: 11
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Tag { text: root.brush ? "BRUSH SHELF" : root.groups ? "REUSABLE GROUPS" : "MATERIAL SHELF"; color: Desk.accent }
                Type { text: root.title; font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            DeskButton { iconName: "close"; accessible: "Close picker"; style: "quiet"; onClicked: root.close() }
        }
        Type {
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: root.brush ? "Choose a brush by its stroke. The source stays as it is."
                : root.texture ? "Texture breaks up paint without changing the shape of a mark."
                : root.groups ? "Make a new group here, or add a copy of one you kept."
                : root.mode === "mask" ? "Use a shape to cut into this part of the brush."
                : "Choose by the imprint, not the filename."
            color: Desk.muted; font.pixelSize: 13
        }
        RowLayout {
            Layout.fillWidth: true; visible: !root.groups
            Field { id: search; Layout.fillWidth: true; placeholderText: "Search the shelf"; onTextEdited: root.query = text; Accessible.name: "Search the shelf" }
        }
        Segments {
            visible: root.mark && root.width >= 600
            Layout.fillWidth: true; choices: ["All", "Drawing", "Organic", "Effects", "My marks", "Imported"]
            current: root.category; onChosen: key => { root.category = key; root.picked = "" }
        }
        ScrollView {
            id: gridScroll
            Layout.fillHeight: true; Layout.fillWidth: true; clip: true
            contentWidth: availableWidth; contentHeight: items.implicitHeight + 2
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: DeskScrollBar {}
            GridLayout {
                id: items
                width: gridScroll.availableWidth - 11
                columns: root.width < 510 ? 2 : root.mark ? 4 : 3
                columnSpacing: 8; rowSpacing: 8
                Repeater {
                    model: root.matched
                    delegate: Button {
                        id: tile
                        required property var modelData
                        Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: 0
                        implicitHeight: root.groups ? 75 : 118
                        Accessible.name: modelData.title
                        objectName: modelData.key + "Tile"
                        padding: 6; hoverEnabled: true
                        background: Rectangle { radius: 6; color: root.picked === tile.modelData.key ? Desk.active : tile.hovered ? Desk.hover : Desk.raised; border.color: tile.visualFocus || root.picked === tile.modelData.key ? Desk.accent : Desk.softLine }
                        contentItem: ColumnLayout {
                            spacing: 4
                            Loader {
                                Layout.fillWidth: true; Layout.fillHeight: true
                                sourceComponent: root.brush ? strokeTile : root.groups ? groupTile : markTile
                                Component { id: strokeTile; Rectangle { radius: 5; clip: true; color: Desk.paper; ArtSheet { anchors.fill: parent; kind: "sample"; sample: tile.modelData.key } } }
                                Component { id: groupTile; Item { Glyph { anchors.centerIn: parent; name: "group"; tint: Desk.accent } } }
                                Component { id: markTile; Item { Well { anchors.centerIn: parent; width: root.texture ? parent.width : Math.min(parent.height, parent.width); height: parent.height; asset: tile.modelData.key; kind: root.texture ? "texture" : "mark" } } }
                            }
                            Type { text: tile.modelData.title; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.weight: Font.DemiBold; font.pixelSize: 12 }
                            Type { text: tile.modelData.sub; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.pixelSize: 10; color: Desk.muted }
                        }
                        onClicked: { root.picked = modelData.key; root.pickedName = modelData.title }
                        onDoubleClicked: root.use(modelData.key)
                    }
                }
                Type { visible: !root.matched.length; text: root.groups ? "No kept groups yet." : "No matches. Try another search."; color: Desk.muted; Layout.columnSpan: items.columns; Layout.topMargin: 12; Layout.leftMargin: 8 }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Desk.line }
        Flow {
            Layout.fillWidth: true; spacing: 7
            DeskButton { iconName: "draw"; text: root.width < 485 ? "Draw" : "Draw a tip"; visible: root.mode === "mark"; onClicked: { root.close(); root.drawRequested() } }
            DeskButton { iconName: "import"; text: root.width < 485 ? "Import" : "Import image"; visible: !root.brush && !root.groups && root.mode !== "mask"; onClicked: { root.close(); root.importRequested(root.mode) } }
            DeskButton { objectName: "newGroup"; iconName: "plus"; text: "New group"; visible: root.groups; onClicked: root.use("") }
            DeskButton { objectName: "acceptAsset"; text: root.groups ? "Add kept group" : root.mode === "duplicate" ? "Duplicate brush" : root.mode.startsWith("replace") ? "Use selected" : "Add selected"; style: "accent"; enabled: !!root.picked; onClicked: root.use(root.picked) }
        }
    }
}
