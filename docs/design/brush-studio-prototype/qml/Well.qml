import QtQuick

Rectangle {
    id: root
    property string asset: ""
    property string kind: "mark"
    color: kind === "texture" || kind === "mask" ? "#eae0d1" : "#f4eadb"
    radius: 5
    border.color: "#ded1bd"
    clip: true
    Image {
        visible: root.asset.length > 0 && root.kind !== "group" && root.kind !== "brush"
        anchors.fill: parent; anchors.margins: root.kind === "texture" ? 1 : Math.max(2, width * .08)
        source: visible ? "image://assets/" + (root.kind === "mask" ? "mark" : root.kind) + "/" + root.asset : ""
        fillMode: Image.PreserveAspectCrop
        smooth: true
    }
}
