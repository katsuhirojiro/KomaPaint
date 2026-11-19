import QtQuick
import KomaPaint 1.0
import "../../components"

KpMenu {
    id: menu
    required property BrushStudioModel draft
    Instantiator {
        model: menu.draft.componentKinds
        delegate: KpMenuItem {
            required property var modelData
            text: modelData.label
            objectName: "brushStudioAdd_" + modelData.name
            onTriggered: menu.draft.addComponent(modelData.value)
        }
        onObjectAdded: function(index, entry) { menu.insertItem(index, entry) }
        onObjectRemoved: function(index, entry) { menu.removeItem(entry) }
    }
}
