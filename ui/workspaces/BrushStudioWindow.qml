import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import KomaPaint 1.0
import "../components"
import "../themes"

// A native window, not an item in the docking workspace. Its draft belongs to
// this window and never reads or replaces the active project's brush settings.
Window {
    id: studio
    objectName: "brushStudioWindow"
    title: qsTr("KomaPaint — Brush Studio")
    flags: Qt.Window
    modality: Qt.NonModal
    width: 960
    height: 600
    minimumWidth: 360
    minimumHeight: 240
    color: KP.windowBg

    BrushStudioModel {
        id: draft
        objectName: "brushStudioModel"
    }

    function present() {
        if (visibility === Window.Minimized) showNormal()
        else show()
        raise()
        requestActivate()
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * KP.gapXL, implicitWidth)
        spacing: KP.gapM

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Brush Studio")
            color: KP.text
            font.family: KP.fontDisplay
            font.pixelSize: KP.fontDisplay1
            font.weight: Font.DemiBold
        }
        Text {
            objectName: "brushStudioDraftName"
            Layout.fillWidth: true
            text: qsTr("Draft: %1").arg(draft.name)
            color: KP.text
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Brush editing is not available in this build.")
            color: KP.textMuted
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: "brushStudioError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: draft.error
            color: KP.error
            font.family: KP.fontUi
            font.pixelSize: KP.fontM
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        KpButton {
            Layout.alignment: Qt.AlignHCenter
            label: qsTr("Close")
            variant: "solid"
            onClicked: studio.close()
        }
    }
}
