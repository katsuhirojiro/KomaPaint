import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    contentWidth: availableWidth; contentHeight: rack.implicitHeight + 3
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical: DeskScrollBar {}
    clip: true
    ColumnLayout {
        id: rack
        width: root.availableWidth - 12; spacing: 9
        Section { Layout.fillWidth: true; heading: "Pin drawing controls"; ruled: false }
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: studio.personal
                delegate: DeskButton {
                    required property var modelData
                    objectName: "pin_" + modelData.key
                    text: modelData.title; iconName: modelData.picked ? "check" : "plus"; picked: modelData.picked
                    size: 40; onClicked: studio.pin(modelData.key)
                }
            }
        }
        Section { Layout.fillWidth: true; heading: "Your working values" }
        Type {
            Layout.fillWidth: true; visible: !studio.personal.some(row=>row.picked)
            text: "Nothing pinned yet. Choose a control above to make it available here."; color: Desk.muted; wrapMode: Text.WordWrap; font.pixelSize: 13
        }
        Repeater {
            model: studio.personal.filter(row=>row.picked).map(row=>row.key)
            delegate: Knob {
                required property string modelData
                readonly property var info: studio.personal.find(row=>row.key===modelData)
                Layout.fillWidth: true; label: info.title; low: info.low; high: info.high; unit: info.suffix
                keyName: "my_" + modelData
                value: studio.working[modelData] === undefined ? studio.brush[modelData] : studio.working[modelData]
                onChanged: next => studio.editWorking(modelData, next)
            }
        }
        DeskButton { visible: studio.personal.some(row=>row.picked); Layout.alignment: Qt.AlignRight; text: "Reset working values"; iconName: "undo"; onClicked: studio.resetWorking() }
    }
}
