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
    color: theme.window

    Material.theme: Material.Dark
    Material.accent: theme.accent
    // roundedScale is a step scale, not pixels: map Hyprland's radius to
    // the nearest step instead of passing the raw value through.
    Material.roundedScale: theme.radius <= 0 ? Material.NotRounded
                         : theme.radius <= 4 ? Material.ExtraSmallScale
                         : theme.radius <= 8 ? Material.SmallScale
                         : theme.radius <= 12 ? Material.MediumScale
                         : theme.radius <= 16 ? Material.LargeScale
                         : Material.ExtraLargeScale

    readonly property bool typing: editor.activeFocus

    // Single letters only fire while the editor does not have focus, so they
    // never eat typed characters. Escape leaves the editor.
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { helpPopup.close(); root.forceActiveFocus() } }
    Shortcut { sequence: "?"; enabled: !win.typing; context: Qt.ApplicationShortcut
        onActivated: helpPopup.visible ? helpPopup.close() : helpPopup.open() }
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
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 56

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
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
                        flat: true
                        onClicked: backend.copyImage()
                    }
                    Button {
                        text: qsTr("Save PNG")
                        highlighted: true
                        Material.background: theme.accent
                        Material.foreground: theme.accentForeground
                        onClicked: backend.saveImage()
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Horizontal
                spacing: 0

                handle: Rectangle {
                    implicitWidth: 9
                    color: theme.window
                    Rectangle {
                        anchors.centerIn: parent
                        width: 1
                        height: parent.height
                        color: (SplitHandle.hovered || SplitHandle.pressed) ? theme.accent : theme.border
                    }
                }

                Rectangle {
                    SplitView.preferredWidth: parent.width * 0.42
                    SplitView.minimumWidth: 260
                    color: theme.surface
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        TextArea {
                            id: editor
                            text: backend.code
                            onTextChanged: if (text !== backend.code) backend.code = text
                            placeholderText: backend.code.length ? "" : qsTr("Type or paste code")
                            font.family: "monospace"
                            font.pixelSize: 14
                            leftPadding: 16
                            rightPadding: 16
                            topPadding: 16
                            wrapMode: TextEdit.NoWrap
                            selectByMouse: true
                            background: null
                            Component.onCompleted: if (backend.code.length === 0) forceActiveFocus()
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        visible: editor.activeFocus
                        color: theme.accent
                    }
                }

                Rectangle {
                    id: pane
                    SplitView.fillWidth: true
                    color: theme.window
                    clip: true

                    // The render is 2x; scale it down to fit both directions, never up.
                    Image {
                        id: preview
                        readonly property real fit: Math.min(1, (pane.width - 32) / Math.max(1, implicitWidth / 2),
                                                             (pane.height - 32) / Math.max(1, implicitHeight / 2))
                        anchors.centerIn: parent
                        source: "image://preview/" + backend.revision
                        width: implicitWidth / 2 * fit
                        height: implicitHeight / 2 * fit
                        cache: false
                        smooth: true
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: theme.border }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 36

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    verticalAlignment: Text.AlignVCenter
                    text: backend.status.length ? backend.status : qsTr("Press ? for shortcuts")
                    color: backend.statusIsError ? theme.error : theme.mutedText
                    elide: Text.ElideMiddle
                }
            }
        }

        Popup {
            id: helpPopup
            parent: Overlay.overlay
            x: Math.round((parent.width - width) / 2)
            y: Math.round((parent.height - height) / 2)
            modal: true
            dim: true
            focus: true
            padding: 24
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            Overlay.modal: Rectangle { color: theme.scrim }

            background: Rectangle {
                color: theme.surface
                radius: theme.radius
                border.color: theme.border
            }

            contentItem: Column {
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
