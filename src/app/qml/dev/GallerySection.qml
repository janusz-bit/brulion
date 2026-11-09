pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GallerySection — one page of the component gallery: a heading, a short description and
// the specimens (children, laid out in a column).
ColumnLayout {
    id: section

    property string title
    property string description
    default property alias content: body.data

    spacing: Theme.space5

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1

        Text {
            Layout.fillWidth: true
            text: section.title
            font.family: Theme.fontUi
            font.pixelSize: Theme.text2xl
            font.weight: Theme.weightSemiBold
            color: Theme.fg
            wrapMode: Text.Wrap
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }
        BText {
            Layout.fillWidth: true
            visible: section.description !== ""
            text: section.description
            variant: "muted"
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Theme.space6
    }
}
