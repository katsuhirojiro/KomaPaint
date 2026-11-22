import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: list
    property var specs: []
    property var values: ({})
    property string keyPrefix: ""
    property bool compact: false
    signal edited(string key, var value)
    spacing: 8
    Repeater {
        model: list.specs
        delegate: Knob {
            required property var modelData
            Layout.fillWidth: true
            label: modelData[1]; keyName: list.keyPrefix + modelData[0]
            low: modelData[2]; high: modelData[3]; unit: modelData[4]
            value: list.values[modelData[0]] !== undefined ? list.values[modelData[0]] : 0
            hint: modelData[5] || ""; narrow: list.compact
            onChanged: next => list.edited(modelData[0], next)
        }
    }
}
