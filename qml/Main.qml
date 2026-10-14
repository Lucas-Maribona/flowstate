import QtQuick

Rectangle {
    color: "#1d2430"

    Rectangle {
        x: 16
        y: 16
        width: 120
        height: 48
        color: "#36c7a7"
    }

    Text {
        anchors.centerIn: parent
        text: "Flowstate · Vulkan"
        color: "white"
        font.pixelSize: 22
    }
}
