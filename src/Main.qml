import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1200
    height: 720
    minimumWidth: 760
    minimumHeight: 460
    visible: true
    title: "codeshot"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool typing: editor.activeFocus

    // Single letters only fire while the editor does not have focus, so they
    // never eat typed characters. Escape leaves the editor.
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; root.forceActiveFocus() } }
    Shortcut { sequence: "?"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.copyImage() }
    Shortcut { sequence: "Ctrl+Shift+C"; context: Qt.ApplicationShortcut; onActivated: backend.copyImage() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: backend.saveImage() }
    Shortcut { sequence: "Ctrl+L"; context: Qt.ApplicationShortcut; onActivated: editor.forceActiveFocus() }
    Shortcut { sequence: "L"; enabled: !win.typing; context: Qt.ApplicationShortcut
        onActivated: langBox.currentIndex = (langBox.currentIndex + 1) % langBox.count }
    Shortcut { sequence: "W"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.windowChrome = !backend.windowChrome }

    Item {
        id: root
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                spacing: 12
                ComboBox {
                    id: langBox
                    model: backend.languages
                    currentIndex: Math.max(0, backend.languages.indexOf(backend.language))
                    onActivated: backend.language = currentText
                    Layout.preferredWidth: 160
                }
                Switch {
                    text: qsTr("Window frame")
                    checked: backend.windowChrome
                    onToggled: backend.windowChrome = checked
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Copy")
                    onClicked: backend.copyImage()
                }
                Button {
                    text: qsTr("Save PNG")
                    highlighted: true
                    onClicked: backend.saveImage()
                }
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Horizontal

                ScrollView {
                    SplitView.preferredWidth: parent.width * 0.42
                    SplitView.minimumWidth: 240
                    TextArea {
                        id: editor
                        text: backend.code
                        onTextChanged: if (text !== backend.code) backend.code = text
                        placeholderText: qsTr("Type or paste code")
                        font.family: "monospace"
                        font.pixelSize: 14
                        wrapMode: TextEdit.NoWrap
                        selectByMouse: true
                        background: Rectangle { color: "#16161a"; radius: theme.radius }
                        Component.onCompleted: if (backend.code.length === 0) forceActiveFocus()
                    }
                }

                Rectangle {
                    SplitView.fillWidth: true
                    color: "#16161a"
                    radius: theme.radius
                    clip: true

                    Flickable {
                        anchors.fill: parent
                        contentWidth: preview.width
                        contentHeight: preview.height
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        ScrollBar.horizontal: ScrollBar {}

                        Image {
                            id: preview
                            // Fit the width of the pane, never upscaling the 2x render.
                            property real fit: Math.min(1, (win.width * 0.58 - 40) / Math.max(1, implicitWidth / 2))
                            source: "image://preview/" + backend.revision
                            width: implicitWidth / 2 * fit
                            height: implicitHeight / 2 * fit
                            cache: false
                            smooth: true
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: backend.status.length ? backend.status : qsTr("Press ? for shortcuts")
                color: backend.statusIsError ? "#ff6b6b" : Material.hintTextColor
                elide: Text.ElideMiddle
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: "#d0000000"
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: 440
                height: helpColumn.implicitHeight + 48
                color: "#1b1b20"
                radius: theme.radius
                border.color: theme.accent

                Column {
                    id: helpColumn
                    anchors.centerIn: parent
                    spacing: 8
                    Label { text: qsTr("Shortcuts"); font.bold: true; font.pixelSize: 18; color: theme.accent }
                    Repeater {
                        model: [
                            ["Esc", qsTr("Leave the editor")],
                            ["Ctrl+L", qsTr("Focus the editor")],
                            ["Space / Ctrl+Shift+C", qsTr("Copy image")],
                            ["Ctrl+S", qsTr("Save PNG")],
                            ["L", qsTr("Next language")],
                            ["W", qsTr("Toggle window frame")],
                            ["?", qsTr("Show shortcuts")],
                            ["Q / Ctrl+Q", qsTr("Quit")]
                        ]
                        delegate: Row {
                            spacing: 16
                            Label { text: modelData[0]; width: 170; color: theme.accent }
                            Label { text: modelData[1] }
                        }
                    }
                }
            }
        }
    }
}
